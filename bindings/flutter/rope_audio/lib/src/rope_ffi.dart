// Hand-written dart:ffi bindings for the rope-audioengine C ABI (rope.h).
//
// The ABI is small and stable, so these are maintained by hand (no ffigen /
// libclang needed). Struct field order and types mirror rope.h exactly; Dart
// FFI applies the platform's natural alignment, matching the C layout.

import 'dart:ffi';

import 'package:ffi/ffi.dart';

/// Opaque engine handle (`struct rope_engine*`).
final class RopeEngineHandle extends Opaque {}

/// Mirrors `rope_config`.
final class RopeConfigNative extends Struct {
  @Uint32()
  external int sampleRate;
  @Uint32()
  external int bufferFrames;
  @Uint32()
  external int channels;
  @Int32()
  external int backend;
  @Array(4)
  external Array<Uint32> reserved;
}

/// Mirrors `rope_play_params`.
final class RopePlayParamsNative extends Struct {
  @Float()
  external double gain;
  @Float()
  external double pan;
  @Int32()
  external int loop;
  @Float()
  external double pitch;
  @Array(3)
  external Array<Uint32> reserved;
}

/// Mirrors `rope_event`.
final class RopeEventNative extends Struct {
  @Int32()
  external int type;
  @Uint64()
  external int voice;
  @Int32()
  external int reason;
  @Uint32()
  external int data;
  @Array(4)
  external Array<Uint32> reserved;
}

// --- Native function signatures --------------------------------------------
typedef _U32Void = Uint32 Function();
typedef _EngineCreateC = Pointer<RopeEngineHandle> Function();
typedef _VoidEngineC = Void Function(Pointer<RopeEngineHandle>);
typedef _VoidEngineD = void Function(Pointer<RopeEngineHandle>);
typedef _I32EngineC = Int32 Function(Pointer<RopeEngineHandle>);
typedef _I32EngineD = int Function(Pointer<RopeEngineHandle>);
typedef _U32EngineC = Uint32 Function(Pointer<RopeEngineHandle>);
typedef _U32EngineD = int Function(Pointer<RopeEngineHandle>);
typedef _F32EngineC = Float Function(Pointer<RopeEngineHandle>);
typedef _F32EngineD = double Function(Pointer<RopeEngineHandle>);

