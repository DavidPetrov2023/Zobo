import 'dart:async';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';

/// Paints a stream of JPEG frames.
///
/// Frames are decoded to a ui.Image and drawn with RawImage instead of going
/// through Image.memory: every frame is a fresh byte array, so Image.memory
/// would push a new entry into the global image cache twenty times a second.
///
/// A frame that arrives while the previous one is still decoding replaces it
/// and the older one is dropped. On a slow phone the picture then loses frame
/// rate rather than falling behind in time, which is what matters when the
/// picture is used for driving.
class LiveImage extends StatefulWidget {
  final Stream<Uint8List> frames;
  final Widget placeholder;

  const LiveImage({
    super.key,
    required this.frames,
    required this.placeholder,
  });

  @override
  State<LiveImage> createState() => _LiveImageState();
}

class _LiveImageState extends State<LiveImage> {
  StreamSubscription<Uint8List>? _subscription;
  ui.Image? _image;
  Uint8List? _pending;
  bool _decoding = false;

  @override
  void initState() {
    super.initState();
    _subscription = widget.frames.listen(_onFrame);
  }

  @override
  void didUpdateWidget(LiveImage oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.frames != widget.frames) {
      _subscription?.cancel();
      _subscription = widget.frames.listen(_onFrame);
    }
  }

  void _onFrame(Uint8List bytes) {
    _pending = bytes;
    if (!_decoding) unawaited(_decodeNext());
  }

  Future<void> _decodeNext() async {
    final bytes = _pending;
    _pending = null;
    if (bytes == null) return;

    _decoding = true;
    try {
      final codec = await ui.instantiateImageCodec(bytes);
      final frame = await codec.getNextFrame();
      codec.dispose();
      if (!mounted) {
        frame.image.dispose();
        return;
      }
      final previous = _image;
      setState(() => _image = frame.image);
      previous?.dispose();
    } catch (_) {
      // A truncated frame is not worth reporting - the next one decodes.
    } finally {
      _decoding = false;
      if (_pending != null && mounted) unawaited(_decodeNext());
    }
  }

  @override
  void dispose() {
    _subscription?.cancel();
    _image?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final image = _image;
    if (image == null) return widget.placeholder;
    return RawImage(
      image: image,
      fit: BoxFit.contain,
      filterQuality: FilterQuality.medium,
    );
  }
}
