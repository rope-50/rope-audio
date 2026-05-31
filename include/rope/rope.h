/*
 * rope-audioengine — stable C ABI (the game-facing "protocol").
 *
 * A thin, FFI-friendly C interface over the C++ engine. Designed to be:
 *   - callable from C99 and from any FFI (Dart `dart:ffi`, C# P/Invoke, Rust,
 *     Swift, Kotlin/JNI, ...),
 *   - ABI-stable: enums only grow (each pinned to 32 bits), structs only append
 *     into a trailing `_reserved` area, and every input struct has a
 *     `*_default()` initializer.
 *
 * Threading: call every function below from a SINGLE control thread (typically
 * your game/main thread). The audio runs on its own real-time thread and never
 * calls back into your code — events are delivered only via rope_poll_event().
 */
#ifndef ROPE_AUDIOENGINE_ROPE_H
#define ROPE_AUDIOENGINE_ROPE_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(ROPE_BUILD_SHARED)
#  ifdef ROPE_BUILD_LIBRARY
#    define ROPE_API __declspec(dllexport)
#  else
#    define ROPE_API __declspec(dllimport)
#  endif
#else
#  define ROPE_API
#endif
#define ROPE_CALL /* platform default calling convention; reserved */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Versioning ---------------------------------------------------------- */
#define ROPE_ABI_VERSION_MAJOR 0u
#define ROPE_ABI_VERSION_MINOR 7u

/* ---- Handles ------------------------------------------------------------- */
typedef struct rope_engine* rope_engine_t; /* opaque; NULL = invalid */
typedef uint32_t            rope_sound;     /* sound-bank handle */
typedef uint64_t            rope_voice;     /* playing-instance handle */

#define ROPE_INVALID_SOUND ((rope_sound)0xFFFFFFFFu)
#define ROPE_INVALID_VOICE ((rope_voice)0)

/* ---- Enums (append-only; 32-bit pinned) ---------------------------------- */
typedef enum rope_result {
    ROPE_OK = 0,
    ROPE_ERR_UNKNOWN = 1,
    ROPE_ERR_INVALID_ARGUMENT = 2,    /* null handle, bad pointer, NaN/Inf */
    ROPE_ERR_NOT_RUNNING = 3,
    ROPE_ERR_ALREADY_RUNNING = 4,
    ROPE_ERR_BACKEND_UNAVAILABLE = 5, /* backend not built in / device failed */
    ROPE_ERR_DECODE_FAILED = 6,
    ROPE_ERR_QUEUE_FULL = 7,
    ROPE_ERR_OUT_OF_SOUNDS = 8,
    ROPE_RESULT_FORCE_U32 = 0x7fffffff
} rope_result;

typedef enum rope_backend {
    ROPE_BACKEND_DEFAULT = 0,         /* best available (currently miniaudio) */
    ROPE_BACKEND_MINIAUDIO = 1,       /* cross-platform incl. Android/iOS */
    ROPE_BACKEND_RTAUDIO = 2,         /* desktop; native API (WASAPI on Win) */
    ROPE_BACKEND_RTAUDIO_ASIO = 3,    /* Windows; force ASIO (low latency) */
    ROPE_BACKEND_NULL = 4,            /* no device; offline/headless (renderoffline) */
    ROPE_BACKEND_FORCE_U32 = 0x7fffffff
} rope_backend;

typedef enum rope_event_type {
    ROPE_EVENT_NONE = 0,
    ROPE_EVENT_VOICE_FINISHED = 1,    /* a voice stopped (see voice, reason) */
    ROPE_EVENT_VOICES_EXHAUSTED = 2,  /* a play() was dropped: pool full */
    ROPE_EVENT_QUEUE_OVERFLOW = 3,    /* events were dropped (see data) */
    ROPE_EVENT_SUSPENDED = 4,         /* device suspended (mobile lifecycle) */
    ROPE_EVENT_RESUMED = 5,
    ROPE_EVENT_DEVICE_CHANGED = 6,    /* reserved; not emitted yet */
    ROPE_EVENT_FORCE_U32 = 0x7fffffff
} rope_event_type;

