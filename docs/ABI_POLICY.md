# C ABI stability policy

The game-facing protocol is the C ABI in [`include/rope/rope.h`](../include/rope/rope.h).
Bindings (Dart FFI, C# P/Invoke, and any future ones) and prebuilt binaries
depend on it staying predictable. This document is the contract.

## Versioning

```c
uint32_t rope_abi_version(void);   /* (MAJOR << 16) | MINOR */
```

- **MINOR** is bumped for **backward-compatible, additive** changes: a new
  function, a new enum value appended at the end, or a new POD field that
  consumes a `_reserved` slot (so `sizeof` is unchanged). Old callers keep
  working without recompiling.
- **MAJOR** is bumped for a **breaking** change: changing a struct's size or
  field order/meaning, changing or removing a function signature, or changing an
  existing enum value. This requires consumers to rebuild.

A consumer can check compatibility at load time, e.g. require the same MAJOR and
a MINOR `>=` the one it was built against.

> **Pre-1.0 caveat:** while the version is `0.x` the ABI is still settling, so a
> MINOR bump *may* occasionally include a breaking change. From `1.0` onward the
> rules above are strict. Pin a known-good commit/release if you need stability
> today.

## Design rules that keep it stable

- **Opaque handle.** `rope_engine_t` is an opaque pointer; its layout is private.
- **POD structs end with `_reserved`.** New fields are appended into the reserved
  space, preserving size and alignment. Every struct has a `*_default()`
  initializer — **always call it**, then set the fields you care about, so
  unknown/reserved fields are zeroed and future-proof.
- **Enums are 32-bit pinned.** Each enum has a `*_FORCE_U32 = 0x7fffffff`
  sentinel so its size is fixed; new values are appended, never renumbered.
- **No C++ across the boundary.** Everything is `extern "C"`, C99-compatible,
  and every shim wraps its body in `try/catch(...)` so a C++ exception can never
  unwind across the FFI edge.
- **Stable names over pretty names.** Functions are not renamed once shipped
  (e.g. `rope_load_wav_*` also decodes FLAC/MP3/OGG — the name stayed for ABI
  stability).

## When adding to the ABI

1. Add the function/field/enum value to `rope.h` (append-only).
2. Implement the shim in `src/rope_c.cpp` (guard args; `try/catch`).
3. Bump `ROPE_ABI_VERSION_MINOR`.
4. Mirror it in the Dart and C# bindings and add a test/smoke.

See [CONTRIBUTING.md](../CONTRIBUTING.md) for the full workflow.
