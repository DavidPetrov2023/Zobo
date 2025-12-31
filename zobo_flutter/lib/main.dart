import 'dart:async';
import 'dart:io';
import 'package:flutter/material.dart';
import 'package:permission_handler/permission_handler.dart';
import 'services/ble_service.dart';
import 'widgets/hold_repeat_button.dart';

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

class _HomePageState extends State<HomePage> {
  final BleService _bleService = BleService();
  final TextEditingController _messageController = TextEditingController();
  final List<String> _logMessages = [];
  final ScrollController _scrollController = ScrollController();

  bool _isScanning = false;
  bool _isConnected = false;
  String? _deviceName;

  late StreamSubscription<bool> _scanSubscription;
  late StreamSubscription<bool> _connectionSubscription;
  late StreamSubscription<String?> _deviceNameSubscription;
  late StreamSubscription<String> _logSubscription;

  @override
  void initState() {
    super.initState();
    _setupSubscriptions();
    _requestPermissions();
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

  @override
  void dispose() {
    _scanSubscription.cancel();
    _connectionSubscription.cancel();
    _deviceNameSubscription.cancel();
    _logSubscription.cancel();
    _bleService.dispose();
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
      ),
      body: SafeArea(
        child: Padding(
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
              const SizedBox(height: 16),
              _buildDPad(),
              const SizedBox(height: 16),
              _buildLogSection(),
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
          const SizedBox(width: 16),
          OutlinedButton(
            onPressed: _clearLog,
            child: const Text("Clear Log"),
          ),
        ],
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
    return Expanded(
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