typedef enum rope_voice_end_reason {
    ROPE_VOICE_END_NATURAL = 0,       /* reached end of a non-looping buffer */
    ROPE_VOICE_END_STOPPED = 1,       /* explicit stop_voice / stop_all */
    ROPE_VOICE_END_STOLEN  = 2,       /* reclaimed by voice-stealing (future) */
    ROPE_VOICE_END_FORCE_U32 = 0x7fffffff
} rope_voice_end_reason;

typedef enum rope_bus {
    ROPE_BUS_SFX = 0,                 /* default */
    ROPE_BUS_MUSIC = 1,
    ROPE_BUS_UI = 2,
    ROPE_BUS_FORCE_U32 = 0x7fffffff
} rope_bus;

/* ---- POD structs (append into _reserved only) ---------------------------- */
typedef struct rope_config {
    uint32_t     sample_rate;    /* 0 = engine default (48000) */
    uint32_t     buffer_frames;  /* 0 = backend default */
    uint32_t     channels;       /* 0 = default; currently always stereo out */
    rope_backend backend;        /* ROPE_BACKEND_DEFAULT */
    uint32_t     _reserved[4];
} rope_config;

typedef struct rope_play_params {
    float    gain;        /* linear, default 1.0 */
    float    pan;         /* -1 = left, 0 = center, +1 = right */
    int32_t  loop;        /* 0 = one-shot, non-zero = loop */
    float    pitch;       /* speed/pitch ratio, default 1.0 (<=0 is treated as 1.0) */
    float    fade_in;     /* fade-in seconds (0 = full gain at start) */
    rope_bus bus;         /* category bus (ROPE_BUS_SFX default) */
    uint32_t _reserved[1];/* future: start_offset... */
} rope_play_params;

typedef struct rope_event {
    rope_event_type       type;
    rope_voice            voice;  /* valid for VOICE_FINISHED */
    rope_voice_end_reason reason; /* valid for VOICE_FINISHED */
    uint32_t              data;   /* dropped count for QUEUE_OVERFLOW */
    uint32_t              _reserved[4];
} rope_event;

/* ---- Version / introspection --------------------------------------------- */
ROPE_API uint32_t    ROPE_CALL rope_abi_version(void); /* (major<<16)|minor */
ROPE_API const char* ROPE_CALL rope_result_str(rope_result r); /* never NULL */

/* ---- Struct initializers (fill with current defaults) -------------------- */
ROPE_API void ROPE_CALL rope_config_default(rope_config* out);
ROPE_API void ROPE_CALL rope_play_params_default(rope_play_params* out);

/* ---- Lifecycle ----------------------------------------------------------- */
ROPE_API rope_engine_t ROPE_CALL rope_engine_create(void);       /* NULL on OOM */
ROPE_API void          ROPE_CALL rope_engine_destroy(rope_engine_t); /* NULL-safe */
ROPE_API rope_result   ROPE_CALL rope_engine_start(rope_engine_t, const rope_config* /*NULL=defaults*/);
ROPE_API rope_result   ROPE_CALL rope_engine_stop(rope_engine_t);
ROPE_API int32_t       ROPE_CALL rope_engine_is_running(rope_engine_t); /* 0/1 */
ROPE_API uint32_t      ROPE_CALL rope_engine_sample_rate(rope_engine_t);
ROPE_API uint32_t      ROPE_CALL rope_engine_channels(rope_engine_t);
/* Monotonic output-frame clock (frames produced since start). Read it to
 * schedule sample-accurate playback via rope_play_scheduled; resets on start/stop. */
ROPE_API uint64_t      ROPE_CALL rope_current_frame(rope_engine_t);

/* ---- Assets -------------------------------------------------------------- */
/* Decode an audio asset into the sound bank. The container is detected from the
 * data (not the extension): WAV, FLAC, OGG/Vorbis and MP3 are supported. The
 * `_wav` names are kept for ABI stability. Returns ROPE_INVALID_SOUND on failure. */
