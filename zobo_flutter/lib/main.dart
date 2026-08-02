import 'dart:async';
import 'dart:io';
import 'package:flutter/material.dart';
import 'package:permission_handler/permission_handler.dart';
import 'services/ble_service.dart';
import 'services/camera_service.dart';
import 'widgets/hold_repeat_button.dart';
import 'pages/camera_page.dart';
import 'pages/settings_page.dart';

const bool kDebugMode = bool.fromEnvironment('DEBUG_MODE', defaultValue: false);

void main() {
  runApp(const ZoboApp());
}

class ZoboApp extends StatelessWidget {
  const ZoboApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Zobo Controller',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.blue),
        useMaterial3: true,
      ),
      darkTheme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: Colors.blue,
          brightness: Brightness.dark,
        ),
        useMaterial3: true,
      ),
      home: const HomePage(),
    );
  }
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> with WidgetsBindingObserver {
  final BleService _bleService = BleService();
  // The camera is the robot's second board and talks over WiFi, so it comes up
  // on its own without waiting for the BLE link. The service lives here rather
  // than on the camera page because both the small view below and that page
  // show the same stream, and the camera only serves one watcher at a time.
  final CameraService _cameraService = CameraService();
  final TextEditingController _messageController = TextEditingController();
  final List<String> _logMessages = [];
  final ScrollController _scrollController = ScrollController();

  bool _isScanning = false;
  bool _isConnected = false;
  String? _deviceName;
  CamStatus _camStatus = const CamStatus(CamState.idle);

  late StreamSubscription<bool> _scanSubscription;
  late StreamSubscription<bool> _connectionSubscription;
  late StreamSubscription<String?> _deviceNameSubscription;
  late StreamSubscription<String> _logSubscription;
  late StreamSubscription<CamStatus> _camStatusSubscription;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _setupSubscriptions();
    _startup();
    _cameraService.startSaved();
  }

  // The robot is the only thing this app talks to, so there is nothing to
  // decide on the first screen - the scan starts by itself. It waits for the
  // permission dialog first, because a scan without those rights finds nothing
  // on Android and would just time out.
  Future<void> _startup() async {
    await _requestPermissions();
    if (!mounted) return;
    _bleService.startScan();
  }

  // A stream nobody is looking at still costs WiFi and battery on both ends,
  // so it goes away with the app and comes back with it.
  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.resumed) {
      _cameraService.resume();
    } else {
      _cameraService.stop();
    }
  }

  void _setupSubscriptions() {
    _scanSubscription = _bleService.isScanning.listen((scanning) {
      setState(() => _isScanning = scanning);
    });

    _connectionSubscription = _bleService.isConnected.listen((connected) {
      setState(() => _isConnected = connected);
    });

    _deviceNameSubscription = _bleService.deviceNameStream.listen((name) {
      setState(() => _deviceName = name);
    });

    _camStatusSubscription = _cameraService.status.listen((status) {
      setState(() => _camStatus = status);
    });

    _logSubscription = _bleService.logMessages.listen((message) {
      setState(() {
        _logMessages.add(message);
        if (_logMessages.length > 100) {
          _logMessages.removeAt(0);
        }
      });
      _scrollToBottom();
    });
  }

  Future<void> _requestPermissions() async {
    if (Platform.isAndroid) {
      await [
        Permission.bluetoothScan,
        Permission.bluetoothConnect,
        Permission.location,
      ].request();
    }
  }

  void _scrollToBottom() {
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (_scrollController.hasClients) {
        _scrollController.animateTo(
          _scrollController.position.maxScrollExtent,
          duration: const Duration(milliseconds: 100),
          curve: Curves.easeOut,
        );
      }
    });
  }

  void _clearLog() {
    setState(() => _logMessages.clear());
  }

  void _showAboutDialog(BuildContext context) {
    showAboutDialog(
      context: context,
      applicationName: 'Zobo Controller',
      applicationVersion: '1.0.0',
      applicationLegalese: '© 2024 David Petrov',
      children: [
        const SizedBox(height: 16),
        const Text('ESP32 BLE robot controller app'),
      ],
    );
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _scanSubscription.cancel();
    _connectionSubscription.cancel();
    _deviceNameSubscription.cancel();
    _logSubscription.cancel();
    _camStatusSubscription.cancel();
    _bleService.dispose();
    _cameraService.dispose();
    _messageController.dispose();
    _scrollController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Zobo Controller'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
        actions: [
          IconButton(
            icon: const Icon(Icons.info_outline),
            onPressed: () => _showAboutDialog(context),
            tooltip: 'About',
          ),
          // The camera is a separate board on WiFi, so this does not wait for
          // the BLE link to the robot.
          IconButton(
            icon: const Icon(Icons.videocam),
            onPressed: _openCamera,
            tooltip: 'Camera',
          ),
          IconButton(
            icon: const Icon(Icons.settings),
            onPressed: _isConnected
                ? () {
                    Navigator.push(
                      context,
                      MaterialPageRoute(
                        builder: (context) => SettingsPage(bleService: _bleService),
                      ),
                    );
                  }
                : null,
            tooltip: 'Settings',
          ),
        ],
      ),
      // Scrollable because the picture pushed the page past the height of a
      // short screen, and a keyboard over the message field takes another
      // chunk of it.
      body: SafeArea(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(16.0),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              _buildStatusSection(),
              const SizedBox(height: 16),
              _buildConnectionButtons(),
              const SizedBox(height: 16),
              _buildMessageInput(),
              const SizedBox(height: 8),
              _buildActionButtons(),
              const SizedBox(height: 12),
              // The sensor gives 4:3, so the box is that shape and no part of
              // the picture is wasted on black borders.
              AspectRatio(aspectRatio: 4 / 3, child: _buildCameraSection()),
              const SizedBox(height: 12),
              _buildDPad(),
              if (kDebugMode) ...[
                const SizedBox(height: 16),
                _buildLogSection(),
              ],
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildStatusSection() {
    String statusText;
    Color statusColor;

    if (_isConnected) {
      statusText = "Connected to: ${_deviceName ?? 'Unknown'}";
      statusColor = Colors.green;
    } else if (_isScanning) {
      statusText = "Scanning...";
      statusColor = Colors.orange;
    } else {
      statusText = "Disconnected";
      statusColor = Colors.grey;
    }

    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: statusColor.withOpacity(0.1),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: statusColor),
      ),
      child: Row(
        children: [
          Icon(
            _isConnected ? Icons.bluetooth_connected : Icons.bluetooth,
            color: statusColor,
          ),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              statusText,
              style: TextStyle(
                color: statusColor,
                fontWeight: FontWeight.bold,
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildConnectionButtons() {
    return Row(
      children: [
        Expanded(
          child: ElevatedButton.icon(
            onPressed: (!_isScanning && !_isConnected)
                ? () => _bleService.startScan()
                : null,
            icon: const Icon(Icons.search),
            label: const Text("Scan"),
          ),
        ),
        const SizedBox(width: 8),
        Expanded(
          child: ElevatedButton.icon(
            onPressed: _isScanning ? () => _bleService.stopScan() : null,
            icon: const Icon(Icons.stop),
            label: const Text("Stop"),
          ),
        ),
        const SizedBox(width: 8),
        Expanded(
          child: ElevatedButton.icon(
            onPressed: _isConnected ? () => _bleService.disconnect() : null,
            icon: const Icon(Icons.bluetooth_disabled),
            label: const Text("Disconnect"),
          ),
        ),
      ],
    );
  }

  Widget _buildMessageInput() {
    return Row(
      children: [
        Expanded(
          child: TextField(
            controller: _messageController,
            decoration: const InputDecoration(
              labelText: "Message",
              border: OutlineInputBorder(),
              contentPadding: EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            ),
          ),
        ),
        const SizedBox(width: 8),
        ElevatedButton(
          onPressed: _isConnected
              ? () {
                  if (_messageController.text.isNotEmpty) {
                    _bleService.sendLine(_messageController.text);
                    _messageController.clear();
                  }
                }
              : null,
          child: const Text("Send"),
        ),
      ],
    );
  }

  Widget _buildActionButtons() {
    return SingleChildScrollView(
      scrollDirection: Axis.horizontal,
      child: Row(
        children: [
          ElevatedButton(
            onPressed: _isConnected
                ? () => _bleService.sendCommand(RobotCommand.ledBlue)
                : null,
            style: ElevatedButton.styleFrom(backgroundColor: Colors.blue.shade100),
            child: const Text("Blue"),
          ),
          const SizedBox(width: 8),
          ElevatedButton(
            onPressed: _isConnected
                ? () => _bleService.sendCommand(RobotCommand.ledRed)
                : null,
            style: ElevatedButton.styleFrom(backgroundColor: Colors.red.shade100),
            child: const Text("Red"),
          ),
          const SizedBox(width: 8),
          ElevatedButton(
            onPressed: _isConnected
                ? () => _bleService.sendCommand(RobotCommand.ledGreen)
                : null,
            style: ElevatedButton.styleFrom(backgroundColor: Colors.green.shade100),
            child: const Text("Green"),
          ),
          const SizedBox(width: 8),
          ElevatedButton(
            onPressed: _isConnected
                ? () => _bleService.sendCommand(RobotCommand.ledAll)
                : null,
            style: ElevatedButton.styleFrom(backgroundColor: Colors.yellow.shade100),
            child: const Text("Light"),
          ),
          const SizedBox(width: 8),
          // The camera carries a light of its own, on the other board. It is
          // wanted while driving into a dark corner, so it sits here next to
          // the robot's own lights and not only on the camera page. No BLE
          // link needed - this one goes over WiFi.
          ValueListenableBuilder<bool>(
            valueListenable: _cameraService.ledOn,
            builder: (context, on, _) => ElevatedButton.icon(
              onPressed: _toggleCamLed,
              style: ElevatedButton.styleFrom(
                backgroundColor: on ? Colors.amber.shade300 : null,
              ),
              icon: Icon(
                on ? Icons.flashlight_on : Icons.flashlight_off,
                size: 18,
              ),
              label: const Text("Cam light"),
            ),
          ),
          if (kDebugMode) ...[
            const SizedBox(width: 16),
            OutlinedButton(
              onPressed: _clearLog,
              child: const Text("Clear Log"),
            ),
          ],
        ],
      ),
    );
  }

  Future<void> _toggleCamLed() async {
    final ok = await _cameraService.toggleLed();
    if (!ok && mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('The camera did not take the light command'),
        ),
      );
    }
  }

  void _openCamera() {
    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (context) => CameraPage(camera: _cameraService),
      ),
    );
  }

  Widget _buildCameraSection() {
    return GestureDetector(
      onTap: _openCamera,
      child: Stack(
        fit: StackFit.expand,
        children: [
          CameraView(camera: _cameraService, status: _camStatus),
          Positioned(left: 8, bottom: 8, child: _buildCameraLabel()),
        ],
      ),
    );
  }

  Widget _buildCameraLabel() {
    final String text;
    switch (_camStatus.state) {
      case CamState.live:
        text = '${_camStatus.fps.toStringAsFixed(0)} fps';
      case CamState.searching:
        text = 'searching ${(_camStatus.progress * 100).round()} %';
      case CamState.idle:
        text = 'camera off';
      case CamState.connecting:
      case CamState.retrying:
      case CamState.error:
        text = _camStatus.message;
    }

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      decoration: BoxDecoration(
        color: Colors.black54,
        borderRadius: BorderRadius.circular(6),
      ),
      child: Text(
        text,
        style: const TextStyle(color: Colors.white70, fontSize: 12),
      ),
    );
  }

  Widget _buildDPad() {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Theme.of(context).colorScheme.surfaceContainerHighest,
        borderRadius: BorderRadius.circular(16),
      ),
      child: Column(
        children: [
          const Text(
            "Movement Control",
            style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16),
          ),
          const SizedBox(height: 12),
          Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              HoldRepeatButton(
                text: "Forward",
                icon: Icons.arrow_upward,
                enabled: _isConnected,
                repeatMs: 100,
                onRepeat: () => _bleService.sendCommand(RobotCommand.moveForward),
                onRelease: () => _bleService.sendCommand(RobotCommand.moveStop),
                width: 80,
                height: 60,
              ),
            ],
          ),
          const SizedBox(height: 8),
          Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              HoldRepeatButton(
                text: "Left",
                icon: Icons.arrow_back,
                enabled: _isConnected,
                repeatMs: 100,
                onRepeat: () => _bleService.sendCommand(RobotCommand.moveLeft),
                onRelease: () => _bleService.sendCommand(RobotCommand.moveStop),
                width: 80,
                height: 60,
              ),
              const SizedBox(width: 8),
              HoldRepeatButton(
                text: "Stop",
                icon: Icons.stop,
                enabled: _isConnected,
                repeatMs: 100,
                onRepeat: () => _bleService.sendCommand(RobotCommand.moveStop),
                width: 80,
                height: 60,
              ),
              const SizedBox(width: 8),
              HoldRepeatButton(
                text: "Right",
                icon: Icons.arrow_forward,
                enabled: _isConnected,
                repeatMs: 100,
                onRepeat: () => _bleService.sendCommand(RobotCommand.moveRight),
                onRelease: () => _bleService.sendCommand(RobotCommand.moveStop),
                width: 80,
                height: 60,
              ),
            ],
          ),
          const SizedBox(height: 8),
          Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              HoldRepeatButton(
                text: "Backward",
                icon: Icons.arrow_downward,
                enabled: _isConnected,
                repeatMs: 100,
                onRepeat: () => _bleService.sendCommand(RobotCommand.moveBackward),
                onRelease: () => _bleService.sendCommand(RobotCommand.moveStop),
                width: 80,
                height: 60,
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _buildLogSection() {
    // A fixed height, not Expanded: the page scrolls now, so there is no
    // leftover space to claim.
    return SizedBox(
      height: 220,
      child: Container(
        decoration: BoxDecoration(
          border: Border.all(color: Colors.grey.shade300),
          borderRadius: BorderRadius.circular(8),
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Container(
              padding: const EdgeInsets.all(8),
              decoration: BoxDecoration(
                color: Theme.of(context).colorScheme.surfaceContainerHighest,
                borderRadius: const BorderRadius.vertical(top: Radius.circular(7)),
              ),
              child: const Row(
                children: [
                  Icon(Icons.terminal, size: 16),
                  SizedBox(width: 8),
                  Text("Log", style: TextStyle(fontWeight: FontWeight.bold)),
                ],
              ),
            ),
            Expanded(
              child: ListView.separated(
                controller: _scrollController,
                padding: const EdgeInsets.all(8),
                itemCount: _logMessages.length,
                separatorBuilder: (_, __) => const Divider(height: 1),
                itemBuilder: (context, index) {
                  return Text(
                    _logMessages[index],
                    style: const TextStyle(
                      fontFamily: 'monospace',
                      fontSize: 12,
                    ),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                  );
                },
              ),
            ),
          ],
        ),
      ),
    );
  }
}
