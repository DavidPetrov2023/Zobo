import 'dart:async';
import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'package:shared_preferences/shared_preferences.dart';
import '../services/ble_service.dart';

// OTA Server configuration - change this to your server IP
const String otaServerBase = 'http://192.168.0.60:8080';

class SettingsPage extends StatefulWidget {
  final BleService bleService;

  const SettingsPage({super.key, required this.bleService});

  @override
  State<SettingsPage> createState() => _SettingsPageState();
}

class _SettingsPageState extends State<SettingsPage> {
  final _ssidController = TextEditingController();
  final _passwordController = TextEditingController();
  final _otaUrlController = TextEditingController();

  String _wifiStatus = 'Unknown';
  String _firmwareVersion = 'Unknown';
  String _otaStatus = '';
  int _otaProgress = 0;
  bool _isConnecting = false;
  bool _isUpdating = false;

  // Server version info
  String? _serverVersion;
  String? _serverDate;
  bool _updateAvailable = false;
  bool _checkingUpdate = false;

  StreamSubscription<String>? _responseSubscription;

  @override
  void initState() {
    super.initState();
    _loadSavedCredentials();
    _setupResponseListener();
    _requestDeviceInfo();
    _checkForUpdates();
  }

  Future<void> _checkForUpdates() async {
    setState(() => _checkingUpdate = true);
    try {
      final response = await http.get(
        Uri.parse('$otaServerBase/version.json'),
      ).timeout(const Duration(seconds: 5));

      if (response.statusCode == 200) {
        final data = json.decode(response.body);
        setState(() {
          _serverVersion = data['version'];
          _serverDate = data['date'];
          _updateAvailable = _serverVersion != null &&
              _serverVersion != _firmwareVersion &&
              _firmwareVersion != 'Unknown';
          _otaUrlController.text = '$otaServerBase/zobo_esp32.bin';
        });
      }
    } catch (e) {
      // Server not available - that's fine
    } finally {
      setState(() => _checkingUpdate = false);
    }
  }

  Future<void> _loadSavedCredentials() async {
    final prefs = await SharedPreferences.getInstance();
    setState(() {
      _ssidController.text = prefs.getString('wifi_ssid') ?? '';
      _passwordController.text = prefs.getString('wifi_password') ?? '';
      // Don't overwrite OTA URL if already set by server check
      final savedUrl = prefs.getString('ota_url') ?? '';
      if (_otaUrlController.text.isEmpty && savedUrl.isNotEmpty) {
        _otaUrlController.text = savedUrl;
      }
      // Set default if still empty
      if (_otaUrlController.text.isEmpty) {
        _otaUrlController.text = '$otaServerBase/zobo_esp32.bin';
      }
    });
  }

