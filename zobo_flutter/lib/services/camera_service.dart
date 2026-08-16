import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:math';

import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;
import 'package:shared_preferences/shared_preferences.dart';

enum CamState { idle, connecting, live, retrying, searching, error }

@immutable
class CamStatus {
  final CamState state;
  final String message;
  final double fps;
  final double kbps;

  /// How far a network search has got, 0 to 1. Meaningless in other states.
  final double progress;

  const CamStatus(
    this.state, {
    this.message = '',
    this.fps = 0,
    this.kbps = 0,
    this.progress = 0,
  });
}

/// A camera answering /status on the local network.
class CameraFound {
  final String host;
  final String firmware;

  const CameraFound(this.host, this.firmware);
}

/// Live picture from the ESP32-CAM over the local network.
///
/// The board is behind NAT, so the website gets its frames pushed over MQTT.
/// On the same WiFi none of that is needed - the camera serves plain MJPEG and
/// the phone can pull it directly, which is both sharper and about a second
/// less delayed than the broker path.
///
/// Picture and control sit on two different ports on purpose: the stream
/// handler occupies the camera's whole HTTP task while somebody is watching,
/// so /status and /led only answer because the stream is elsewhere.
class CameraService {
  static const int streamPort = 81;
  static const int controlPort = 80;

  static const String hostPrefsKey = 'cam_host';

  /// Where the camera has been living. Only a starting guess - DHCP hands out
  /// whatever it likes after a router reboot, which is why a failed connection
  /// searches the network once.
  static const String defaultHost = '192.168.0.243';

  /// What the picture is asked to be, every time we connect.
  ///
  /// The camera keeps whatever it was last told, and that is not something to
  /// rely on: a reboot puts it back to HVGA, and anyone tuning it over /set
  /// leaves it wherever they finished. Asking on every connect makes the frame
  /// rate a property of this app instead of a leftover.
  ///
  /// QVGA is the choice that matters most. The sensor reads it with binning and
  /// manages roughly twice the frames of HVGA (~24 against ~12.5 per second),
  /// while the phone scales both to the same size anyway - on a screen held at
  /// arm's length, smoothness beats resolution.
  static const int _wantFrameSize = 6;   // QVGA 320x240
  static const int _wantQuality = 12;    // lower is better; 12 is the usual trade

  /// The stream is a TCP connection that carries no keepalive of its own: when
  /// the camera reboots or WiFi drops, the socket often just goes quiet instead
  /// of closing. Silence this long is treated as a dead link.
  static const Duration _stallTimeout = Duration(seconds: 6);
  static const Duration _retryDelay = Duration(seconds: 3);

  final _frames = StreamController<Uint8List>.broadcast();
  final _status = StreamController<CamStatus>.broadcast();

  Stream<Uint8List> get frames => _frames.stream;
  Stream<CamStatus> get status => _status.stream;
  CamStatus get lastStatus => _lastStatus;

  String _host = '';
  bool _wanted = false;
  HttpClient? _client;
  StreamSubscription<List<int>>? _subscription;
  Timer? _retryTimer;
  Timer? _statsTimer;

  CamStatus _lastStatus = const CamStatus(CamState.idle);
  DateTime _lastFrameAt = DateTime.now();
  int _framesThisSecond = 0;
  int _bytesThisSecond = 0;

  // Multipart parser state.
  final List<int> _header = [];
  Uint8List? _body;
  int _bodyFilled = 0;

  bool _searching = false;
  bool _searchedAfterFailure = false;

  /// Whether a failed connection may go looking for the camera. Off in tests,
  /// where sweeping the real network would be both slow and rude.
  bool autoSearch = true;

  String get host => _host;
  bool get running => _wanted;
  bool get searching => _searching;

  /// Connects to the address used last time, or to the usual one on a phone
  /// that has never seen the camera.
  Future<void> startSaved() async {
    final prefs = await SharedPreferences.getInstance();
    final saved = prefs.getString(hostPrefsKey) ?? '';
    _searchedAfterFailure = false;
    start(saved.isEmpty ? defaultHost : saved);
  }

