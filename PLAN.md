# Plan to 1.0

Objective: a solid, cross-platform, real-time **game audio engine** — a lock-free
C++20 mixer behind a stable C ABI, with first-class bindings for **Flutter/Flame**
and **Unity/Godot**, shipped as installable packages with prebuilt binaries and CI
across every target platform.

This file tracks **what remains**. For the already-shipped feature list see the
[README roadmap](README.md#roadmap). Checked boxes here are remaining items that
have since landed; unchecked boxes are still open.

---

## 1. Platform reach — native plugin glue (deferred to the macOS M4 machine)

The core builds and is verified on Windows desktop (miniaudio/WASAPI). The
remaining work is the per-platform plugin packaging + on-device verification.

- [ ] **Android**: Flutter plugin Gradle/CMake glue; build `librope_audio.so`
      for `arm64-v8a`/`armeabi-v7a`/`x86_64`; verify on a device/emulator (AAudio/OpenSL).
- [ ] **iOS**: CocoaPods `.podspec` compiling the core as a static lib; link
      CoreAudio/AudioToolbox; verify `__Internal` symbol resolution on device.
- [ ] **macOS**: Flutter macOS plugin + `.podspec`; verify on the M4 (CoreAudio).
- [ ] **Linux**: Flutter Linux plugin CMake; verify (ALSA/PulseAudio).
- [ ] Audio-focus / interruption handling per OS (phone call, route change).
- [ ] CI: add Android (NDK cross-compile) and iOS/macOS build jobs.

## 2. Godot — second binding half (GDScript / GDExtension)

C# bindings (Unity + Godot .NET) are done and CI-verified. The remaining piece
serves pure-GDScript users.

- [ ] **GDExtension** wrapper over the C ABI using `godot-cpp` (a `RopeAudio` Node).
- [ ] `.gdextension` descriptor + prebuilt libs per platform.
- [ ] Minimal Godot example scene (positional SFX + music + event signals).
- [ ] CI: build the GDExtension against `godot-cpp`.

## 3. DSP / engine features (verifiable here via the Null backend + tests)

- [ ] **Higher-quality resampling** (windowed-sinc) as an opt-in alternative to
      the current linear interpolator; A/B test vs linear.
- [ ] **Sample-accurate scheduling** — start a voice at an absolute frame time
      (tight musical/looping sync).
- [ ] **Category buses** (SFX / Music / UI) with per-group volume + mute/solo.
- [ ] **Device-changed / auto-reroute** events (miniaudio device-notification
      plumbing) + host re-query of rate/channels.
- [ ] **OGG/FLAC/MP3 decoding** (dr_libs / stb_vorbis) beyond WAV.
- [ ] Optional per-voice low-pass/one-pole filter for distance attenuation.

## 4. Distribution & packaging (turn it into something installable)

- [ ] **Flutter plugin → pub.dev**: prebuilt binaries per platform, versioned
      release, pub score/metadata, screenshots/GIF.
- [ ] **C# → NuGet** (`RopeAudio`) with native runtimes packed per RID
      (`win-x64`, `linux-x64`, `osx-arm64`, …) + a Unity UPM package variant.
- [ ] **GitHub Releases**: tagged versions with prebuilt shared libraries and
      headers (`rope.h`) attached as assets.
- [ ] CMake `install()` + a CMake package config (`find_package(rope)`) and/or
      a `FetchContent`-friendly entry for C++ consumers.
- [ ] Semantic-versioned ABI policy doc (when MAJOR/MINOR bump; the
      `rope_abi_version()` contract).
- [ ] **Standalone `*-starter` template repos** (post-publish): mirror the key
      `examples/` into independent "Use this template" repos that depend on the
      published package — see item 6.

## 5. Quality, docs & polish

- [ ] **Benchmarks**: mix throughput / per-callback CPU at N voices; latency
      numbers per backend.
- [ ] **Stress/soak test**: many concurrent plays + load/unload churn under the
      Null backend (catch leaks / queue overflows / reclamation bugs).
- [ ] **API reference docs** (Doxygen for C/C++, dartdoc, XML docs for C#).
- [ ] A short **demo video / GIF** of the Flame example for the README and pub.
- [ ] `LICENSE` confirmed + third-party attributions (dr_wav, miniaudio, RtAudio).
- [ ] `CONTRIBUTING.md` + issue/PR templates.

---

### Suggested order

1. **(3)** engine features — fully verifiable here, raises the core's value now.
2. **(2)** Godot GDExtension — completes the binding story (CI-verifiable build).
3. **(1)** mobile/desktop plugin glue — on the macOS M4 (needs devices/toolchains).
4. **(4)/(5)** packaging, releases, docs — once the surface is stable.

---

## 6. Minimal example projects (in-repo, `examples/`)

A small, copy-and-run example per platform/binding, all kept **in this repo**
under `examples/<platform>/`. They build against the local checkout and are
compiled by CI, so they stay in lockstep with the core and the ABI (a binding
change updates its example in the same commit). Each stays deliberately minimal:
init engine → load one asset → play with pan/gain → drain events → clean shutdown.

- [ ] **`examples/flutter/`** — minimal Flutter/Flame game: tap to play a panned
      SFX over a looping music bed + master-volume slider. (The plugin already
      ships a Flame example; promote/trim it here as the canonical one.)
- [ ] **`examples/unity/`** — minimal Unity project: one scene, a `MonoBehaviour`
      that plays positional SFX on click, native lib under `Plugins/`.
- [ ] **`examples/godot-csharp/`** — minimal Godot .NET project using the C#
      bindings (Node polling events in `_Process`).
- [ ] **`examples/godot-gdscript/`** — minimal Godot project using the
      GDExtension (pure GDScript; depends on item 2).
- [ ] **`examples/cpp/`** — minimal C++ consumer (`play_wav` already covers this;
      keep a stripped "hello voice" variant).
- [ ] **`examples/c/`** — minimal C program linking the shared lib + `rope.h`
      (`rope_c_smoke.c` already covers this; keep as the lowest-level reference).

Shared conventions:
- [ ] Each has a README with build → run in a few commands.
- [ ] Reuse the generated test assets (`tools/gen_test_wavs.ps1`).
- [ ] CI builds each example against the local core (consumer-side smoke).
- [ ] Linked from this repo's README as official examples.

> **Standalone `*-starter` template repos are deferred to release time** — see
> item 4. Once rope is published (pub.dev / NuGet / tagged binaries), mirror the
> 1–2 most useful examples (Flutter, Unity) into independent
> "Use this template" repos that depend on the *published* package, ideally
> generated from `examples/` to avoid duplicate maintenance.
