/// Idiomatic Dart API over the native rope-audioengine C ABI.
///
/// Pure Dart (no Flutter dependency) so it runs under `dart` as well as inside
/// a Flutter app. A Flame bridge lives in `flame_rope.dart`.
library;

import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'src/rope_ffi.dart';

/// Which native backend to open (mirrors `rope_backend`).
enum RopeBackend { defaultBackend, miniaudio, rtaudio, rtaudioAsio }

/// Category bus a voice routes through (mirrors `rope_bus`).
enum RopeBus { sfx, music, ui }

/// Why a voice stopped (mirrors `rope_voice_end_reason`).
enum RopeVoiceEndReason { natural, stopped, stolen }

/// Engine -> app notification kind (mirrors `rope_event_type`).
enum RopeEventType {
  none,
  voiceFinished,
  voicesExhausted,
  queueOverflow,
  suspended,
  resumed,
  deviceChanged,
}

/// A single engine event drained via [RopeEngine.pollEvents].
class RopeEvent {
  const RopeEvent({
    required this.type,
    required this.voice,
    required this.reason,
    required this.count,
  });

  final RopeEventType type;
  final int voice; // valid for voiceFinished
  final RopeVoiceEndReason reason;
  final int count; // dropped count for queueOverflow

  @override
  String toString() =>
      'RopeEvent($type, voice=$voice, reason=$reason, count=$count)';
}

/// Invalid handles (mirror the C sentinels).
const int kInvalidSound = 0xFFFFFFFF;
const int kInvalidVoice = 0;

DynamicLibrary _openDefault() {
  if (Platform.isWindows) return DynamicLibrary.open('rope_audio.dll');
  if (Platform.isMacOS || Platform.isIOS) {
    // On iOS the symbols are statically linked into the process.
    try {
      return DynamicLibrary.open('rope_audio.framework/rope_audio');
    } catch (_) {
      return DynamicLibrary.process();
    }
  }
  return DynamicLibrary.open('librope_audio.so');
}

/// A handle to the native audio engine.
///
/// Threading: create and call from a single thread (typically the app/main
/// isolate). The audio runs on its own native real-time thread and never calls
/// back into Dart — drain events with [pollEvents] each frame.
class RopeEngine {
  RopeEngine._(this._b, this._engine);

  /// Open the native library and create an engine.
  ///
  /// Pass [library] or [libraryPath] to point at a specific build of the
  /// shared library; otherwise the platform default name is used.
  factory RopeEngine({DynamicLibrary? library, String? libraryPath}) {
    final dl = library ??
        (libraryPath != null ? DynamicLibrary.open(libraryPath) : _openDefault());
    final b = RopeBindings(dl);
    final engine = b.engineCreate();
    if (engine == nullptr) {
      throw StateError('rope: engine creation failed');
    }
    return RopeEngine._(b, engine);
  }

  final RopeBindings _b;
  final Pointer<RopeEngineHandle> _engine;
  bool _disposed = false;

  /// ABI version as (major << 16) | minor.
  int get abiVersion => _b.abiVersion();

  /// Open the default output device and start streaming. Returns true on success.
  bool start({
    int sampleRate = 0,
    int bufferFrames = 0,
    RopeBackend backend = RopeBackend.defaultBackend,
  }) {
    final cfg = calloc<RopeConfigNative>();
    try {
      cfg.ref.sampleRate = sampleRate;
      cfg.ref.bufferFrames = bufferFrames;
      cfg.ref.channels = 0;
      cfg.ref.backend = backend.index;
      return _b.engineStart(_engine, cfg) == 0; // ROPE_OK
    } finally {
      calloc.free(cfg);
    }
  }

  void stop() => _b.engineStop(_engine);
  bool get isRunning => _b.engineIsRunning(_engine) != 0;
  int get sampleRate => _b.engineSampleRate(_engine);
  int get channels => _b.engineChannels(_engine);

  /// The engine's monotonic output-frame clock (frames produced since start).
  /// Pass `currentFrame + n` as [play]'s `startFrame` to cue sample-accurately.
  int get currentFrame => _b.currentFrame(_engine);

