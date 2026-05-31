// C ABI shim: implements rope.h over the C++ rope::AudioEngine. Every entry
// point guards its arguments and wraps the body in try/catch so a C++ exception
// can never unwind across the C/FFI boundary.

#include "rope/rope.h"

#include "rope/AudioEngine.hpp"

#include <cmath>
#include <cstring>
#include <new>

// The opaque handle is just a heap-owned engine.
struct rope_engine {
    rope::AudioEngine engine;
};

namespace {

inline bool isFiniteF(float v) { return std::isfinite(v); }

rope::BackendType mapBackend(rope_backend b) {
    switch (b) {
    case ROPE_BACKEND_MINIAUDIO:    return rope::BackendType::Miniaudio;
    case ROPE_BACKEND_RTAUDIO:      return rope::BackendType::RtAudio;
    case ROPE_BACKEND_RTAUDIO_ASIO: return rope::BackendType::RtAudioAsio;
    case ROPE_BACKEND_NULL:         return rope::BackendType::Null;
    case ROPE_BACKEND_DEFAULT:
    default:                        return rope::BackendType::Default;
    }
}

rope_event_type mapEventType(rope::EventType t) {
    switch (t) {
    case rope::EventType::VoiceFinished:   return ROPE_EVENT_VOICE_FINISHED;
    case rope::EventType::VoicesExhausted: return ROPE_EVENT_VOICES_EXHAUSTED;
    case rope::EventType::QueueOverflow:   return ROPE_EVENT_QUEUE_OVERFLOW;
    case rope::EventType::Suspended:       return ROPE_EVENT_SUSPENDED;
    case rope::EventType::Resumed:         return ROPE_EVENT_RESUMED;
    case rope::EventType::None:
    default:                               return ROPE_EVENT_NONE;
    }
}

rope::Bus mapBus(rope_bus b) {
    switch (b) {
    case ROPE_BUS_MUSIC: return rope::Bus::Music;
    case ROPE_BUS_UI:    return rope::Bus::Ui;
    case ROPE_BUS_SFX:
    default:             return rope::Bus::Sfx;
    }
}

// Build C++ PlayParams from the C struct (NULL = defaults). Returns false if a
// finite-valued field is NaN/Inf (rejected). startFrame is set by the caller.
bool buildPlayParams(const rope_play_params* p, rope::PlayParams& params) {
    if (!p) return true;
    if (!isFiniteF(p->gain) || !isFiniteF(p->pan)) return false;
    params.gain   = p->gain;
    params.pan    = p->pan;
    params.loop   = p->loop != 0;
    params.pitch  = (p->pitch > 0.0f) ? p->pitch : 1.0f;   // 0/NaN -> neutral
    params.fadeIn = (p->fade_in > 0.0f) ? p->fade_in : 0.0f;
    params.bus    = mapBus(p->bus);
    return true;
}

rope_voice_end_reason mapReason(rope::VoiceEndReason r) {
    switch (r) {
    case rope::VoiceEndReason::Stopped: return ROPE_VOICE_END_STOPPED;
    case rope::VoiceEndReason::Stolen:  return ROPE_VOICE_END_STOLEN;
    case rope::VoiceEndReason::Natural:
    default:                            return ROPE_VOICE_END_NATURAL;
    }
}

} // namespace