/// Looks up every rope_* entry point from a loaded library.
class RopeBindings {
  RopeBindings(DynamicLibrary dl)
      : abiVersion =
            dl.lookupFunction<_U32Void, int Function()>('rope_abi_version'),
        resultStr = dl.lookupFunction<Pointer<Utf8> Function(Int32),
            Pointer<Utf8> Function(int)>('rope_result_str'),
        configDefault = dl.lookupFunction<Void Function(Pointer<RopeConfigNative>),
            void Function(Pointer<RopeConfigNative>)>('rope_config_default'),
        playParamsDefault = dl.lookupFunction<
            Void Function(Pointer<RopePlayParamsNative>),
            void Function(
                Pointer<RopePlayParamsNative>)>('rope_play_params_default'),
        engineCreate = dl.lookupFunction<_EngineCreateC,
            Pointer<RopeEngineHandle> Function()>('rope_engine_create'),
        engineDestroy = dl
            .lookupFunction<_VoidEngineC, _VoidEngineD>('rope_engine_destroy'),
        engineStart = dl.lookupFunction<
            Int32 Function(Pointer<RopeEngineHandle>, Pointer<RopeConfigNative>),
            int Function(Pointer<RopeEngineHandle>,
                Pointer<RopeConfigNative>)>('rope_engine_start'),
        engineStop =
            dl.lookupFunction<_I32EngineC, _I32EngineD>('rope_engine_stop'),
        engineIsRunning = dl
            .lookupFunction<_I32EngineC, _I32EngineD>('rope_engine_is_running'),
        engineSampleRate = dl.lookupFunction<_U32EngineC, _U32EngineD>(
            'rope_engine_sample_rate'),
        engineChannels = dl
            .lookupFunction<_U32EngineC, _U32EngineD>('rope_engine_channels'),
        loadWavFile = dl.lookupFunction<
            Uint32 Function(Pointer<RopeEngineHandle>, Pointer<Utf8>),
            int Function(Pointer<RopeEngineHandle>,
                Pointer<Utf8>)>('rope_load_wav_file'),
        loadWavMemory = dl.lookupFunction<
            Uint32 Function(Pointer<RopeEngineHandle>, Pointer<Void>, Size),
            int Function(Pointer<RopeEngineHandle>, Pointer<Void>,
                int)>('rope_load_wav_memory'),
        unloadSound = dl.lookupFunction<
            Int32 Function(Pointer<RopeEngineHandle>, Uint32),
            int Function(
                Pointer<RopeEngineHandle>, int)>('rope_unload_sound'),
        play = dl.lookupFunction<
            Uint64 Function(Pointer<RopeEngineHandle>, Uint32,
                Pointer<RopePlayParamsNative>),
            int Function(Pointer<RopeEngineHandle>, int,
                Pointer<RopePlayParamsNative>)>('rope_play'),
        stopVoice = dl.lookupFunction<
            Int32 Function(Pointer<RopeEngineHandle>, Uint64),
            int Function(Pointer<RopeEngineHandle>, int)>('rope_stop_voice'),
        stopAll =
            dl.lookupFunction<_I32EngineC, _I32EngineD>('rope_stop_all'),
        setVoiceGain = dl.lookupFunction<
            Int32 Function(Pointer<RopeEngineHandle>, Uint64, Float),
            int Function(Pointer<RopeEngineHandle>, int,
                double)>('rope_set_voice_gain'),
        setVoicePan = dl.lookupFunction<
            Int32 Function(Pointer<RopeEngineHandle>, Uint64, Float),
            int Function(Pointer<RopeEngineHandle>, int,
                double)>('rope_set_voice_pan'),
        setVoicePitch = dl.lookupFunction<
            Int32 Function(Pointer<RopeEngineHandle>, Uint64, Float),
            int Function(Pointer<RopeEngineHandle>, int,
                double)>('rope_set_voice_pitch'),
        setMasterVolume = dl.lookupFunction<
            Int32 Function(Pointer<RopeEngineHandle>, Float),
            int Function(
                Pointer<RopeEngineHandle>, double)>('rope_set_master_volume'),
        getMasterVolume = dl.lookupFunction<_F32EngineC, _F32EngineD>(
            'rope_get_master_volume'),
        setMasterLimiter = dl.lookupFunction<
            Void Function(Pointer<RopeEngineHandle>, Int32),
            void Function(
                Pointer<RopeEngineHandle>, int)>('rope_set_master_limiter'),
        getMasterLimiter = dl.lookupFunction<_I32EngineC, _I32EngineD>(
            'rope_get_master_limiter'),
        engineSuspend = dl
            .lookupFunction<_I32EngineC, _I32EngineD>('rope_engine_suspend'),
        engineResume =
            dl.lookupFunction<_I32EngineC, _I32EngineD>('rope_engine_resume'),
        pollEvent = dl.lookupFunction<
            Int32 Function(Pointer<RopeEngineHandle>, Pointer<RopeEventNative>),
            int Function(Pointer<RopeEngineHandle>,
                Pointer<RopeEventNative>)>('rope_poll_event');

  final int Function() abiVersion;
  final Pointer<Utf8> Function(int) resultStr;
  final void Function(Pointer<RopeConfigNative>) configDefault;
  final void Function(Pointer<RopePlayParamsNative>) playParamsDefault;
  final Pointer<RopeEngineHandle> Function() engineCreate;
  final void Function(Pointer<RopeEngineHandle>) engineDestroy;
  final int Function(Pointer<RopeEngineHandle>, Pointer<RopeConfigNative>)
      engineStart;
  final int Function(Pointer<RopeEngineHandle>) engineStop;
  final int Function(Pointer<RopeEngineHandle>) engineIsRunning;
  final int Function(Pointer<RopeEngineHandle>) engineSampleRate;
  final int Function(Pointer<RopeEngineHandle>) engineChannels;
  final int Function(Pointer<RopeEngineHandle>, Pointer<Utf8>) loadWavFile;
  final int Function(Pointer<RopeEngineHandle>, Pointer<Void>, int)
      loadWavMemory;
  final int Function(Pointer<RopeEngineHandle>, int) unloadSound;
  final int Function(
      Pointer<RopeEngineHandle>, int, Pointer<RopePlayParamsNative>) play;
  final int Function(Pointer<RopeEngineHandle>, int) stopVoice;
  final int Function(Pointer<RopeEngineHandle>) stopAll;
  final int Function(Pointer<RopeEngineHandle>, int, double) setVoiceGain;
  final int Function(Pointer<RopeEngineHandle>, int, double) setVoicePan;
  final int Function(Pointer<RopeEngineHandle>, int, double) setVoicePitch;
  final int Function(Pointer<RopeEngineHandle>, double) setMasterVolume;
  final double Function(Pointer<RopeEngineHandle>) getMasterVolume;
  final void Function(Pointer<RopeEngineHandle>, int) setMasterLimiter;
  final int Function(Pointer<RopeEngineHandle>) getMasterLimiter;
  final int Function(Pointer<RopeEngineHandle>) engineSuspend;
  final int Function(Pointer<RopeEngineHandle>) engineResume;
  final int Function(Pointer<RopeEngineHandle>, Pointer<RopeEventNative>)
      pollEvent;
}
