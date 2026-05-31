// Idiomatic C# wrapper over the rope-audioengine C ABI.
//
// Works in any .NET host that can P/Invoke the native library: a Unity project
// (drop these .cs files in Assets/ and the native lib under Assets/Plugins/),
// a Godot .NET (C#) project, or a plain console app. The audio runs on its own
// native real-time thread and never calls back into managed code — drain events
// with PollEvent() each frame.

using System;
using System.Runtime.InteropServices;

namespace Rope
{
    /// <summary>Invalid handle sentinels (mirror the C ABI).</summary>
    public static class RopeHandle
    {
        public const uint InvalidSound = 0xFFFFFFFFu;
        public const ulong InvalidVoice = 0ul;
    }

    /// <summary>A handle to the native audio engine. Dispose to stop and free it.</summary>
    public sealed class RopeEngine : IDisposable
    {
        private IntPtr _engine;

        public RopeEngine()
        {
            _engine = Native.rope_engine_create();
            if (_engine == IntPtr.Zero)
                throw new InvalidOperationException("rope: engine creation failed");
        }

        /// <summary>ABI version as (major &lt;&lt; 16) | minor.</summary>
        public static uint AbiVersion => Native.rope_abi_version();

        /// <summary>Human-readable name for a result code.</summary>
        public static string ResultString(RopeResult r) =>
            Marshal.PtrToStringUTF8(Native.rope_result_str(r)) ?? "unknown";

        /// <summary>Open the default output device and start streaming.</summary>
        /// <returns>true on success.</returns>
        public bool Start(uint sampleRate = 0, uint bufferFrames = 0,
                          RopeBackend backend = RopeBackend.Default)
        {
            Native.rope_config_default(out var cfg);
            cfg.SampleRate = sampleRate;
            cfg.BufferFrames = bufferFrames;
            cfg.Backend = backend;
            return Native.rope_engine_start(_engine, in cfg) == RopeResult.Ok;
        }

        public void Stop() => Native.rope_engine_stop(_engine);
        public bool IsRunning => Native.rope_engine_is_running(_engine) != 0;
        public uint SampleRate => Native.rope_engine_sample_rate(_engine);
        public uint Channels => Native.rope_engine_channels(_engine);

        /// <summary>Decode an audio file (WAV/FLAC/MP3/OGG, auto-detected) into the sound bank.</summary>
        /// <returns>A sound handle, or <see cref="RopeHandle.InvalidSound"/> on failure.</returns>
        public uint LoadWavFile(string path) => Native.rope_load_wav_file(_engine, path);

        /// <summary>Decode audio from an in-memory buffer (WAV/FLAC/MP3/OGG, auto-detected).</summary>
        public uint LoadWavBytes(byte[] data)
        {
            if (data == null || data.Length == 0) return RopeHandle.InvalidSound;
            var handle = GCHandle.Alloc(data, GCHandleType.Pinned);
            try
            {
                return Native.rope_load_wav_memory(
                    _engine, handle.AddrOfPinnedObject(), (UIntPtr)(uint)data.Length);
            }
            finally { handle.Free(); }
        }

        /// <summary>Retire a sound; new plays fail and the buffer frees on <see cref="Stop"/>.</summary>
        public bool UnloadSound(uint sound) =>
            Native.rope_unload_sound(_engine, sound) == RopeResult.Ok;

        /// <summary>Start a voice. <paramref name="pitch"/> is a speed/pitch ratio
        /// (1 = original, 2 = +1 octave, 0.5 = -1 octave). <paramref name="fadeIn"/>
        /// fades gain up over the given seconds.</summary>
        /// <returns>A voice handle, or <see cref="RopeHandle.InvalidVoice"/>.</returns>
        public ulong Play(uint sound, float gain = 1f, float pan = 0f, bool loop = false,
                          float pitch = 1f, float fadeIn = 0f, RopeBus bus = RopeBus.Sfx)
        {
            Native.rope_play_params_default(out var p);
            p.Gain = gain;
            p.Pan = pan;
            p.Loop = loop ? 1 : 0;
            p.Pitch = pitch;
            p.FadeIn = fadeIn;
            p.Bus = bus;
            return Native.rope_play(_engine, sound, in p);
        }

        /// <summary>Stop a voice, optionally fading out over <paramref name="fadeOut"/> seconds.</summary>
        public void StopVoice(ulong voice, float fadeOut = 0f)
        {
            if (fadeOut > 0f) Native.rope_stop_voice_fade(_engine, voice, fadeOut);
            else Native.rope_stop_voice(_engine, voice);
        }

        public void StopAll() => Native.rope_stop_all(_engine);

        public void SetVoiceGain(ulong voice, float gain) =>
            Native.rope_set_voice_gain(_engine, voice, gain);
        public void SetVoicePan(ulong voice, float pan) =>
            Native.rope_set_voice_pan(_engine, voice, pan);
        public void SetVoicePitch(ulong voice, float pitch) =>
            Native.rope_set_voice_pitch(_engine, voice, pitch);

        public float MasterVolume
        {
            get => Native.rope_get_master_volume(_engine);
            set => Native.rope_set_master_volume(_engine, value);
        }

        /// <summary>Set a category bus (SFX/Music/UI) group volume; smoothed (~5 ms).</summary>
        public void SetBusVolume(RopeBus bus, float gain) =>
            Native.rope_set_bus_volume(_engine, bus, gain);
        public float BusVolume(RopeBus bus) => Native.rope_get_bus_volume(_engine, bus);

        /// <summary>Master-bus soft-clip limiter (on by default).</summary>
        public bool MasterLimiterEnabled
        {
            get => Native.rope_get_master_limiter(_engine) != 0;
            set => Native.rope_set_master_limiter(_engine, value ? 1 : 0);
        }

        public void Suspend() => Native.rope_engine_suspend(_engine);
        public void Resume() => Native.rope_engine_resume(_engine);

        /// <summary>Pull the next pending engine event. Call in a loop each frame.</summary>
        /// <returns>true if <paramref name="ev"/> was filled, false when the queue is empty.</returns>
        public bool PollEvent(out RopeEvent ev) => Native.rope_poll_event(_engine, out ev) != 0;

        /// <summary>Headless offline rendering for tests/tools (use with
        /// <see cref="RopeBackend.Null"/>). Writes interleaved float frames.</summary>
        public void RenderOffline(float[] outBuffer, uint frames)
        {
            if (outBuffer == null) return;
            var handle = GCHandle.Alloc(outBuffer, GCHandleType.Pinned);
            try { Native.rope_render_offline(_engine, handle.AddrOfPinnedObject(), frames); }
            finally { handle.Free(); }
        }

        public void Dispose()
        {
            if (_engine != IntPtr.Zero)
            {
                Native.rope_engine_destroy(_engine); // ~AudioEngine stops the device
                _engine = IntPtr.Zero;
            }
        }
    }
}
