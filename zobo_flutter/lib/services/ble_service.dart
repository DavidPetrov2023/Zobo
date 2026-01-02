import 'dart:async';
import 'dart:convert';
import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';

enum RobotCommand {
  moveBackward(0),
  moveForward(1),
  moveStop(2),
  moveLeft(3),
  moveRight(4),
  ledGreen(10),
  ledRed(20),
  ledBlue(30),
  ledAll(40);

  final int value;
  const RobotCommand(this.value);
}

// Extended commands for WiFi and OTA
class ExtendedCommands {
  static const int wifiSet = 0x50;
  static const int wifiConnect = 0x51;
  static const int wifiDisconnect = 0x52;
  static const int wifiStatus = 0x53;
  static const int wifiClear = 0x54;
  static const int otaUpdate = 0x60;
  static const int otaCheck = 0x61;
  static const int getVersion = 0x62;
  static const int getInfo = 0x63;
  static const int ping = 0x70;  // Keepalive ping
}

class BleService {
  static const String deviceName = "Zobo";

  static final Uuid uartServiceUuid = Uuid.parse("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
  static final Uuid uartRxUuid = Uuid.parse("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
  static final Uuid uartTxUuid = Uuid.parse("6e400003-b5a3-f393-e0a9-e50e24dcca9e");

  final FlutterReactiveBle _ble = FlutterReactiveBle();

  String? _deviceId;
  StreamSubscription? _scanSubscription;
  StreamSubscription? _connectionSubscription;
  StreamSubscription? _notificationSubscription;
  Timer? _keepaliveTimer;

  final _isScanning = StreamController<bool>.broadcast();
  final _isConnected = StreamController<bool>.broadcast();
  final _deviceName = StreamController<String?>.broadcast();
  final _logMessages = StreamController<String>.broadcast();
  final _responses = StreamController<String>.broadcast();

  Stream<bool> get isScanning => _isScanning.stream;
  Stream<bool> get isConnected => _isConnected.stream;
  Stream<String?> get deviceNameStream => _deviceName.stream;
  Stream<String> get logMessages => _logMessages.stream;
  Stream<String> get responses => _responses.stream;

  bool _connected = false;
  bool _scanning = false;
  String? _connectedDeviceName;

  QualifiedCharacteristic? _rxCharacteristic;

  Future<void> startScan() async {
    if (_scanning || _connected) return;

    _scanning = true;
    _isScanning.add(true);
    _addLog("Scan", "Starting BLE scan...");

    _scanSubscription = _ble.scanForDevices(
      withServices: [],
      scanMode: ScanMode.lowLatency,
    ).listen((device) {
      final name = device.name;
      if (name.toLowerCase().contains(deviceName.toLowerCase())) {
        _addLog("Found", "Device '$name' found, connecting...");
        stopScan();
        _connectToDevice(device.id, name);
      }
    }, onError: (e) {
      _addLog("Error", "Scan error: $e");
      _scanning = false;
      _isScanning.add(false);
    });

    Future.delayed(const Duration(seconds: 15), () {
      if (_scanning && !_connected) {
        stopScan();
        _addLog("Scan", "Scan timeout, device not found");
      }
    });
  }

  Future<void> stopScan() async {
    _scanSubscription?.cancel();
    _scanSubscription = null;
    _scanning = false;
    _isScanning.add(false);
  }

  Future<void> _connectToDevice(String deviceId, String name) async {
    try {
      _deviceId = deviceId;
      _connectedDeviceName = name;

      _connectionSubscription = _ble.connectToDevice(
        id: deviceId,
        connectionTimeout: const Duration(seconds: 10),
      ).listen((state) async {
        if (state.connectionState == DeviceConnectionState.connected) {
          _addLog("Connected", "Connected to $name");
          await _discoverServices();
        } else if (state.connectionState == DeviceConnectionState.disconnected) {
          _connected = false;
          _isConnected.add(false);
          _deviceName.add(null);
          _rxCharacteristic = null;
          _addLog("Disconnected", "Device disconnected");
        }
      }, onError: (e) {
        _addLog("Error", "Connection error: $e");
        _connected = false;
        _isConnected.add(false);
      });
    } catch (e) {
      _addLog("Error", "Connection failed: $e");
      await disconnect();
    }
  }

  Future<void> _discoverServices() async {
    if (_deviceId == null) return;

    try {
      // Request larger MTU for longer messages (WiFi credentials, OTA URLs)
      try {
        final mtu = await _ble.requestMtu(deviceId: _deviceId!, mtu: 512);
        _addLog("MTU", "Negotiated MTU: $mtu");
      } catch (e) {
        _addLog("MTU", "MTU negotiation failed, using default");
      }

      _rxCharacteristic = QualifiedCharacteristic(
        serviceId: uartServiceUuid,
        characteristicId: uartRxUuid,
        deviceId: _deviceId!,
      );

      final txCharacteristic = QualifiedCharacteristic(
        serviceId: uartServiceUuid,
        characteristicId: uartTxUuid,
        deviceId: _deviceId!,
      );

      _notificationSubscription = _ble.subscribeToCharacteristic(txCharacteristic).listen((data) {
        final text = utf8.decode(data);
        _addLog("RX", text);
        _responses.add(text);
      }, onError: (e) {
        _addLog("Error", "Notification error: $e");
      });

      _connected = true;
      _isConnected.add(true);
      _deviceName.add(_connectedDeviceName);
      _startKeepalive();
      _addLog("Ready", "UART service initialized");
    } catch (e) {
      _addLog("Error", "Service discovery failed: $e");
      await disconnect();
    }
  }

  void _startKeepalive() {
    _keepaliveTimer?.cancel();
    _keepaliveTimer = Timer.periodic(const Duration(seconds: 5), (_) {
      if (_connected) {
        sendBytes([ExtendedCommands.ping]);
      }
    });
  }

  void _stopKeepalive() {
    _keepaliveTimer?.cancel();
    _keepaliveTimer = null;
  }

  Future<void> disconnect() async {
    _stopKeepalive();
    _notificationSubscription?.cancel();
    _connectionSubscription?.cancel();

    _deviceId = null;
    _rxCharacteristic = null;
    _connected = false;
    _isConnected.add(false);
    _deviceName.add(null);
  }

  Future<void> sendCommand(RobotCommand command) async {
    await sendByte(command.value);
  }

  Future<void> sendByte(int value) async {
    await sendBytes([value]);
  }

  Future<void> sendBytes(List<int> bytes) async {
    if (_rxCharacteristic == null || !_connected) return;

    try {
      await _ble.writeCharacteristicWithResponse(_rxCharacteristic!, value: bytes);
      final hexStr = bytes.map((b) => '0x${b.toRadixString(16).padLeft(2, '0')}').join(' ');
      _addLog("TX", "[bytes] $hexStr");
    } catch (e) {
      _addLog("Error", "Send failed: $e");
    }
  }

  Future<void> sendLine(String text) async {
    if (_rxCharacteristic == null || !_connected) return;

    try {
      final bytes = utf8.encode("$text\n");
      await _ble.writeCharacteristicWithResponse(_rxCharacteristic!, value: bytes);
      _addLog("TX", text);
    } catch (e) {
      _addLog("Error", "Send failed: $e");
    }
  }

  void _addLog(String tag, String message) {
    final time = DateTime.now();
    final timeStr = "${time.hour.toString().padLeft(2, '0')}:${time.minute.toString().padLeft(2, '0')}:${time.second.toString().padLeft(2, '0')}.${time.millisecond.toString().padLeft(3, '0')}";
    _logMessages.add("[$timeStr] $tag: $message");
  }

  void dispose() {
    _scanSubscription?.cancel();
    _connectionSubscription?.cancel();
    _notificationSubscription?.cancel();
    _isScanning.close();
    _isConnected.close();
    _deviceName.close();
    _logMessages.close();
    _responses.close();
  }

  // ============================================================================
  // WiFi Commands
  // ============================================================================

  Future<void> setWifiCredentials(String ssid, String password) async {
    // Format: 0x50 + SSID + \0 + PASSWORD + \0
    final bytes = [ExtendedCommands.wifiSet, ...utf8.encode(ssid), 0, ...utf8.encode(password), 0];
    await sendBytes(bytes);
  }

  Future<void> connectWifi() async {
    await sendBytes([ExtendedCommands.wifiConnect]);
  }

  Future<void> disconnectWifi() async {
    await sendBytes([ExtendedCommands.wifiDisconnect]);
  }

  Future<void> getWifiStatus() async {
    await sendBytes([ExtendedCommands.wifiStatus]);
  }

  Future<void> clearWifiCredentials() async {
    await sendBytes([ExtendedCommands.wifiClear]);
  }

  // ============================================================================
  // OTA Commands
  // ============================================================================

  Future<void> startOtaUpdate(String url) async {
    // Format: 0x60 + URL + \0
    final bytes = [ExtendedCommands.otaUpdate, ...utf8.encode(url), 0];
    await sendBytes(bytes);
  }

  Future<void> checkOtaUpdate(String versionUrl) async {
    // Format: 0x61 + URL + \0
    final bytes = [ExtendedCommands.otaCheck, ...utf8.encode(versionUrl), 0];
    await sendBytes(bytes);
  }

  Future<void> getVersion() async {
    await sendBytes([ExtendedCommands.getVersion]);
  }

  Future<void> getDeviceInfo() async {
    await sendBytes([ExtendedCommands.getInfo]);
  }
}
