# Contributing to rope-audioengine

Thanks for your interest! This is a real-time C++ audio engine with a stable C
ABI and Flutter/Flame + Unity/Godot bindings. The bar is **correctness and
real-time safety**, backed by tests and green CI.

## Ground rules

- **The audio thread is sacred.** Code reached from the render callback
  (`Impl::mix`, `drainCommands`, anything they call) must not allocate, lock,
  block, or call into user code or non-RT-safe library functions (no `sin`/`cos`/
  `exp` in the callback — precompute on the control thread, as the pan law,
  filter coefficient and sinc table do).
- **Control/audio communication is lock-free.** Cross-thread messages go through
  the SPSC queues; cross-thread scalars are `std::atomic`. The control API is
  serialized by `controlMutex`, which the audio thread never takes.
- **The C ABI is append-only.** See [docs/ABI_POLICY.md](docs/ABI_POLICY.md).
  New POD struct fields consume a `_reserved` slot (same size); otherwise add a
  new function. Bump `ROPE_ABI_VERSION_MINOR` for additive changes.
- **Bindings mirror the ABI.** A change to `include/rope/rope.h` must be
  reflected in the Dart (`bindings/flutter`) and C# (`bindings/csharp`) bindings,
  and ideally exercised by a test/smoke.

## Building & testing

See the [README](README.md#building--running-windows-visual-studio-2022) for the
full setup. In short:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Tests run headlessly via the **Null backend** + `renderOffline()` — no audio
device needed. Add a deterministic test for any behavioral change; prefer
asserting rendered output over timing.

## Pull requests

1. Branch from `main`; keep PRs focused.
2. Code, comments and commit messages in **English**.
3. Add/adjust tests; make sure `ctest`, `dart analyze`/`flutter test`, and the
   C# smoke pass. CI runs all of these on Windows/Linux/macOS — it must be green.
4. Don't commit generated build trees (`build*/`, `stage/`, `bin/`, `obj/`).
5. Describe the change and how you verified it (the PR template prompts for this).

## Style

- C++20. Match the surrounding style (4-space indent, braces on the same line).
- Keep public headers free of backend/implementation details (the pimpl + C ABI
  boundary). Document non-obvious real-time constraints in comments.

## Reporting bugs / requesting features

Open an issue using the templates. For audio glitches, include sample rate,
buffer size, backend, platform, and (if possible) a minimal repro via the Null
backend + `renderOffline`.
