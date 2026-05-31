// Raw P/Invoke bindings for the rope-audioengine C ABI (rope.h).
//
// Struct layouts mirror rope.h exactly (LayoutKind.Sequential with natural
// packing reproduces the C padding, including rope_event's int32 -> uint64 gap),
// the reserved fields are present so the native *_default()/poll memset-on-
// sizeof writes never overrun the managed struct, every entry point uses
// CallingConvention.Cdecl, the path string marshals as UTF-8, and size_t maps
// to UIntPtr (pointer-width). See README for Unity/Godot setup.

using System;
using System.Runtime.InteropServices;

namespace Rope
{
    public enum RopeResult
    {
        Ok = 0,
        ErrUnknown = 1,
        ErrInvalidArgument = 2,
        ErrNotRunning = 3,
        ErrAlreadyRunning = 4,
        ErrBackendUnavailable = 5,
        ErrDecodeFailed = 6,
        ErrQueueFull = 7,
        ErrOutOfSounds = 8,
    }

    public enum RopeBackend
    {
        Default = 0,
        Miniaudio = 1,
        RtAudio = 2,
        RtAudioAsio = 3,
        Null = 4,
    }

    public enum RopeEventType
    {
        None = 0,
        VoiceFinished = 1,
        VoicesExhausted = 2,
        QueueOverflow = 3,
        Suspended = 4,
        Resumed = 5,
        DeviceChanged = 6,
    }

    public enum RopeVoiceEndReason
    {
        Natural = 0,
        Stopped = 1,
        Stolen = 2,
    }

    public enum RopeBus
    {
        Sfx = 0,
        Music = 1,
        Ui = 2,
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct RopeConfig
    {
        public uint SampleRate;     // 0 = engine default (48000)
        public uint BufferFrames;   // 0 = backend default
        public uint Channels;       // 0 = default (stereo out)
        public RopeBackend Backend; // enum, int32
        private uint _r0, _r1, _r2, _r3; // _reserved[4]
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct RopePlayParams
    {
        public float Gain;   // default 1.0
        public float Pan;    // -1..+1
        public int Loop;     // 0 / non-zero
        public float Pitch;  // default 1.0 (<=0 => 1.0)
        public float FadeIn; // seconds (0 = full gain at start)
        public RopeBus Bus;  // category bus (Sfx default)
        private uint _r0;    // _reserved[1]
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct RopeEvent
    {
        public RopeEventType Type;          // int32
        // (4 bytes of padding here so Voice is 8-byte aligned — Sequential
        //  natural packing inserts it, matching the C struct.)
        public ulong Voice;                 // valid for VoiceFinished
        public RopeVoiceEndReason Reason;   // int32
        public uint Data;                   // dropped count for QueueOverflow
        private uint _r0, _r1, _r2, _r3;    // _reserved[4]
    }

    internal static class Native
    {
        // The native library is "rope_audio" (rope_audio.dll / librope_audio.so
        // / .dylib). On iOS the engine is statically linked into the app, so
        // Unity IL2CPP must resolve symbols against "__Internal".
#if UNITY_IOS && !UNITY_EDITOR
        public const string Lib = "__Internal";
#else
        public const string Lib = "rope_audio";
#endif
        private const CallingConvention Cdecl = CallingConvention.Cdecl;

        [DllImport(Lib, CallingConvention = Cdecl)] public static extern uint rope_abi_version();
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern IntPtr rope_result_str(RopeResult r);

        [DllImport(Lib, CallingConvention = Cdecl)] public static extern void rope_config_default(out RopeConfig cfg);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern void rope_play_params_default(out RopePlayParams p);

        [DllImport(Lib, CallingConvention = Cdecl)] public static extern IntPtr rope_engine_create();
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern void rope_engine_destroy(IntPtr engine);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern RopeResult rope_engine_start(IntPtr engine, in RopeConfig cfg);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern RopeResult rope_engine_stop(IntPtr engine);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern int rope_engine_is_running(IntPtr engine);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern uint rope_engine_sample_rate(IntPtr engine);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern uint rope_engine_channels(IntPtr engine);

        [DllImport(Lib, CallingConvention = Cdecl)] public static extern uint rope_load_wav_file(
            IntPtr engine, [MarshalAs(UnmanagedType.LPUTF8Str)] string utf8Path);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern uint rope_load_wav_memory(IntPtr engine, IntPtr data, UIntPtr size);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern RopeResult rope_unload_sound(IntPtr engine, uint sound);

        [DllImport(Lib, CallingConvention = Cdecl)] public static extern ulong rope_play(IntPtr engine, uint sound, in RopePlayParams p);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern RopeResult rope_stop_voice(IntPtr engine, ulong voice);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern RopeResult rope_stop_voice_fade(IntPtr engine, ulong voice, float fadeSeconds);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern RopeResult rope_stop_all(IntPtr engine);

        [DllImport(Lib, CallingConvention = Cdecl)] public static extern RopeResult rope_set_voice_gain(IntPtr engine, ulong voice, float gain);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern RopeResult rope_set_voice_pan(IntPtr engine, ulong voice, float pan);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern RopeResult rope_set_voice_pitch(IntPtr engine, ulong voice, float pitch);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern RopeResult rope_set_master_volume(IntPtr engine, float gain);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern float rope_get_master_volume(IntPtr engine);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern RopeResult rope_set_bus_volume(IntPtr engine, RopeBus bus, float gain);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern float rope_get_bus_volume(IntPtr engine, RopeBus bus);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern void rope_set_master_limiter(IntPtr engine, int enabled);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern int rope_get_master_limiter(IntPtr engine);

        [DllImport(Lib, CallingConvention = Cdecl)] public static extern RopeResult rope_engine_suspend(IntPtr engine);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern RopeResult rope_engine_resume(IntPtr engine);

        [DllImport(Lib, CallingConvention = Cdecl)] public static extern int rope_poll_event(IntPtr engine, out RopeEvent ev);
        [DllImport(Lib, CallingConvention = Cdecl)] public static extern void rope_render_offline(IntPtr engine, IntPtr outBuffer, uint frames);
    }
}