  /// Remembers an address and switches to it.
  Future<void> useHost(String host) async {
    final target = host.trim();
    if (target.isEmpty) return;
    _searchedAfterFailure = false;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(hostPrefsKey, target);
    start(target);
  }

  /// Picks the stream back up on the address it was last watching.
  void resume() {
    if (!_wanted && !_searching && _host.isNotEmpty) start(_host);
  }

  void start(String host) {
    final target = host.trim();
    if (target.isEmpty) {
      _emit(CamState.error, 'Fill in the camera address first');
      return;
    }
    _host = target;
    _wanted = true;
    _statsTimer ??= Timer.periodic(const Duration(seconds: 1), (_) => _tick());
    _connect();
  }

  void stop() {
    _wanted = false;
    _retryTimer?.cancel();
    _retryTimer = null;
    _statsTimer?.cancel();
    _statsTimer = null;
    _abort();
    _emit(CamState.idle, '');
  }

  Future<void> _connect() async {
    if (!_wanted) return;
    _abort();
    _emit(CamState.connecting, 'Connecting to $_host...');

    final client = HttpClient()
      ..connectionTimeout = const Duration(seconds: 4)
      ..idleTimeout = const Duration(seconds: 5);
    _client = client;

    try {
      // Before the stream, not after: the first frames should already be the
      // size we want, otherwise the picture visibly changes shape a second in.
      await _applyWantedSettings();
      if (!_wanted || !identical(_client, client)) {
        client.close(force: true);
        return;
      }

      final request =
          await client.getUrl(Uri.parse('http://$_host:$streamPort/stream'));
      final response = await request.close().timeout(const Duration(seconds: 6));

      // stop() may have won the race while we were waiting on the network.
      if (!identical(_client, client)) {
        client.close(force: true);
        return;
      }
      if (response.statusCode != 200) {
        _retry('Camera answered HTTP ${response.statusCode}');
        return;
      }

      _resetParser();
      _lastFrameAt = DateTime.now();
      _emit(CamState.live, 'Live from $_host');
      _subscription = response.listen(
        _onData,
        onError: (Object e) => _retry(_short(e)),
        onDone: () => _retry('Camera closed the stream'),
        cancelOnError: true,
      );
    } on TimeoutException {
      _retry('No answer from $_host');
    } catch (e) {
      _retry(_short(e));
    }
  }

  /// Settings live on the control port, which answers even while the stream
  /// runs - the two servers are separate on the camera precisely so this works.
  Future<void> _applyWantedSettings() async {
    await _set('framesize', _wantFrameSize);
    await _set('quality', _wantQuality);
  }

  Future<void> _set(String name, int value) async {
    final client = HttpClient()
      ..connectionTimeout = const Duration(milliseconds: 800);
    try {
      final request = await client
          .getUrl(Uri.parse('http://$_host:$controlPort/set?var=$name&val=$value'));
      final response =
          await request.close().timeout(const Duration(milliseconds: 1200));
      await response.drain<void>();
    } catch (_) {
      // A camera that will not take a setting can still send a picture, and a
      // picture at the wrong size beats no picture at all. Connecting carries
      // on either way.
    } finally {
      client.close(force: true);
    }
  }

  void _retry(String message) {
    _abort();
    if (!_wanted) return;
    _emit(CamState.retrying, message);

    // The stored address goes stale whenever DHCP moves the camera. Rather
    // than loop forever on a dead address, look for it - but only once, so a
    // camera that is simply switched off does not start a scan every three
    // seconds for as long as the app is open.
    if (autoSearch && !_searchedAfterFailure && !_searching) {
      _searchedAfterFailure = true;
      unawaited(_autoSearch());
      return;
    }

    _retryTimer?.cancel();
    _retryTimer = Timer(_retryDelay, _connect);
  }

  Future<void> _autoSearch() async {
    final found = await search();
    if (found.isNotEmpty) await useHost(found.first.host);
  }

