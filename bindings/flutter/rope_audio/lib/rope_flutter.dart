/// Flutter integration helpers for rope_audio: load sounds from bundled assets,
/// hook app lifecycle to suspend/resume, and map positions to stereo pan.
///
/// The core [RopeEngine] (rope_audio.dart) stays Flutter-free; this layer adds
/// the Flutter glue. Re-exports the core API so a single import is enough.
library;

import 'dart:ffi';

import 'package:flutter/services.dart' show rootBundle;
import 'package:flutter/widgets.dart';

import 'rope_audio.dart';

export 'rope_audio.dart';

/// A thin Flutter-friendly wrapper around [RopeEngine].
class RopeAudio {
  RopeAudio({DynamicLibrary? library, String? libraryPath})
      : engine = RopeEngine(library: library, libraryPath: libraryPath);

  /// The underlying engine — use it directly for play/stop/mix control.
  final RopeEngine engine;

  AppLifecycleListener? _lifecycle;

  /// Open the output device and start streaming. Returns true on success.
  bool start({int sampleRate = 0, int bufferFrames = 0}) =>
      engine.start(sampleRate: sampleRate, bufferFrames: bufferFrames);

  /// Load a WAV bundled as a Flutter asset (declared in pubspec `assets:`).
  /// Flutter assets are not real files, so we read their bytes and decode
  /// them in-memory. Returns a sound handle or [kInvalidSound].
  Future<int> loadAsset(String key) async {
    final data = await rootBundle.load(key);
    return engine
        .loadBytes(data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes));
  }

  /// Automatically suspend the device when the app is backgrounded/hidden and
  /// resume when it returns to the foreground (audio focus on mobile).
  void enableLifecycleHandling() {
    _lifecycle ??= AppLifecycleListener(
      onStateChange: (state) {
        if (state == AppLifecycleState.resumed) {
          engine.resume();
        } else if (state == AppLifecycleState.paused ||
            state == AppLifecycleState.hidden) {
          engine.suspend();
        }
      },
    );
  }

  /// Map an x coordinate within [width] to a stereo pan in [-1, 1]
  /// (left edge = -1, center = 0, right edge = +1). Handy for 2D positional
  /// audio in a Flame world.
  static double panForX(double x, double width) {
    if (width <= 0) return 0;
    return (x / width).clamp(0.0, 1.0) * 2 - 1;
  }

  void dispose() {
    _lifecycle?.dispose();
    _lifecycle = null;
    engine.dispose();
  }
}
