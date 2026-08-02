import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:zobo_flutter/services/camera_service.dart';

/// A stand-in for the camera: serves the same multipart stream on the same
/// port, so the parser is exercised the way it runs on the phone.
Future<HttpServer> _fakeCamera(List<Uint8List> frames, {int chunk = 7}) async {
  final server =
      await HttpServer.bind(InternetAddress.loopbackIPv4, CameraService.streamPort);
  server.listen((request) async {
    final response = request.response;
    // Without this, Dart's own server holds the frames back in an output
    // buffer and nothing reaches the client until the response closes - the
    // opposite of a live stream, and not how the camera behaves.
    response.bufferOutput = false;
    response.headers.set(HttpHeaders.contentTypeHeader,
        'multipart/x-mixed-replace;boundary=zoboframe');
    for (final frame in frames) {
      response.add(utf8.encode('\r\n--zoboframe\r\n'
          'Content-Type: image/jpeg\r\n'
          'Content-Length: ${frame.length}\r\n\r\n'));
      // Deliberately awkward chunking: on WiFi a frame never arrives in one
      // piece, and part headers get split down the middle just as often.
      for (var i = 0; i < frame.length; i += chunk) {
        response.add(frame.sublist(i, (i + chunk).clamp(0, frame.length)));
        await response.flush();
      }
    }
  });
  return server;
}

Uint8List _frame(int seed, int length) =>
    Uint8List.fromList(List.generate(length, (i) => (seed + i) % 256));

Future<void> _waitFor(bool Function() done, {int seconds = 10}) async {
  final deadline = DateTime.now().add(Duration(seconds: seconds));
  while (!done() && DateTime.now().isBefore(deadline)) {
    await Future<void>.delayed(const Duration(milliseconds: 20));
  }
}

void main() {
  test('reassembles frames split across chunk boundaries', () async {
    final sent = [_frame(1, 1000), _frame(90, 33), _frame(7, 4096)];
    final server = await _fakeCamera(sent);
    final camera = CameraService()..autoSearch = false;
    final got = <Uint8List>[];
    final subscription = camera.frames.listen(got.add);

    camera.start('127.0.0.1');
    await _waitFor(() => got.length == sent.length);

    await subscription.cancel();
    camera.dispose();
    await server.close(force: true);

    expect(got, equals(sent));
  });

  test('skips a part that carries no length instead of losing the stream',
      () async {
    final server =
        await HttpServer.bind(InternetAddress.loopbackIPv4, CameraService.streamPort);
    final good = _frame(3, 512);
    server.listen((request) async {
      final response = request.response;
      response.bufferOutput = false;
      response.headers.set(HttpHeaders.contentTypeHeader,
          'multipart/x-mixed-replace;boundary=zoboframe');
      response.add(utf8.encode(
          '\r\n--zoboframe\r\nContent-Type: text/plain\r\n\r\n'));
      response.add(utf8.encode('\r\n--zoboframe\r\nContent-Type: image/jpeg\r\n'
          'Content-Length: ${good.length}\r\n\r\n'));
      response.add(good);
      await response.flush();
    });

    final camera = CameraService()..autoSearch = false;
    final got = <Uint8List>[];
    final subscription = camera.frames.listen(got.add);

    camera.start('127.0.0.1');
    await _waitFor(() => got.isNotEmpty);

    await subscription.cancel();
    camera.dispose();
    await server.close(force: true);

    expect(got, equals([good]));
  });

  test('reconnects after the camera drops the stream', () async {
    var connections = 0;
    final frame = _frame(11, 256);
    final server =
        await HttpServer.bind(InternetAddress.loopbackIPv4, CameraService.streamPort);
    server.listen((request) async {
      connections++;
      final response = request.response;
      response.headers.set(HttpHeaders.contentTypeHeader,
          'multipart/x-mixed-replace;boundary=zoboframe');
      response.add(utf8.encode('\r\n--zoboframe\r\nContent-Type: image/jpeg\r\n'
          'Content-Length: ${frame.length}\r\n\r\n'));
      response.add(frame);
      await response.flush();
      await response.close(); // camera rebooted mid-stream
    });

    final camera = CameraService()..autoSearch = false;
    final got = <Uint8List>[];
    final subscription = camera.frames.listen(got.add);

    camera.start('127.0.0.1');
    await _waitFor(() => connections >= 2 && got.length >= 2, seconds: 15);

    await subscription.cancel();
    camera.dispose();
    await server.close(force: true);

    expect(connections, greaterThanOrEqualTo(2));
    expect(got.length, greaterThanOrEqualTo(2));
  }, timeout: const Timeout(Duration(seconds: 30)));
}