  /// Sweeps the network for cameras, pausing the picture while it runs.
  ///
  /// Returns what it found and leaves the choice to the caller; if nothing
  /// answers, the previous address is picked back up.
  Future<List<CameraFound>> search() async {
    if (_searching) return const [];
    _searching = true;
    final previous = _host;
    _wanted = false;
    _retryTimer?.cancel();
    _retryTimer = null;
    _abort();

    _emit(CamState.searching, 'Searching the network...');
    final found = await discover(
      onProgress: (progress) => _emit(CamState.searching,
          'Searching the network...', progress: progress),
    );
    _searching = false;

    if (found.isEmpty) {
      _emit(CamState.error, 'No camera found on this network');
      _host = previous;
      resume();
    }
    return found;
  }

  void _abort() {
    _subscription?.cancel();
    _subscription = null;
    _client?.close(force: true);
    _client = null;
    _resetParser();
  }

  void _tick() {
    if (_lastStatus.state == CamState.live &&
        DateTime.now().difference(_lastFrameAt) > _stallTimeout) {
      _retry('No picture for ${_stallTimeout.inSeconds} s, reconnecting');
      return;
    }

    final fps = _framesThisSecond.toDouble();
    final kbps = _bytesThisSecond / 1024;
    _framesThisSecond = 0;
    _bytesThisSecond = 0;

    if (_lastStatus.state == CamState.live) {
      _emit(CamState.live, _lastStatus.message, fps: fps, kbps: kbps);
    }
  }

  void _emit(CamState state, String message,
      {double fps = 0, double kbps = 0, double progress = 0}) {
    _lastStatus = CamStatus(state,
        message: message, fps: fps, kbps: kbps, progress: progress);
    if (!_status.isClosed) _status.add(_lastStatus);
  }

  void _resetParser() {
    _header.clear();
    _body = null;
    _bodyFilled = 0;
  }

  /// Cuts the multipart stream into whole JPEGs.
  ///
  /// The frame length is taken from the part header rather than by looking for
  /// the end-of-image marker: the marker can also appear inside the data, and
  /// the camera sends Content-Length for every frame anyway.
  void _onData(List<int> chunk) {
    var i = 0;
    while (i < chunk.length) {
      final body = _body;
      if (body != null) {
        final take = min(body.length - _bodyFilled, chunk.length - i);
        body.setRange(_bodyFilled, _bodyFilled + take, chunk, i);
        _bodyFilled += take;
        i += take;
        if (_bodyFilled == body.length) {
          _body = null;
          _bodyFilled = 0;
          _lastFrameAt = DateTime.now();
          _framesThisSecond++;
          _bytesThisSecond += body.length;
          if (!_frames.isClosed) _frames.add(body);
        }
        continue;
      }

      // Header bytes: boundary line, content type, content length, blank line.
      _header.add(chunk[i]);
      i++;
      final n = _header.length;
      if (n >= 4 &&
          _header[n - 4] == 13 &&
          _header[n - 3] == 10 &&
          _header[n - 2] == 13 &&
          _header[n - 1] == 10) {
        final length = _contentLength(String.fromCharCodes(_header));
        _header.clear();
        if (length == null || length <= 0) continue; // not a frame part
        _body = Uint8List(length);
        _bodyFilled = 0;
      } else if (n > 512) {
        // Nothing that long is a part header - the stream is out of step and
        // would never line up again on its own.
        _retry('Damaged stream from the camera');
        return;
      }
    }
  }

  static int? _contentLength(String header) {
    final match = RegExp(r'Content-Length:\s*(\d+)', caseSensitive: false)
        .firstMatch(header);
    return match == null ? null : int.tryParse(match.group(1)!);
  }

  /// Whether the flash LED is believed to be on.
  ///
  /// It lives with the service rather than with a page because the light is
  /// switched from two places - the driving screen and the camera screen - and
  /// each has to show what the other did.
  final ValueNotifier<bool> ledOn = ValueNotifier(false);

