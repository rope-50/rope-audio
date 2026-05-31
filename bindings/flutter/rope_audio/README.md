# rope_audio

Low-latency game audio for **Dart & Flutter/Flame**, powered by the native
[rope-audioengine](../../..) through its stable C ABI (`dart:ffi`).

Built for games: fire many simultaneous one-shot SFX with per-voice gain/pan,
loop a music bed, control a master volume, and receive engine events
(voice-finished, etc.) by polling once per frame. The audio runs entirely on a
native real-time thread (no Dart GC in the hot path) and **never calls back into
Dart** — you drain events in your game loop.

> Status: v0.1. Windows desktop is verified end-to-end. The native core is
> cross-platform (miniaudio: Android/iOS/macOS/Linux) — those platforms reuse
> the same code and only need their plugin build glue (a mechanical follow-up).

## Why rope vs the alternatives

* vs `audioplayers`/`flame_audio`: those target media playback (higher latency);
  rope is built for low-latency, high-polyphony game SFX.
* The poll-based event model maps directly onto Flame's `update(dt)` loop and
  sidesteps the hardest part of Dart FFI (calling Dart from a native thread).

## Usage (pure Dart API)

```dart
import 'package:rope_audio/rope_audio.dart';

final engine = RopeEngine();          // opens the bundled native library
engine.start();                       // default device, 48 kHz

final sfx = engine.loadBytes(bytes);  // or engine.loadFile('path.wav')
engine.masterVolume = 0.8;
final voice = engine.play(sfx, gain: 0.7, pan: -0.5);   // pan: -1..+1

// each frame:
for (final e in engine.pollEvents()) {
  if (e.type == RopeEventType.voiceFinished) { /* ... */ }
}

engine.stopVoice(voice);
engine.dispose();
```

## Usage with Flutter / Flame

```dart
import 'package:rope_audio/rope_flutter.dart';

final audio = RopeAudio()..start()..enableLifecycleHandling();

// Load a WAV bundled as a Flutter asset (declared under `assets:` in pubspec):
final sfx = await audio.loadAsset('assets/jump.wav');

// In a Flame game, pan by an object's screen position:
final pan = RopeAudio.panForX(component.x, size.x);
audio.engine.play(sfx, gain: 0.7, pan: pan);

// Drain events in update(dt):
for (final e in audio.engine.pollEvents()) { /* ... */ }
```

See [`example/`](example/) for a complete Flame demo (looping music,
tap-to-play panned SFX, master-volume slider, event handling). Run it with:

```bash
cd example
flutter run -d windows
```

## API summary

`RopeEngine` (core, no Flutter dependency):
`start` · `stop` · `loadFile` · `loadBytes` · `unloadSound` · `play(gain, pan,
loop)` · `stopVoice` · `stopAll` · `setVoiceGain` · `setVoicePan` ·
`masterVolume` · `pollEvents` · `suspend` · `resume`.

`RopeAudio` (Flutter glue): `loadAsset` (rootBundle bytes), `panForX`,
`enableLifecycleHandling` (auto suspend/resume), plus the underlying `engine`.

## How the native library is provided

This is a Flutter **FFI plugin**. On Windows, the plugin's `windows/CMakeLists.txt`
compiles the engine core (miniaudio-only) into `rope_audio.dll` and Flutter
bundles it next to your app; `dart:ffi` opens it by name. For standalone Dart,
build the shared library from the repo
(`-DROPE_AUDIO_BUILD_SHARED=ON`) and pass its path:
`RopeEngine(libraryPath: 'path/to/rope_audio.dll')`.

## License

Same as the parent rope-audioengine repository.
