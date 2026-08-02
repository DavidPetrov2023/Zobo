import 'dart:async';

import 'package:flutter/material.dart';

import '../services/camera_service.dart';
import '../widgets/live_image.dart';

/// The camera full screen, with the address and the light.
///
/// The stream itself belongs to the home page - the same picture feeds the
/// small view above the driving buttons, and the camera serves one watcher at
/// a time, so opening this page must not start a second one.
class CameraPage extends StatefulWidget {
  final CameraService camera;

  const CameraPage({super.key, required this.camera});

  @override
  State<CameraPage> createState() => _CameraPageState();
}

class _CameraPageState extends State<CameraPage> {
  final TextEditingController _hostController = TextEditingController();

  late CamStatus _status = widget.camera.lastStatus;
  StreamSubscription<CamStatus>? _statusSubscription;

  CameraService get _camera => widget.camera;

  @override
  void initState() {
    super.initState();
    _hostController.text = _camera.host;
    _statusSubscription = _camera.status.listen((status) {
      setState(() {
        _status = status;
        // The address can change under us: the service saves whatever it finds
        // on the network, and the field should not keep showing the dead one.
        if (_hostController.text != _camera.host && !_hostFocused) {
          _hostController.text = _camera.host;
        }
      });
    });
    if (!_camera.running && !_camera.searching) unawaited(_camera.startSaved());
  }

  bool _hostFocused = false;

  void _toggleStream() {
    if (_camera.running) {
      _camera.stop();
      return;
    }
    _camera.useHost(_hostController.text);
  }

  Future<void> _search() async {
    final found = await _camera.search();
    if (found.isEmpty || !mounted) return;

    final chosen = found.length == 1 ? found.first : await _pickCamera(found);
    if (chosen == null) {
      _camera.resume();
      return;
    }
    await _camera.useHost(chosen.host);
  }

  Future<CameraFound?> _pickCamera(List<CameraFound> found) {
    return showDialog<CameraFound>(
      context: context,
      builder: (context) => SimpleDialog(
        title: const Text('Pick a camera'),
        children: [
          for (final camera in found)
            SimpleDialogOption(
              onPressed: () => Navigator.pop(context, camera),
              child: Text('${camera.host}  (fw ${camera.firmware})'),
            ),
        ],
      ),
    );
  }

  Future<void> _toggleLed(bool on) async {
    // The service owns the switch position and puts it back if the camera
    // refuses, so there is nothing to undo here.
    final ok = await _camera.setLed(on);
    if (!ok && mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('The camera did not take the light command')),
      );
    }
  }

  @override
  void dispose() {
    _statusSubscription?.cancel();
    _hostController.dispose();
    // The service outlives this page - the home page owns it.
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Camera'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
        actions: [
          IconButton(
            icon: const Icon(Icons.travel_explore),
            onPressed: _status.state == CamState.searching ? null : _search,
            tooltip: 'Find the camera on the network',
          ),
        ],
      ),
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(16.0),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              _buildAddressRow(),
              const SizedBox(height: 12),
              CameraStatusBar(status: _status),
              const SizedBox(height: 12),
              Expanded(
                child: CameraView(camera: _camera, status: _status),
              ),
              const SizedBox(height: 12),
              _buildControls(),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildAddressRow() {
    return Row(
      children: [
        Expanded(
          child: Focus(
            onFocusChange: (focused) => _hostFocused = focused,
            child: TextField(
              controller: _hostController,
              keyboardType: TextInputType.url,
              autocorrect: false,
              decoration: const InputDecoration(
                labelText: 'Camera address',
                hintText: CameraService.defaultHost,
                border: OutlineInputBorder(),
                contentPadding:
                    EdgeInsets.symmetric(horizontal: 12, vertical: 8),
              ),
              onSubmitted: (_) {
                if (!_camera.running) _toggleStream();
              },
            ),
          ),
        ),
        const SizedBox(width: 8),
        ElevatedButton.icon(
          onPressed:
              _status.state == CamState.searching ? null : _toggleStream,
          icon: Icon(_camera.running ? Icons.stop : Icons.play_arrow),
          label: Text(_camera.running ? 'Stop' : 'Show'),
        ),
      ],
    );
  }

  Widget _buildControls() {
    return Row(
      children: [
        const Icon(Icons.flashlight_on),
        const SizedBox(width: 8),
        const Text('Light'),
        ValueListenableBuilder<bool>(
          valueListenable: _camera.ledOn,
          builder: (context, on, _) => Switch(
            value: on,
            onChanged: _camera.host.isEmpty ? null : _toggleLed,
          ),
        ),
      ],
    );
  }
}

/// Black box with the picture in it, shared by the home page and this one.
class CameraView extends StatelessWidget {
  final CameraService camera;
  final CamStatus status;

  const CameraView({super.key, required this.camera, required this.status});

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        color: Colors.black,
        borderRadius: BorderRadius.circular(12),
      ),
      clipBehavior: Clip.antiAlias,
      child: Center(
        child: LiveImage(
          frames: camera.frames,
          placeholder: Text(
            switch (status.state) {
              CamState.idle => 'No picture',
              CamState.searching => 'Looking for the camera...',
              _ => 'Waiting for the picture...',
            },
            style: const TextStyle(color: Colors.white54),
          ),
        ),
      ),
    );
  }
}

/// One line saying what the camera link is doing.
class CameraStatusBar extends StatelessWidget {
  final CamStatus status;

  const CameraStatusBar({super.key, required this.status});

  @override
  Widget build(BuildContext context) {
    if (status.state == CamState.searching) {
      return Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Text('Searching the network... ${(status.progress * 100).round()} %'),
          const SizedBox(height: 6),
          LinearProgressIndicator(value: status.progress),
        ],
      );
    }

    late final Color color;
    late final IconData icon;
    switch (status.state) {
      case CamState.live:
        color = Colors.green;
        icon = Icons.videocam;
      case CamState.connecting:
      case CamState.retrying:
        color = Colors.orange;
        icon = Icons.sync;
      case CamState.error:
        color = Colors.red;
        icon = Icons.error_outline;
      case CamState.searching:
      case CamState.idle:
        color = Colors.grey;
        icon = Icons.videocam_off;
    }

    final text = status.state == CamState.live
        ? '${status.message} — ${status.fps.toStringAsFixed(0)} fps, '
            '${status.kbps.toStringAsFixed(0)} kB/s'
        : (status.message.isEmpty ? 'Not watching' : status.message);

    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: color.withOpacity(0.1),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: color),
      ),
      child: Row(
        children: [
          Icon(icon, color: color),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              text,
              style: TextStyle(color: color, fontWeight: FontWeight.bold),
            ),
          ),
        ],
      ),
    );
  }
}