  /// Decode an audio file (WAV/FLAC/MP3/OGG, auto-detected) into the sound bank.
  /// Returns a sound handle or [kInvalidSound] on failure.
  int loadFile(String path) {
    final p = path.toNativeUtf8();
    try {
      return _b.loadWavFile(_engine, p);
    } finally {
      calloc.free(p);
    }
  }

  /// Decode audio from an in-memory buffer (e.g. a bundled asset's bytes).
  /// WAV/FLAC/MP3/OGG are auto-detected from the data.
  int loadBytes(Uint8List bytes) {
    if (bytes.isEmpty) return kInvalidSound;
    final ptr = calloc<Uint8>(bytes.length);
    try {
      ptr.asTypedList(bytes.length).setAll(0, bytes);
      return _b.loadWavMemory(_engine, ptr.cast<Void>(), bytes.length);
    } finally {
      calloc.free(ptr);
    }
  }

  /// Retire a sound; new plays fail and the buffer frees on [stop].
  bool unloadSound(int sound) => _b.unloadSound(_engine, sound) == 0;

  /// Start a voice. Returns a voice handle or [kInvalidVoice].
  /// [pitch] is a speed/pitch ratio (1 = original, 2 = +1 octave, 0.5 = -1).
  int play(int sound,
      {double gain = 1.0,
      double pan = 0.0,
      bool loop = false,
      double pitch = 1.0,
      double fadeIn = 0.0,
      RopeBus bus = RopeBus.sfx,
      int startFrame = 0}) {
    final pp = calloc<RopePlayParamsNative>();
    try {
      pp.ref.gain = gain;
      pp.ref.pan = pan;
      pp.ref.loop = loop ? 1 : 0;
      pp.ref.pitch = pitch;
      pp.ref.fadeIn = fadeIn;
      pp.ref.bus = bus.index;
      return startFrame > 0
          ? _b.playScheduled(_engine, sound, pp, startFrame)
          : _b.play(_engine, sound, pp);
    } finally {
      calloc.free(pp);
    }
  }

  /// Stop a voice, optionally fading out over [fadeOut] seconds.
  void stopVoice(int voice, {double fadeOut = 0.0}) {
    if (fadeOut > 0.0) {
      _b.stopVoiceFade(_engine, voice, fadeOut);
    } else {
      _b.stopVoice(_engine, voice);
    }
  }

  void stopAll() => _b.stopAll(_engine);

  void setVoiceGain(int voice, double gain) =>
      _b.setVoiceGain(_engine, voice, gain);
  void setVoicePan(int voice, double pan) =>
      _b.setVoicePan(_engine, voice, pan);
  void setVoicePitch(int voice, double pitch) =>
      _b.setVoicePitch(_engine, voice, pitch);

  set masterVolume(double gain) => _b.setMasterVolume(_engine, gain);
  double get masterVolume => _b.getMasterVolume(_engine);

  /// Set a category bus (SFX/Music/UI) group volume; smoothed (~5 ms).
  void setBusVolume(RopeBus bus, double gain) =>
      _b.setBusVolume(_engine, bus.index, gain);
  double busVolume(RopeBus bus) => _b.getBusVolume(_engine, bus.index);

  /// Master-bus soft-clip limiter (on by default).
  set masterLimiterEnabled(bool enabled) =>
      _b.setMasterLimiter(_engine, enabled ? 1 : 0);
  bool get masterLimiterEnabled => _b.getMasterLimiter(_engine) != 0;

  void suspend() => _b.engineSuspend(_engine);
  void resume() => _b.engineResume(_engine);

  /// Drain all pending engine events (call once per frame).
  Iterable<RopeEvent> pollEvents() sync* {
    final ev = calloc<RopeEventNative>();
    try {
      while (_b.pollEvent(_engine, ev) == 1) {
        yield RopeEvent(
          type: RopeEventType.values[ev.ref.type],
          voice: ev.ref.voice,
          reason: RopeVoiceEndReason.values[ev.ref.reason],
          count: ev.ref.data,
        );
      }
    } finally {
      calloc.free(ev);
    }
  }

  /// Stop and destroy the engine. The instance is unusable afterwards.
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _b.engineStop(_engine);
    _b.engineDestroy(_engine);
  }
}
