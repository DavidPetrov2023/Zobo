import 'dart:async';
import 'package:flutter/material.dart';

class HoldRepeatButton extends StatefulWidget {
  final String text;
  final IconData? icon;
  final bool enabled;
  final int repeatMs;
  final VoidCallback onRepeat;
  final VoidCallback? onRelease;
  final double? width;
  final double? height;

  const HoldRepeatButton({
    super.key,
    required this.text,
    this.icon,
    required this.enabled,
    this.repeatMs = 100,
    required this.onRepeat,
    this.onRelease,
    this.width,
    this.height,
  });

  @override
  State<HoldRepeatButton> createState() => _HoldRepeatButtonState();
}

class _HoldRepeatButtonState extends State<HoldRepeatButton> {
  Timer? _timer;
  bool _isPressed = false;

  void _startRepeating() {
    if (!widget.enabled) return;

    setState(() => _isPressed = true);
    widget.onRepeat();

    _timer = Timer.periodic(Duration(milliseconds: widget.repeatMs), (_) {
      if (_isPressed && widget.enabled) {
        widget.onRepeat();
      } else {
        _stopRepeating();
      }
    });
  }

  void _stopRepeating() {
    _timer?.cancel();
    _timer = null;
    if (mounted) {
      setState(() => _isPressed = false);
      widget.onRelease?.call();
    }
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTapDown: (_) => _startRepeating(),
      onTapUp: (_) => _stopRepeating(),
      onTapCancel: _stopRepeating,
      child: Container(
        width: widget.width,
        height: widget.height,
        child: ElevatedButton(
          onPressed: widget.enabled ? widget.onRepeat : null,
          style: ElevatedButton.styleFrom(
            backgroundColor: _isPressed && widget.enabled
                ? Theme.of(context).colorScheme.primaryContainer
                : null,
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
          ),
          child: widget.icon != null
              ? Icon(widget.icon)
              : Text(widget.text),
        ),
      ),
    );
  }
}
