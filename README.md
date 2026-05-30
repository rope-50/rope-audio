# rope-audioengine

A modern C++ (C++20) real-time audio engine for **games and DAWs**. It opens a
single output stream and mixes any number of simultaneously-playing mono/stereo
WAV voices on the audio thread.

Built on [RtAudio](https://github.com/thestk/rtaudio) for cross-platform
real-time I/O (WASAPI/DirectSound on Windows, CoreAudio on macOS, ALSA/Pulse/JACK
on Linux) and [dr_wav](https://github.com/mackron/dr_libs) for WAV decoding.

> Status: **v0.1 boilerplate** — the foundation is in place (CMake + RtAudio,
> WAV loading, lock-free voice mixing). See the roadmap below for what's next.

## Layout

```
include/rope/        Public headers
  AudioBuffer.hpp      Decoded asset (interleaved float samples)
  WavLoader.hpp        decodeWav() — file -> AudioBuffer
  AudioEngine.hpp      The engine: start/stop, loadWav, play, stopVoice
src/                 Implementation
  WavLoader.cpp        dr_wav-backed decoder
  AudioEngine.cpp      RtAudio stream, lock-free command queue, voice mixer
examples/
  play_wav.cpp         Loads WAVs from argv and plays them all at once
cmake/
  Dependencies.cmake   FetchContent for RtAudio + dr_wav
```

## Design

* **Real-time safe audio thread.** The audio callback never locks or allocates.
  The control thread hands work to it through a single-producer/single-consumer
  lock-free queue (`play`/`stop` become commands drained at the top of each
  callback).
* **Stable sound bank.** Decoded buffers live in the engine (`unique_ptr` for
  stable addresses); voices reference them by raw pointer, so no `shared_ptr`
  refcounting or deallocation ever happens on the audio thread.
* **pimpl public API.** `RtAudio.h` and the mixer stay out of the headers.

## Building (Windows, Visual Studio 2022)

Requires CMake ≥ 3.24, a C++20 compiler, and internet access on first configure
(RtAudio and dr_wav are fetched from source).

```powershell
cmake --preset windows-vs2022
cmake --build --preset windows-vs2022
```

The `play_wav` demo ends up in `build/vs2022/bin/Release/`:

```powershell
./build/vs2022/bin/Release/play_wav.exe kick.wav snare.wav hat.wav
```

## Roadmap

- [ ] Sample-rate conversion (resample sources to the device rate)
- [ ] Master limiter / soft-clip on the mix bus
- [ ] Per-voice pan, pitch, fade in/out
- [ ] Voice-finished notifications back to the control thread
- [ ] Streaming playback for large files (no full decode into RAM)
- [ ] OGG/FLAC/MP3 decoding (dr_flac / dr_mp3 / stb_vorbis)
- [ ] Buses/sends and a simple effect graph
- [ ] Unit tests + CI
```
