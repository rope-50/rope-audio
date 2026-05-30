# rope-audioengine

A modern C++ (C++20) real-time audio engine for **games and DAWs**. It opens a
single output stream and mixes any number of simultaneously-playing mono/stereo
WAV voices on the audio thread.

Cross-platform by design through a small **audio backend** abstraction:

| Backend       | Platforms                                              | Notes                          |
|---------------|--------------------------------------------------------|--------------------------------|
| **miniaudio** (default) | Windows, macOS, Linux, **Android, iOS** | WASAPI/CoreAudio/ALSA/AAudio…  |
| **RtAudio**   | Windows, macOS, Linux                                  | Adds **ASIO** on Windows (DAW) |

WAV decoding uses [dr_wav](https://github.com/mackron/dr_libs).

> Status: **v0.1** — boilerplate + backend abstraction. CMake build, WAV
> loading, lock-free voice mixing, and verified simultaneous playback on
> Windows (miniaudio/WASAPI). See the roadmap below.

## Layout

```
include/rope/        Public headers
  AudioBuffer.hpp      Decoded asset (interleaved float samples)
  WavLoader.hpp        decodeWav() — file -> AudioBuffer
  AudioBackend.hpp     Backend interface + createAudioBackend() factory
  AudioEngine.hpp      The engine: start/stop, loadWav, play, stopVoice
src/
  WavLoader.cpp        dr_wav-backed decoder
  AudioEngine.cpp      Lock-free command queue + voice mixer (backend-agnostic)
  AudioBackend.cpp     Backend factory
  backends/
    Backends.hpp         Internal per-backend factory declarations
    MiniaudioBackend.cpp miniaudio device backend (default)
    RtAudioBackend.cpp   RtAudio device backend (ASIO on Windows)
examples/
  play_wav.cpp         Loads WAVs from argv and plays them all at once
tools/
  gen_test_wavs.ps1    Generates sine-tone test WAVs under assets/
cmake/
  Dependencies.cmake   FetchContent for dr_wav, miniaudio, RtAudio
```

## Design

* **Backend abstraction.** The mixer is platform-independent; a backend's only
  job is to open the default output device and pull float audio via a render
  callback. Swap backends without touching the engine. Pick one at runtime:
  `engine.start(48000, 512, rope::BackendType::RtAudioAsio)`.
* **Real-time safe audio thread.** The audio callback never locks or allocates.
  `play`/`stop` become commands drained from a single-producer/single-consumer
  lock-free queue at the top of each callback.
* **Stable sound bank.** Decoded buffers live in the engine (`unique_ptr` for
  stable addresses); voices reference them by raw pointer — no `shared_ptr`
  refcounting or deallocation on the audio thread.
* **pimpl public API.** Backend headers (miniaudio.h, RtAudio.h) stay out of the
  public interface.

## Building (Windows, Visual Studio 2022)

Requires CMake ≥ 3.24, a C++20 compiler, and internet access on first configure
(dr_wav, miniaudio and RtAudio are fetched from source).

```powershell
cmake --preset windows-vs2022
cmake --build --preset windows-vs2022
```

Generate test tones and play two WAVs simultaneously:

```powershell
powershell -ExecutionPolicy Bypass -File tools/gen_test_wavs.ps1
./build/vs2022/bin/Release/play_wav.exe ./assets/tone_a4_mono.wav ./assets/chord_stereo.wav
```

### Notes

* **ASIO** is enabled by default in the RtAudio backend on Windows (RtAudio
  bundles the ASIO SDK). Select it with `BackendType::RtAudioAsio`.
* **Windows / COM:** the miniaudio backend holds a COM reference for the device
  lifetime to keep WASAPI initialization stable (see `MiniaudioBackend.cpp`).
* Running freshly-built unsigned binaries requires **Smart App Control** to be
  off (it blocks them otherwise).

## Roadmap

- [ ] Sample-rate conversion (resample sources to the device rate)
- [ ] Master limiter / soft-clip on the mix bus
- [ ] Per-voice pan, pitch, fade in/out
- [ ] Sample-accurate scheduling (start voices at an absolute frame time)
- [ ] Voice-finished notifications back to the control thread
- [ ] Mobile lifecycle handling (audio focus / interruptions on Android & iOS)
- [ ] OGG/FLAC/MP3 decoding
- [ ] Buses/sends and a simple effect graph
- [ ] Unit tests + CI
```