ROPE_API rope_sound  ROPE_CALL rope_load_wav_file(rope_engine_t, const char* utf8_path);
ROPE_API rope_sound  ROPE_CALL rope_load_wav_memory(rope_engine_t, const void* data, size_t size);
ROPE_API rope_result ROPE_CALL rope_unload_sound(rope_engine_t, rope_sound);

/* ---- Playback ------------------------------------------------------------ */
ROPE_API rope_voice  ROPE_CALL rope_play(rope_engine_t, rope_sound, const rope_play_params* /*NULL=defaults*/);
/* Like rope_play, but the voice starts at absolute output-frame `start_frame` on
 * the sample clock (see rope_current_frame). A time of 0 or already in the past
 * starts immediately. Use currentFrame + N for sample-accurate cueing. */
ROPE_API rope_voice  ROPE_CALL rope_play_scheduled(rope_engine_t, rope_sound,
                                                   const rope_play_params* /*NULL=defaults*/,
                                                   uint64_t start_frame);
ROPE_API rope_result ROPE_CALL rope_stop_voice(rope_engine_t, rope_voice);
/* Stop a voice with a fade-out in seconds (0 behaves like rope_stop_voice). */
ROPE_API rope_result ROPE_CALL rope_stop_voice_fade(rope_engine_t, rope_voice, float fade_seconds);
ROPE_API rope_result ROPE_CALL rope_stop_all(rope_engine_t);

/* ---- Live mix control ---------------------------------------------------- */
ROPE_API rope_result ROPE_CALL rope_set_voice_gain(rope_engine_t, rope_voice, float gain);
ROPE_API rope_result ROPE_CALL rope_set_voice_pan(rope_engine_t, rope_voice, float pan);
ROPE_API rope_result ROPE_CALL rope_set_voice_pitch(rope_engine_t, rope_voice, float pitch);
ROPE_API rope_result ROPE_CALL rope_set_master_volume(rope_engine_t, float gain);
ROPE_API float       ROPE_CALL rope_get_master_volume(rope_engine_t);
/* Category bus (SFX/Music/UI) group volume; smoothed (~5 ms). */
ROPE_API rope_result ROPE_CALL rope_set_bus_volume(rope_engine_t, rope_bus, float gain);
ROPE_API float       ROPE_CALL rope_get_bus_volume(rope_engine_t, rope_bus);
/* Per-bus mute/solo (non-zero = on). While any bus is soloed, only soloed buses
 * are audible; a muted bus is always silent. Smoothed (~5 ms). */
ROPE_API rope_result ROPE_CALL rope_set_bus_muted(rope_engine_t, rope_bus, int32_t muted);
ROPE_API int32_t     ROPE_CALL rope_get_bus_muted(rope_engine_t, rope_bus);
ROPE_API rope_result ROPE_CALL rope_set_bus_soloed(rope_engine_t, rope_bus, int32_t soloed);
ROPE_API int32_t     ROPE_CALL rope_get_bus_soloed(rope_engine_t, rope_bus);
/* Master-bus soft-clip limiter (on by default; non-zero = enabled). */
ROPE_API void        ROPE_CALL rope_set_master_limiter(rope_engine_t, int32_t enabled);
ROPE_API int32_t     ROPE_CALL rope_get_master_limiter(rope_engine_t);

/* ---- Mobile lifecycle ---------------------------------------------------- */
ROPE_API rope_result ROPE_CALL rope_engine_suspend(rope_engine_t);
ROPE_API rope_result ROPE_CALL rope_engine_resume(rope_engine_t);

/* ---- Events (poll on the control thread; drain in a while-loop) ----------- */
ROPE_API int32_t ROPE_CALL rope_poll_event(rope_engine_t, rope_event* out); /* 1=filled, 0=empty */

/* ---- Offline rendering (use with ROPE_BACKEND_NULL; testing / headless) ---
 * Renders `frames` frames of interleaved float into `out` (frames * channels
 * floats) by running the mixer synchronously. Do NOT call while a real device
 * backend is streaming. */
ROPE_API void ROPE_CALL rope_render_offline(rope_engine_t, float* out, uint32_t frames);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROPE_AUDIOENGINE_ROPE_H */