extern "C" {

uint32_t rope_abi_version(void) {
    return (ROPE_ABI_VERSION_MAJOR << 16) | ROPE_ABI_VERSION_MINOR;
}

const char* rope_result_str(rope_result r) {
    switch (r) {
    case ROPE_OK:                       return "ok";
    case ROPE_ERR_INVALID_ARGUMENT:     return "invalid argument";
    case ROPE_ERR_NOT_RUNNING:          return "not running";
    case ROPE_ERR_ALREADY_RUNNING:      return "already running";
    case ROPE_ERR_BACKEND_UNAVAILABLE:  return "backend unavailable";
    case ROPE_ERR_DECODE_FAILED:        return "decode failed";
    case ROPE_ERR_QUEUE_FULL:           return "queue full";
    case ROPE_ERR_OUT_OF_SOUNDS:        return "out of sounds";
    case ROPE_ERR_UNKNOWN:
    default:                            return "unknown error";
    }
}

void rope_config_default(rope_config* out) {
    if (!out) return;
    std::memset(out, 0, sizeof(*out));
    out->backend = ROPE_BACKEND_DEFAULT; /* 0; explicit for clarity */
}

void rope_play_params_default(rope_play_params* out) {
    if (!out) return;
    std::memset(out, 0, sizeof(*out));
    out->gain  = 1.0f;
    out->pan   = 0.0f;
    out->loop  = 0;
    out->pitch = 1.0f;
}

rope_engine_t rope_engine_create(void) {
    try {
        return new rope_engine();
    } catch (...) {
        return nullptr;
    }
}

void rope_engine_destroy(rope_engine_t e) {
    delete e; // ~AudioEngine stops the device; delete nullptr is a no-op
}

rope_result rope_engine_start(rope_engine_t e, const rope_config* cfg) {
    if (!e) return ROPE_ERR_INVALID_ARGUMENT;
    try {
        unsigned int sr = cfg ? cfg->sample_rate : 0;
        unsigned int bf = cfg ? cfg->buffer_frames : 0;
        rope::BackendType bk = mapBackend(cfg ? cfg->backend : ROPE_BACKEND_DEFAULT);
        return e->engine.start(sr, bf, bk) ? ROPE_OK : ROPE_ERR_BACKEND_UNAVAILABLE;
    } catch (...) {
        return ROPE_ERR_UNKNOWN;
    }
}

rope_result rope_engine_stop(rope_engine_t e) {
    if (!e) return ROPE_ERR_INVALID_ARGUMENT;
    try { e->engine.stop(); return ROPE_OK; }
    catch (...) { return ROPE_ERR_UNKNOWN; }
}

int32_t rope_engine_is_running(rope_engine_t e) {
    if (!e) return 0;
    try { return e->engine.isRunning() ? 1 : 0; }
    catch (...) { return 0; }
}

uint32_t rope_engine_sample_rate(rope_engine_t e) {
    if (!e) return 0;
    try { return e->engine.sampleRate(); }
    catch (...) { return 0; }
}

uint32_t rope_engine_channels(rope_engine_t e) {
    if (!e) return 0;
    try { return e->engine.outputChannels(); }
    catch (...) { return 0; }
}

uint64_t rope_current_frame(rope_engine_t e) {
    if (!e) return 0;
    try { return e->engine.currentFrame(); }
    catch (...) { return 0; }
}

rope_sound rope_load_wav_file(rope_engine_t e, const char* utf8_path) {
    if (!e || !utf8_path) return ROPE_INVALID_SOUND;
    try {
        // Interpret the incoming bytes as UTF-8 (the char8_t path constructor is
        // the non-deprecated replacement for std::filesystem::u8path).
        return e->engine.loadWav(
            std::filesystem::path(reinterpret_cast<const char8_t*>(utf8_path)));
    } catch (...) {
        return ROPE_INVALID_SOUND;
    }
}

rope_sound rope_load_wav_memory(rope_engine_t e, const void* data, size_t size) {
    if (!e || !data || size == 0) return ROPE_INVALID_SOUND;
    try { return e->engine.loadWavMemory(data, size); }
    catch (...) { return ROPE_INVALID_SOUND; }
}

rope_result rope_unload_sound(rope_engine_t e, rope_sound s) {
    if (!e) return ROPE_ERR_INVALID_ARGUMENT;
    try { return e->engine.unloadSound(s) ? ROPE_OK : ROPE_ERR_INVALID_ARGUMENT; }
    catch (...) { return ROPE_ERR_UNKNOWN; }
}

rope_voice rope_play(rope_engine_t e, rope_sound s, const rope_play_params* p) {
    if (!e) return ROPE_INVALID_VOICE;
    try {
        rope::PlayParams params;
        if (!buildPlayParams(p, params)) return ROPE_INVALID_VOICE;
        return e->engine.play(s, params);
    } catch (...) {
        return ROPE_INVALID_VOICE;
    }
}

rope_voice rope_play_scheduled(rope_engine_t e, rope_sound s, const rope_play_params* p,
                               uint64_t start_frame) {
    if (!e) return ROPE_INVALID_VOICE;
    try {
        rope::PlayParams params;
        if (!buildPlayParams(p, params)) return ROPE_INVALID_VOICE;
        params.startFrame = start_frame;
        return e->engine.play(s, params);
    } catch (...) {
        return ROPE_INVALID_VOICE;
    }
}

rope_result rope_stop_voice(rope_engine_t e, rope_voice v) {
    if (!e) return ROPE_ERR_INVALID_ARGUMENT;
    try { return e->engine.stopVoice(v) ? ROPE_OK : ROPE_ERR_QUEUE_FULL; }
    catch (...) { return ROPE_ERR_UNKNOWN; }
}

rope_result rope_stop_voice_fade(rope_engine_t e, rope_voice v, float fade_seconds) {
    if (!e) return ROPE_ERR_INVALID_ARGUMENT;
    if (!(fade_seconds >= 0.0f)) fade_seconds = 0.0f;  // NaN/negative -> instant
    try { return e->engine.stopVoice(v, fade_seconds) ? ROPE_OK : ROPE_ERR_QUEUE_FULL; }
    catch (...) { return ROPE_ERR_UNKNOWN; }
}

rope_result rope_stop_all(rope_engine_t e) {
    if (!e) return ROPE_ERR_INVALID_ARGUMENT;
    try { return e->engine.stopAll() ? ROPE_OK : ROPE_ERR_QUEUE_FULL; }
    catch (...) { return ROPE_ERR_UNKNOWN; }
}

rope_result rope_set_voice_gain(rope_engine_t e, rope_voice v, float gain) {
    if (!e || !isFiniteF(gain)) return ROPE_ERR_INVALID_ARGUMENT;
    try { return e->engine.setVoiceGain(v, gain) ? ROPE_OK : ROPE_ERR_QUEUE_FULL; }
    catch (...) { return ROPE_ERR_UNKNOWN; }
}

rope_result rope_set_voice_pan(rope_engine_t e, rope_voice v, float pan) {
    if (!e || !isFiniteF(pan)) return ROPE_ERR_INVALID_ARGUMENT;
    try { return e->engine.setVoicePan(v, pan) ? ROPE_OK : ROPE_ERR_QUEUE_FULL; }
    catch (...) { return ROPE_ERR_UNKNOWN; }
}

rope_result rope_set_voice_pitch(rope_engine_t e, rope_voice v, float pitch) {
    if (!e) return ROPE_ERR_INVALID_ARGUMENT;   // pitch is clamped internally
    try { return e->engine.setVoicePitch(v, pitch) ? ROPE_OK : ROPE_ERR_QUEUE_FULL; }
    catch (...) { return ROPE_ERR_UNKNOWN; }
}

rope_result rope_set_master_volume(rope_engine_t e, float gain) {
    if (!e || !isFiniteF(gain)) return ROPE_ERR_INVALID_ARGUMENT;
    try { return e->engine.setMasterVolume(gain) ? ROPE_OK : ROPE_ERR_QUEUE_FULL; }
    catch (...) { return ROPE_ERR_UNKNOWN; }
}

float rope_get_master_volume(rope_engine_t e) {
    if (!e) return 0.0f;
    try { return e->engine.masterVolume(); }
    catch (...) { return 0.0f; }
}

rope_result rope_set_bus_volume(rope_engine_t e, rope_bus bus, float gain) {
    if (!e || !isFiniteF(gain)) return ROPE_ERR_INVALID_ARGUMENT;
    try { return e->engine.setBusVolume(mapBus(bus), gain) ? ROPE_OK : ROPE_ERR_QUEUE_FULL; }
    catch (...) { return ROPE_ERR_UNKNOWN; }
}

float rope_get_bus_volume(rope_engine_t e, rope_bus bus) {
    if (!e) return 0.0f;
    try { return e->engine.busVolume(mapBus(bus)); }
    catch (...) { return 0.0f; }
}

void rope_set_master_limiter(rope_engine_t e, int32_t enabled) {
    if (!e) return;
    try { e->engine.setMasterLimiterEnabled(enabled != 0); } catch (...) {}
}

int32_t rope_get_master_limiter(rope_engine_t e) {
    if (!e) return 0;
    try { return e->engine.masterLimiterEnabled() ? 1 : 0; }
    catch (...) { return 0; }
}

rope_result rope_engine_suspend(rope_engine_t e) {
    if (!e) return ROPE_ERR_INVALID_ARGUMENT;
    try { e->engine.suspend(); return ROPE_OK; }
    catch (...) { return ROPE_ERR_UNKNOWN; }
}

rope_result rope_engine_resume(rope_engine_t e) {
    if (!e) return ROPE_ERR_INVALID_ARGUMENT;
    try { e->engine.resume(); return ROPE_OK; }
    catch (...) { return ROPE_ERR_UNKNOWN; }
}

int32_t rope_poll_event(rope_engine_t e, rope_event* out) {
    if (!e || !out) return 0;
    try {
        rope::Event ev;
        if (!e->engine.pollEvent(ev)) return 0;
        std::memset(out, 0, sizeof(*out));
        out->type   = mapEventType(ev.type);
        out->voice  = ev.voice;
        out->reason = mapReason(ev.reason);
        out->data   = ev.count;
        return 1;
    } catch (...) {
        return 0;
    }
}

void rope_render_offline(rope_engine_t e, float* out, uint32_t frames) {
    if (!e || !out) return;
    try { e->engine.renderOffline(out, frames); } catch (...) {}
}

} // extern "C"
