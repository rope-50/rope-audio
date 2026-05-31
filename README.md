# rope-audioengine

[![CI](https://github.com/rope-50/rope-audio/actions/workflows/ci.yml/badge.svg)](https://github.com/rope-50/rope-audio/actions/workflows/ci.yml)

A modern C++ (C++20) real-time audio engine for **games**. It opens a single
output stream and mixes any number of simultaneously-playing mono/stereo WAV
voices on a real-time audio thread, with per-voice gain/pan and a poll-based
event channel — exposed to game code through a **stable C ABI** and a
**Flutter/Flame** plugin.

Cross-platform by design through a small **audio backend** abstraction:

| Backend       | Platforms                                              | Notes                          |
|---------------|--------------------------------------------------------|--------------------------------|
| **miniaudio** (default) | Windows, macOS, Linux, **Android, iOS** | WASAPI/CoreAudio/ALSA/AAudio…  |
| **RtAudio**   | Windows, macOS, Linux                                  | Adds **ASIO** on Windows       |

WAV decoding uses [dr_wav](https://github.com/mackron/dr_libs).

> Status: **v0.1** — thread-safe C++ core (lock-free mixer, gain/pan, master
> volume, poll events, file + in-memory WAV loading), a stable C ABI, and a
> Flutter FFI plugin with a Flame example. Covered by a GoogleTest suite (run
> headlessly via a Null backend) and CI across Windows/Linux/macOS. Verified
> end-to-end on Windows (miniaudio/WASAPI), from the C++ engine up through Dart
> FFI and a running Flutter/Flame app.

## Layout

```
include/rope/          Public headers
  AudioBuffer.hpp        Decoded asset (interleaved float samples)
  WavLoader.hpp          decodeWav() — file / memory -> AudioBuffer
  AudioBackend.hpp       Backend interface + createAudioBackend() factory
  AudioEngine.hpp        C++ engine: start/stop, load, play, pan/gain, events
  rope.h                 Stable C ABI (the game-facing protocol)
src/
  AudioEngine.cpp        Lock-free command + event queues, voice mixer
  WavLoader.cpp          dr_wav-backed decoder (file + memory)
  AudioBackend.cpp       Backend factory
  rope_c.cpp             C ABI shim over AudioEngine
  backends/              miniaudio (default) and RtAudio/ASIO device backends
bindings/flutter/
  rope_audio/            Flutter FFI plugin (Dart API + Flame example)
examples/
  play_wav.cpp           C++ demo: panned playback + master + events
  rope_c_smoke.c         C ABI smoke test (file + in-memory load)
tools/gen_test_wavs.ps1  Generates sine-tone test WAVs under assets/
cmake/Dependencies.cmake FetchContent for dr_wav, miniaudio, RtAudio
```

## Design

* **Backend abstraction.** The mixer is platform-independent; a backend's only
  job is to open the default output device and pull float audio via a render
  callback. Pick one at runtime: `engine.start(48000, 512, BackendType::RtAudioAsio)`.
* **Real-time-safe audio thread.** The callback never locks or allocates.
  `play`/`stop`/`set*` become commands drained from a single-producer/
  single-consumer lock-free queue; engine→app events flow back through a second
  lock-free queue and are delivered only by polling (the audio thread never
  calls into your code).
* **Stable sound bank.** Decoded buffers live in the engine (`unique_ptr` for
  stable addresses); voices reference them by raw pointer — no `shared_ptr`
  refcounting or deallocation on the audio thread. `unloadSound` retires a slot
  and frees it on `stop()`.
* **Constant-power pan** + master volume applied in the mixer, computed per
  command (never per sample).
* **pimpl / C ABI.** Backend headers stay out of the public API; the C ABI
  (`rope.h`) is POD-only, FFI-friendly, and ABI-stable (reserved struct fields,
  32-bit-pinned enums, `*_default()` initializers).

## The game protocol (C ABI)

`include/rope/rope.h` is the stable, FFI-friendly surface games bind to:

```c
rope_engine_t e = rope_engine_create();
rope_config cfg; rope_config_default(&cfg);
rope_engine_start(e, &cfg);

rope_sound s = rope_load_wav_memory(e, bytes, size);   // or rope_load_wav_file
rope_play_params p; rope_play_params_default(&p);
p.gain = 0.7f; p.pan = -0.5f;
rope_voice v = rope_play(e, s, &p);
rope_set_master_volume(e, 0.8f);

rope_event ev;                          // drain once per frame
while (rope_poll_event(e, &ev)) { /* VOICE_FINISHED, ... */ }

rope_engine_stop(e);
rope_engine_destroy(e);
```

Threading: the API is thread-safe (control calls are serialized internally), so
you may call from any thread; poll events each frame. The audio thread never
calls back into your code.

## Building & running (Windows, Visual Studio 2022)

Requires CMake ≥ 3.24, a C++20 compiler, and internet on first configure
(dr_wav, miniaudio, RtAudio are fetched from source).

```powershell
cmake --preset windows-vs2022
cmake --build --preset windows-vs2022

powershell -ExecutionPolicy Bypass -File tools/gen_test_wavs.ps1
./build/vs2022/bin/Release/play_wav.exe ./assets/tone_a4_mono.wav ./assets/chord_stereo.wav
./build/vs2022/bin/Release/rope_c_smoke.exe   # C ABI smoke (run from repo root)
```

### Flutter / Flame

The plugin and a Flame demo live in `bindings/flutter/rope_audio`:

```powershell
cd bindings/flutter/rope_audio/example
flutter run -d windows
```

Tap to fire SFX panned by screen position over a looping music bed; a master
slider controls output volume. See the plugin's
[README](bindings/flutter/rope_audio/README.md) for the Dart API.

### Testing & CI

The mixer is tested deterministically and headlessly via a **Null backend**
(`BackendType::Null` + `renderOffline()`), so tests need no audio hardware:

```powershell
cmake --preset windows-vs2022           # configures GoogleTest too
cmake --build --preset windows-vs2022
ctest --test-dir build/vs2022 -C Release --output-on-failure
```

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) runs the C++ build + tests
on **Windows, Linux and macOS**, plus a Flutter job (`dart analyze` +
`flutter test`) on every push and PR.

### Notes

* **ASIO** is enabled by default in the RtAudio backend on Windows. Select it
  with `BackendType::RtAudioAsio`.
* **Windows / COM:** the miniaudio backend holds a COM reference for the device
  lifetime to keep WASAPI initialization stable (see `MiniaudioBackend.cpp`).
* Running freshly-built unsigned binaries requires **Smart App Control** off.

## Roadmap

Done in v0.1:
- [x] Per-voice gain + constant-power pan, master volume
- [x] Voice-finished / exhausted / overflow events (poll channel)
- [x] In-memory WAV loading (bundled/packed game assets)
- [x] Suspend/resume hooks for mobile lifecycle
- [x] Stable C ABI + Flutter FFI plugin + Flame example (Windows-verified)
- [x] Headless offline rendering (Null backend) + GoogleTest suite + CI
- [x] Per-voice resampling (any source rate → device rate) + pitch control
- [x] Live reclamation of unloaded sounds (memory bounded to live data, refcounted)
- [x] Master-bus soft-clip limiter (prevents harsh clipping; toggleable)

Next:
- [ ] Per-voice fades + parameter smoothing (anti-zipper); higher-quality resampler
- [ ] Sample-accurate scheduling (start voices at an absolute frame time)
- [ ] Category buses (SFX/Music/UI) with group volume
- [ ] Mobile/desktop plugin glue beyond Windows (Android/iOS/macOS/Linux)
- [ ] Device-changed / auto-reroute events
- [ ] OGG/FLAC/MP3 decoding
```