  /// The flash LED is genuinely bright, so it stays off unless asked for.
  ///
  /// The switch moves before the camera answers: over WiFi the reply takes long
  /// enough that a switch waiting for it feels broken. A refused command puts it
  /// back.
  Future<bool> setLed(bool on) async {
    if (_host.isEmpty) return false;
    final previous = ledOn.value;
    ledOn.value = on;
    try {
      final response = await http
          .get(Uri.parse('http://$_host:$controlPort/led?on=${on ? 1 : 0}'))
          .timeout(const Duration(seconds: 3));
      if (response.statusCode == 200) return true;
    } catch (_) {
      // Falls through to putting the switch back.
    }
    ledOn.value = previous;
    return false;
  }

  Future<bool> toggleLed() => setLed(!ledOn.value);

  Future<Map<String, dynamic>?> fetchStatus() async {
    if (_host.isEmpty) return null;
    try {
      final response = await http
          .get(Uri.parse('http://$_host:$controlPort/status'))
          .timeout(const Duration(seconds: 3));
      if (response.statusCode != 200) return null;
      final data = json.decode(response.body);
      return data is Map<String, dynamic> ? data : null;
    } catch (_) {
      return null;
    }
  }

  /// Sweeps the phone's own /24 for cameras.
  ///
  /// The board takes whatever address DHCP hands out, so a typed-in IP goes
  /// stale after every router reboot. mDNS would be nicer, but the firmware
  /// does not announce itself, and this finishes in a few seconds.
  static Future<List<CameraFound>> discover({
    void Function(double progress)? onProgress,
  }) async {
    final prefix = await _localSubnet();
    if (prefix == null) return const [];

    final found = <CameraFound>[];
    const batch = 32;
    for (var base = 1; base < 255; base += batch) {
      final probes = <Future<CameraFound?>>[];
      for (var last = base; last < base + batch && last < 255; last++) {
        probes.add(_probe('$prefix.$last'));
      }
      found.addAll((await Future.wait(probes)).whereType<CameraFound>());
      onProgress?.call(min(1.0, (base + batch - 1) / 254));
    }
    return found;
  }

  static Future<CameraFound?> _probe(String host) async {
    final client = HttpClient()
      ..connectionTimeout = const Duration(milliseconds: 600);
    try {
      final request =
          await client.getUrl(Uri.parse('http://$host:$controlPort/status'));
      final response =
          await request.close().timeout(const Duration(milliseconds: 900));
      if (response.statusCode != 200) return null;
      final body = await response
          .transform(utf8.decoder)
          .join()
          .timeout(const Duration(milliseconds: 500));
      final data = json.decode(body);
      // Any device may serve something on /status; only the camera reports a
      // frame size together with a firmware string.
      if (data is Map && data['framesize'] != null && data['fw'] != null) {
        return CameraFound(host, data['fw'].toString());
      }
      return null;
    } catch (_) {
      return null;
    } finally {
      client.close(force: true);
    }
  }

  static Future<String?> _localSubnet() async {
    try {
      final interfaces = await NetworkInterface.list(
        type: InternetAddressType.IPv4,
        includeLoopback: false,
        includeLinkLocal: false,
      );
      for (final interface in interfaces) {
        for (final address in interface.addresses) {
          final parts = address.address.split('.');
          if (parts.length != 4) continue;
          // Only private ranges - scanning a mobile carrier network would be
          // both pointless and slow.
          final first = int.tryParse(parts[0]) ?? 0;
          final second = int.tryParse(parts[1]) ?? 0;
          final private = first == 10 ||
              first == 192 && second == 168 ||
              first == 172 && second >= 16 && second <= 31;
          if (private) return '${parts[0]}.${parts[1]}.${parts[2]}';
        }
      }
    } catch (e) {
      debugPrint('Subnet lookup failed: $e');
    }
    return null;
  }

  static String _short(Object error) {
    if (error is SocketException) {
      return error.osError?.message ?? 'Network unreachable';
    }
    return error.toString();
  }

  void dispose() {
    stop();
    _frames.close();
    _status.close();
    ledOn.dispose();
  }
}