  Future<void> _saveCredentials() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('wifi_ssid', _ssidController.text);
    await prefs.setString('wifi_password', _passwordController.text);
    await prefs.setString('ota_url', _otaUrlController.text);
  }

  void _setupResponseListener() {
    _responseSubscription = widget.bleService.responses.listen((response) {
      _handleResponse(response);
    });
  }

  void _handleResponse(String response) {
    setState(() {
      if (response.startsWith('WIFI:')) {
        final parts = response.substring(5).split(':');
        _wifiStatus = parts.join(' ');
        _isConnecting = false;
      } else if (response.startsWith('VERSION:')) {
        _firmwareVersion = response.substring(8);
      } else if (response.startsWith('INFO:')) {
        // Parse device info
        final info = response.substring(5);
        if (info.contains('v')) {
          final versionMatch = RegExp(r'v([\d.]+)').firstMatch(info);
          if (versionMatch != null) {
            _firmwareVersion = versionMatch.group(1) ?? _firmwareVersion;
          }
        }
      } else if (response.startsWith('OTA:')) {
        final parts = response.substring(4).split(':');
        if (parts.length >= 2) {
          final progress = int.tryParse(parts[0]) ?? -1;
          final status = parts.sublist(1).join(':');
          if (progress >= 0) {
            _otaProgress = progress;
            _otaStatus = status;
          } else {
            _otaStatus = status;
          }
        } else {
          _otaStatus = parts[0];
        }

        if (_otaStatus.contains('complete')) {
          _isUpdating = false;
          _otaProgress = 100;
          // Show success dialog
          Future.delayed(const Duration(milliseconds: 500), () {
            if (mounted) {
              _showUpdateCompleteDialog();
            }
          });
        } else if (_otaStatus.contains('failed') || _otaStatus.contains('ERR')) {
          _isUpdating = false;
        }
      }
    });
  }

  void _showUpdateCompleteDialog() {
    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (ctx) => AlertDialog(
        title: const Row(
          children: [
            Icon(Icons.check_circle, color: Colors.green),
            SizedBox(width: 8),
            Text('Update Complete'),
          ],
        ),
        content: const Text(
          'Firmware has been updated successfully!\n\n'
          'The device is restarting. Please wait a moment and then reconnect via Bluetooth.',
        ),
        actions: [
          TextButton(
            onPressed: () {
              Navigator.pop(ctx);
              // Go back to main screen to reconnect
              Navigator.pop(context);
            },
            child: const Text('OK'),
          ),
        ],
      ),
    );
  }

  void _requestDeviceInfo() {
    Future.delayed(const Duration(milliseconds: 500), () {
      widget.bleService.getVersion();
      widget.bleService.getWifiStatus();
    });
  }

  @override
  void dispose() {
    _responseSubscription?.cancel();
    _ssidController.dispose();
    _passwordController.dispose();
    _otaUrlController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Settings'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            _buildDeviceInfoCard(),
            const SizedBox(height: 16),
            _buildWifiCard(),
            const SizedBox(height: 16),
            _buildOtaCard(),
          ],
        ),
      ),
    );
  }

  Widget _buildDeviceInfoCard() {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                const Icon(Icons.info_outline),
                const SizedBox(width: 8),
                Text(
                  'Device Info',
                  style: Theme.of(context).textTheme.titleLarge,
                ),
              ],
            ),
            const Divider(),
            _buildInfoRow('Firmware Version', _firmwareVersion),
            _buildInfoRow('WiFi Status', _wifiStatus),
            const SizedBox(height: 8),
            ElevatedButton.icon(
              onPressed: () {
                widget.bleService.getVersion();
                widget.bleService.getWifiStatus();
              },
              icon: const Icon(Icons.refresh),
              label: const Text('Refresh'),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildInfoRow(String label, String value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(label, style: const TextStyle(fontWeight: FontWeight.w500)),
          Text(value),
        ],
      ),
    );
  }

  Widget _buildWifiCard() {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                const Icon(Icons.wifi),
                const SizedBox(width: 8),
                Text(
                  'WiFi Configuration',
                  style: Theme.of(context).textTheme.titleLarge,
                ),
              ],
            ),
            const Divider(),
            TextField(
              controller: _ssidController,
              decoration: const InputDecoration(
                labelText: 'SSID',
                border: OutlineInputBorder(),
                prefixIcon: Icon(Icons.router),
              ),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _passwordController,
              decoration: const InputDecoration(
                labelText: 'Password',
                border: OutlineInputBorder(),
                prefixIcon: Icon(Icons.lock),
              ),
              obscureText: true,
            ),
            const SizedBox(height: 16),
            Row(
              children: [
                Expanded(
                  child: ElevatedButton.icon(
                    onPressed: _isConnecting
                        ? null
                        : () async {
                            if (_ssidController.text.isNotEmpty) {
                              // Save to local storage
                              await _saveCredentials();
                              // Send to ESP32
                              await widget.bleService.setWifiCredentials(
                                _ssidController.text,
                                _passwordController.text,
                              );
                              if (!mounted) return;
                              ScaffoldMessenger.of(context).showSnackBar(
                                const SnackBar(content: Text('WiFi credentials saved')),
                              );
                            }
                          },
                    icon: const Icon(Icons.save),
                    label: const Text('Save'),
                  ),
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: ElevatedButton.icon(
                    onPressed: _isConnecting
                        ? null
                        : () {
                            setState(() => _isConnecting = true);
                            widget.bleService.connectWifi();
                          },
                    icon: _isConnecting
                        ? const SizedBox(
                            width: 16,
                            height: 16,
                            child: CircularProgressIndicator(strokeWidth: 2),
                          )
                        : const Icon(Icons.wifi),
                    label: Text(_isConnecting ? 'Connecting...' : 'Connect'),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 8),
            Row(
              children: [
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: () => widget.bleService.disconnectWifi(),
                    icon: const Icon(Icons.wifi_off),
                    label: const Text('Disconnect'),
                  ),
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: () {
                      showDialog(
                        context: context,
                        builder: (ctx) => AlertDialog(
                          title: const Text('Clear WiFi?'),
                          content: const Text('This will remove saved WiFi credentials from the device.'),
                          actions: [
                            TextButton(
                              onPressed: () => Navigator.pop(ctx),
                              child: const Text('Cancel'),
                            ),
                            TextButton(
                              onPressed: () {
                                widget.bleService.clearWifiCredentials();
                                Navigator.pop(ctx);
                              },
                              child: const Text('Clear'),
                            ),
                          ],
                        ),
                      );
                    },
                    icon: const Icon(Icons.delete_outline),
                    label: const Text('Clear'),
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildOtaCard() {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                const Icon(Icons.system_update),
                const SizedBox(width: 8),
                Text(
                  'Firmware Update (OTA)',
                  style: Theme.of(context).textTheme.titleLarge,
                ),
                const Spacer(),
                if (_checkingUpdate)
                  const SizedBox(
                    width: 16,
                    height: 16,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  )
                else if (_updateAvailable)
                  Container(
                    padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
                    decoration: BoxDecoration(
                      color: Colors.green,
                      borderRadius: BorderRadius.circular(12),
                    ),
                    child: const Text(
                      'NEW',
                      style: TextStyle(color: Colors.white, fontSize: 12, fontWeight: FontWeight.bold),
                    ),
                  ),
              ],
            ),
            const Divider(),
            if (_serverVersion != null) ...[
              Container(
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: _updateAvailable ? Colors.green.withOpacity(0.1) : Colors.grey.withOpacity(0.1),
                  borderRadius: BorderRadius.circular(8),
                ),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [
                        const Text('Server version:', style: TextStyle(fontWeight: FontWeight.w500)),
                        Text(_serverVersion!, style: TextStyle(
                          color: _updateAvailable ? Colors.green : null,
                          fontWeight: _updateAvailable ? FontWeight.bold : null,
                        )),
                      ],
                    ),
                    if (_serverDate != null) ...[
                      const SizedBox(height: 4),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          const Text('Built:', style: TextStyle(fontWeight: FontWeight.w500)),
                          Text(_serverDate!, style: const TextStyle(fontSize: 12)),
                        ],
                      ),
                    ],
                    if (_updateAvailable) ...[
                      const SizedBox(height: 8),
                      const Text(
                        'Update available!',
                        style: TextStyle(color: Colors.green, fontWeight: FontWeight.bold),
                      ),
                    ] else ...[
                      const SizedBox(height: 8),
                      const Text(
                        'Firmware is up to date',
                        style: TextStyle(color: Colors.grey),
                      ),
                    ],
                  ],
                ),
              ),
              const SizedBox(height: 12),
            ] else ...[
              const Text(
                'OTA server not available. Enter URL manually:',
                style: TextStyle(color: Colors.grey),
              ),
              const SizedBox(height: 8),
            ],
            TextField(
              controller: _otaUrlController,
              decoration: const InputDecoration(
                labelText: 'Firmware URL',
                border: OutlineInputBorder(),
                prefixIcon: Icon(Icons.link),
                hintText: 'http://192.168.0.60:8080/zobo_esp32.bin',
              ),
            ),
            const SizedBox(height: 12),
            if (_isUpdating || _otaStatus.isNotEmpty) ...[
              Container(
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: _otaStatus.contains('ERR') || _otaStatus.contains('failed')
                      ? Colors.red.withOpacity(0.1)
                      : Colors.blue.withOpacity(0.1),
                  borderRadius: BorderRadius.circular(8),
                ),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      children: [
                        if (_isUpdating)
                          const SizedBox(
                            width: 16,
                            height: 16,
                            child: CircularProgressIndicator(strokeWidth: 2),
                          )
                        else
                          Icon(
                            _otaStatus.contains('ERR') || _otaStatus.contains('failed')
                                ? Icons.error
                                : _otaStatus.contains('complete') ? Icons.check_circle : Icons.info,
                            size: 16,
                            color: _otaStatus.contains('complete') ? Colors.green : null,
                          ),
                        const SizedBox(width: 8),
                        Expanded(child: Text(_otaStatus)),
                        if (_isUpdating) Text('$_otaProgress%', style: const TextStyle(fontWeight: FontWeight.bold)),
                      ],
                    ),
                    if (_isUpdating) ...[
                      const SizedBox(height: 8),
                      ClipRRect(
                        borderRadius: BorderRadius.circular(4),
                        child: LinearProgressIndicator(
                          value: _otaProgress / 100,
                          minHeight: 8,
                          backgroundColor: Colors.grey.shade300,
                          valueColor: const AlwaysStoppedAnimation<Color>(Colors.blue),
                        ),
                      ),
                    ],
                  ],
                ),
              ),
              const SizedBox(height: 12),
            ],
            Row(
              children: [
                Expanded(
                  child: ElevatedButton.icon(
                    onPressed: _isUpdating || _otaUrlController.text.isEmpty
                        ? null
                        : () {
                            showDialog(
                              context: context,
                              builder: (ctx) => AlertDialog(
                                title: const Text('Start Update?'),
                                content: Column(
                                  mainAxisSize: MainAxisSize.min,
                                  crossAxisAlignment: CrossAxisAlignment.start,
                                  children: [
                                    if (_serverVersion != null) ...[
                                      Row(
                                        children: [
                                          const Text('Current version: '),
                                          Text(_firmwareVersion, style: const TextStyle(fontWeight: FontWeight.bold)),
                                        ],
                                      ),
                                      const SizedBox(height: 4),
                                      Row(
                                        children: [
                                          const Text('New version: '),
                                          Text(_serverVersion!, style: const TextStyle(fontWeight: FontWeight.bold, color: Colors.green)),
                                        ],
                                      ),
                                      if (_serverDate != null) ...[
                                        const SizedBox(height: 4),
                                        Text('Built: $_serverDate', style: TextStyle(fontSize: 12, color: Colors.grey.shade600)),
                                      ],
                                      const SizedBox(height: 16),
                                    ],
                                    const Text(
                                      'The device will download and install the new firmware. '
                                      'Make sure the device is connected to WiFi and has stable power.',
                                    ),
                                  ],
                                ),
                                actions: [
                                  TextButton(
                                    onPressed: () => Navigator.pop(ctx),
                                    child: const Text('Cancel'),
                                  ),
                                  TextButton(
                                    onPressed: () {
                                      Navigator.pop(ctx);
                                      setState(() {
                                        _isUpdating = true;
                                        _otaProgress = 0;
                                        _otaStatus = 'Starting update...';
                                      });
                                      widget.bleService.startOtaUpdate(_otaUrlController.text);
                                    },
                                    child: const Text('Update'),
                                  ),
                                ],
                              ),
                            );
                          },
                    icon: _isUpdating
                        ? const SizedBox(
                            width: 16,
                            height: 16,
                            child: CircularProgressIndicator(strokeWidth: 2),
                          )
                        : const Icon(Icons.download),
                    label: Text(_isUpdating ? 'Updating...' : 'Start Update'),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 8),
            Row(
              children: [
                Expanded(
                  child: Text(
                    'Server: $otaServerBase',
                    style: TextStyle(fontSize: 12, color: Colors.grey.shade600),
                  ),
                ),
                TextButton.icon(
                  onPressed: _checkingUpdate ? null : _checkForUpdates,
                  icon: const Icon(Icons.refresh, size: 16),
                  label: const Text('Check'),
                ),
              ],
            ),
            Text(
              'Note: WiFi must be connected before starting OTA update.',
              style: TextStyle(fontSize: 12, color: Colors.grey.shade600),
            ),
          ],
        ),
      ),
    );
  }
}
