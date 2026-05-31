# rope-audioengine — C# bindings (Unity & Godot)

Idiomatic C# bindings over the rope-audioengine [C ABI](../../include/rope/rope.h).
The same managed assembly works in **Unity**, **Godot .NET (C#)**, and plain
.NET apps — only the native library placement differs per host.

```
bindings/csharp/
  RopeAudio/                 # the binding library (netstandard2.1)
    Native.cs                #   raw P/Invoke (DllImport) — structs, enums, entry points
    RopeEngine.cs            #   idiomatic IDisposable wrapper
    RopeAudio.csproj
  RopeAudio.Smoke/           # headless smoke test (net8.0, Null backend)
    Program.cs
    RopeAudio.Smoke.csproj
  examples/                  # copy/paste integration snippets (not compiled here)
    UnityRopeAudio.cs        #   MonoBehaviour: poll events in Update, lifecycle
    GodotRopeAudio.cs        #   Node: poll events in _Process, lifecycle
```

> Status: bindings are written against ABI `0.4` and mirror the C structs/enums
> exactly (Cdecl, UTF-8 paths, `size_t`→`UIntPtr`, reserved struct fields,
> `__Internal` on iOS). Verify with the headless smoke on a machine with the
> .NET SDK; the Unity/Godot integration is validated on those engines.

## 1. Build the native library

The bindings load `rope_audio` (`rope_audio.dll` / `librope_audio.so` /
`librope_audio.dylib`). Build it as a shared library from the repo root:

```bash
cmake -S . -B build -DROPE_AUDIO_BUILD_SHARED=ON
cmake --build build --config Release --target rope_audio_shared
```

## 2. Run the headless smoke test

The smoke drives the real library through the **Null backend** + offline
rendering (no audio device), generates a deterministic WAV in memory, plays it,
and asserts the rendered output and the fade-out VoiceFinished event.

```bash
# Point the smoke project at the freshly built native library, then run it:
#   Windows:
set ROPE_AUDIO_NATIVE=C:\path\to\build\bin\Release\rope_audio.dll
#   macOS/Linux:
export ROPE_AUDIO_NATIVE=/path/to/build/librope_audio.dylib   # or .so

dotnet run --project bindings/csharp/RopeAudio.Smoke
```

Expected tail:

```
settled frame: L=0.2000 R=0.0000 (expect L~0.20 R~0.00)
[event] VoiceFinished voice=... reason=Stopped
done
```

## 3. Unity setup

1. Copy `RopeAudio/Native.cs` and `RopeAudio/RopeEngine.cs` into your project
   (e.g. `Assets/RopeAudio/`).
2. Place the native library under `Assets/Plugins/` per platform and set its
   import settings (CPU/OS) in the Inspector:
   - Windows: `Assets/Plugins/x86_64/rope_audio.dll`
   - macOS: `Assets/Plugins/rope_audio.bundle` (or `.dylib`)
   - Android: `Assets/Plugins/Android/libs/arm64-v8a/librope_audio.so`
   - iOS: build the engine as a **static** library; Unity IL2CPP resolves
     `__Internal` (already handled by the `UNITY_IOS` branch in `Native.cs`).
3. Use it from a `MonoBehaviour` (see `examples/UnityRopeAudio.cs`): create the
   engine in `Awake`, `Start(RopeBackend.Default)`, drain `PollEvent` in
   `Update`, and `Dispose` in `OnDestroy`. Load clips via `LoadWavBytes` from a
   `TextAsset`'s `.bytes`.

## 4. Godot .NET (C#) setup

1. Add `RopeAudio/Native.cs` and `RopeAudio/RopeEngine.cs` to your Godot C#
   project (anywhere under the project; Godot compiles all `.cs`).
2. Ship the native library next to the export / in the project so the OS loader
   finds it (`rope_audio.dll` beside the executable on Windows, `librope_audio.so`
   on Linux, `librope_audio.dylib` on macOS).
3. Use it from a `Node` (see `examples/GodotRopeAudio.cs`): create + `Start` in
   `_Ready`, drain `PollEvent` in `_Process`, `Dispose` in `_ExitTree`. Load
   bytes with `FileAccess.GetFileAsBytes("res://sfx.wav")`.

## API at a glance

```csharp
using Rope;

using var engine = new RopeEngine();
engine.Start();                                  // default device/backend
uint sfx = engine.LoadWavBytes(File.ReadAllBytes("shot.wav"));

engine.MasterVolume = 0.8f;
ulong v = engine.Play(sfx, gain: 0.7f, pan: -0.5f, fadeIn: 0.05f);
engine.SetVoicePan(v, 0.3f);                     // smoothed (anti-zipper)
engine.StopVoice(v, fadeOut: 0.1f);              // fade then VoiceFinished

while (engine.PollEvent(out var ev))             // drain once per frame
    if (ev.Type == RopeEventType.VoiceFinished) { /* ... */ }
```

Threading: create and call from one thread (the game/main thread). The audio
runs on its own native real-time thread and never calls back into managed code,
so there are no GC/marshalling callbacks to worry about — just poll events.
