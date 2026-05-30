#include "rope/AudioEngine.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace rope {
namespace {

constexpr std::size_t kMaxVoices            = 64;   ///< polyphony limit
constexpr std::size_t kCommandQueueCap      = 256;  ///< pending control->audio commands
constexpr std::size_t kEventQueueCap        = 256;  ///< pending audio->control events
constexpr std::size_t kControlEventCap      = 16;   ///< pending control-origin events
constexpr unsigned int kDefaultSampleRate   = 48000;
constexpr unsigned int kDefaultOutChannels  = 2;    ///< requested stereo output
constexpr float        kCenterPanGain       = 0.70710678f; // cos(pi/4) = sin(pi/4)

/// Constant-power pan law: pan in [-1,1] -> per-channel gains. Center is -3 dB
/// on each side, preserving perceived loudness. Cheap enough to call per command
/// (never per sample).
inline void computePan(float pan, float& panL, float& panR) {
    if (pan < -1.0f) pan = -1.0f;
    else if (pan > 1.0f) pan = 1.0f;
    const float theta = (pan + 1.0f) * 0.25f * 3.14159265358979323846f; // (p+1)*pi/4
    panL = std::cos(theta);
    panR = std::sin(theta);
}

// --- Control -> audio thread messages --------------------------------------
enum class CommandType { Play, Stop, StopAll, SetGain, SetPan, SetMaster };

struct Command {
    CommandType        type{};
    const AudioBuffer* buffer = nullptr; // Play only
    VoiceHandle        voice  = kInvalidVoice;
    float              gain   = 1.0f;    // Play / SetGain / SetMaster
    float              pan    = 0.0f;    // Play / SetPan
    bool               loop   = false;
};

// --- Audio/control -> poll events ------------------------------------------
struct EngineEvent {
    enum class Kind : std::uint32_t {
        VoiceFinished, VoicesExhausted, QueueOverflow, Suspended, Resumed
    };
    Kind          kind   = Kind::VoiceFinished;
    VoiceHandle   voice  = kInvalidVoice;
    std::uint32_t reason = 0;  // -> VoiceEndReason
    std::uint32_t data   = 0;  // dropped count for QueueOverflow
};

/// Minimal single-producer / single-consumer lock-free ring buffer.
template <typename T, std::size_t Capacity>
class SpscQueue {
public:
    bool push(const T& item) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) % Capacity;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false; // full
        }
        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& out) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false; // empty
        }
        out = buffer_[tail];
        tail_.store((tail + 1) % Capacity, std::memory_order_release);
        return true;
    }

private:
    std::array<T, Capacity>  buffer_{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};

/// A single playing instance. Owned and mutated exclusively by the audio thread.
struct Voice {
    const AudioBuffer* buffer   = nullptr;
    std::size_t        position = 0;     // next frame to read
    float              gain     = 1.0f;
    float              pan      = 0.0f;
    float              panL     = kCenterPanGain;
    float              panR     = kCenterPanGain;
    bool               loop     = false;
    bool               active   = false;
    VoiceHandle        id       = kInvalidVoice;
};

} // namespace

// ---------------------------------------------------------------------------
// Implementation (pimpl) — keeps the backend and the mixer out of the public API.
// ---------------------------------------------------------------------------
struct AudioEngine::Impl {
    std::unique_ptr<AudioBackend> backend;
    bool              running    = false;
    bool              suspended  = false;
    unsigned int      sampleRate = kDefaultSampleRate;
    unsigned int      channels   = kDefaultOutChannels;
    AudioStreamConfig startConfig{};   // remembered for resume()

    // Sound bank — control thread only. unique_ptr gives stable addresses so the
    // audio thread can hold raw pointers into it safely.
    std::vector<std::unique_ptr<AudioBuffer>> sounds;
    // Retired-but-still-referenced buffers; freed on stop() (RT thread joined).
    std::vector<std::unique_ptr<AudioBuffer>> retired;

    // Voice pool — audio thread only.
    std::array<Voice, kMaxVoices> voices{};
    float master = 1.0f;                          // audio thread only

    // Control -> audio command channel.
    SpscQueue<Command, kCommandQueueCap> commands;
    // audio -> control event channel + a small control-origin channel.
    SpscQueue<EngineEvent, kEventQueueCap>     audioEvents;
    SpscQueue<EngineEvent, kControlEventCap>   controlEvents;
    std::atomic<std::uint32_t> droppedEvents{0};

    std::atomic<VoiceHandle>   nextVoiceId{1};
    std::atomic<float>         masterShadow{1.0f}; // control-side mirror of master

    // ---- audio thread ----

    void pushEvent(const EngineEvent& e) {
        if (!audioEvents.push(e)) {
            droppedEvents.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void drainCommands() {
        Command cmd;
        while (commands.pop(cmd)) {
            switch (cmd.type) {
            case CommandType::Play: {
                Voice* slot = nullptr;
                for (Voice& v : voices) {
                    if (!v.active) { slot = &v; break; }
                }
                if (!slot) {
                    pushEvent({EngineEvent::Kind::VoicesExhausted, kInvalidVoice, 0, 0});
                    break;
                }
                slot->buffer   = cmd.buffer;
                slot->position = 0;
                slot->gain     = cmd.gain;
                slot->pan      = cmd.pan;
                computePan(cmd.pan, slot->panL, slot->panR);
                slot->loop     = cmd.loop;
                slot->id       = cmd.voice;
                slot->active   = true;
                break;
            }
            case CommandType::Stop:
                for (Voice& v : voices) {
                    if (v.active && v.id == cmd.voice) {
                        v.active = false;
                        pushEvent({EngineEvent::Kind::VoiceFinished, v.id,
                                   static_cast<std::uint32_t>(VoiceEndReason::Stopped), 0});
                        break;
                    }
                }
                break;
            case CommandType::StopAll:
                for (Voice& v : voices) {
                    if (v.active) {
                        v.active = false;
                        pushEvent({EngineEvent::Kind::VoiceFinished, v.id,
                                   static_cast<std::uint32_t>(VoiceEndReason::Stopped), 0});
                    }
                }
                break;
            case CommandType::SetGain:
                for (Voice& v : voices) {
                    if (v.active && v.id == cmd.voice) { v.gain = cmd.gain; break; }
                }
                break;
            case CommandType::SetPan:
                for (Voice& v : voices) {
                    if (v.active && v.id == cmd.voice) {
                        v.pan = cmd.pan;
                        computePan(cmd.pan, v.panL, v.panR);
                        break;
                    }
                }
                break;
            case CommandType::SetMaster:
                master = cmd.gain;
                break;
            }
        }
    }

    void mix(float* out, unsigned int nFrames) {
        const unsigned int outCh = channels;
        std::memset(out, 0, sizeof(float) * nFrames * outCh);

        for (Voice& v : voices) {
            if (!v.active || v.buffer == nullptr) continue;

            const AudioBuffer&  buf    = *v.buffer;
            const std::size_t   frames = buf.frameCount();
            const std::uint32_t srcCh  = buf.channels;

            for (unsigned int f = 0; f < nFrames; ++f) {
                if (v.position >= frames) {
                    if (v.loop) {
                        v.position = 0;
                    } else {
                        v.active = false;
                        pushEvent({EngineEvent::Kind::VoiceFinished, v.id,
                                   static_cast<std::uint32_t>(VoiceEndReason::Natural), 0});
                        break;
                    }
                }
                const float* src = &buf.samples[v.position * srcCh];
                // Mono source -> both ears; stereo+ -> first two channels.
                const float l = src[0];
                const float r = (srcCh == 1) ? src[0] : src[1];

                if (outCh == 1) {
                    out[f] += 0.5f * (l * v.panL + r * v.panR) * v.gain; // downmix
                } else {
                    out[f * outCh + 0] += l * v.gain * v.panL;
                    out[f * outCh + 1] += r * v.gain * v.panR;
                    // Output channels beyond stereo are left silent for now.
                }
                ++v.position;
            }
        }

        // Master gain over the whole block (instantaneous in v1; a limiter /
        // ramp would go here in a later version).
        if (master != 1.0f) {
            const std::size_t total = static_cast<std::size_t>(nFrames) * outCh;
            for (std::size_t i = 0; i < total; ++i) out[i] *= master;
        }
    }

    // RenderCallback invoked by whichever backend is active, on its RT thread.
    static void render(float* out, unsigned int frames, void* user) {
        auto* self = static_cast<Impl*>(user);
        self->drainCommands();
        self->mix(out, frames);
    }

    // ---- control thread ----

    bool pushCommand(const Command& cmd) {
        return commands.push(cmd);
    }
};

namespace {

Event translateEvent(const EngineEvent& e) {
    Event out;
    out.voice  = e.voice;
    out.reason = static_cast<VoiceEndReason>(e.reason);
    out.count  = e.data;
    switch (e.kind) {
    case EngineEvent::Kind::VoiceFinished:   out.type = EventType::VoiceFinished;   break;
    case EngineEvent::Kind::VoicesExhausted: out.type = EventType::VoicesExhausted; break;
    case EngineEvent::Kind::QueueOverflow:   out.type = EventType::QueueOverflow;   break;
    case EngineEvent::Kind::Suspended:       out.type = EventType::Suspended;       break;
    case EngineEvent::Kind::Resumed:         out.type = EventType::Resumed;         break;
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}

AudioEngine::~AudioEngine() { stop(); }

bool AudioEngine::start(unsigned int sampleRate, unsigned int bufferFrames,
                        BackendType backendType) {
    if (impl_->running) return true;

    impl_->backend = createAudioBackend(backendType);
    if (!impl_->backend) {
        std::fprintf(stderr, "[rope] requested audio backend is not available\n");
        return false;
    }

    AudioStreamConfig config;
    config.sampleRate   = sampleRate ? sampleRate : kDefaultSampleRate;
    config.bufferFrames = bufferFrames;
    config.channels     = impl_->channels;

    if (!impl_->backend->start(config, &Impl::render, impl_.get())) {
        std::fprintf(stderr, "[rope] failed to start audio backend\n");
        impl_->backend.reset();
        return false;
    }

    // Adopt whatever the device actually negotiated.
    impl_->startConfig  = config;
    impl_->sampleRate   = impl_->backend->sampleRate();
    impl_->channels     = impl_->backend->channels();
    impl_->running      = true;
    impl_->suspended    = false;

    std::printf("[rope] backend: %s | %u Hz | %u ch\n",
                impl_->backend->name(), impl_->sampleRate, impl_->channels);
    return true;
}

void AudioEngine::stop() {
    if (!impl_->running) return;
    if (impl_->backend) impl_->backend->stop();
    impl_->backend.reset();
    impl_->running   = false;
    impl_->suspended = false;
    impl_->retired.clear();   // RT thread is joined; safe to free retired buffers
}

bool AudioEngine::isRunning() const noexcept { return impl_->running; }

SoundHandle AudioEngine::loadWav(const std::filesystem::path& path) {
    std::optional<AudioBuffer> decoded = decodeWav(path);
    if (!decoded) {
        std::fprintf(stderr, "[rope] failed to decode WAV: %s\n",
                     path.string().c_str());
        return kInvalidSound;
    }
    if (impl_->running && decoded->sampleRate != impl_->sampleRate) {
        std::fprintf(stderr,
            "[rope] warning: '%s' is %u Hz but the engine runs at %u Hz; "
            "it will play back pitch-shifted (resampling not implemented yet)\n",
            path.string().c_str(), decoded->sampleRate, impl_->sampleRate);
    }
    impl_->sounds.push_back(std::make_unique<AudioBuffer>(std::move(*decoded)));
    return static_cast<SoundHandle>(impl_->sounds.size() - 1);
}

SoundHandle AudioEngine::loadWavMemory(const void* data, std::size_t size) {
    std::optional<AudioBuffer> decoded = decodeWav(data, size);
    if (!decoded) {
        std::fprintf(stderr, "[rope] failed to decode WAV from memory (%zu bytes)\n", size);
        return kInvalidSound;
    }
    if (impl_->running && decoded->sampleRate != impl_->sampleRate) {
        std::fprintf(stderr,
            "[rope] warning: in-memory WAV is %u Hz but the engine runs at %u Hz; "
            "it will play back pitch-shifted (resampling not implemented yet)\n",
            decoded->sampleRate, impl_->sampleRate);
    }
    impl_->sounds.push_back(std::make_unique<AudioBuffer>(std::move(*decoded)));
    return static_cast<SoundHandle>(impl_->sounds.size() - 1);
}

bool AudioEngine::unloadSound(SoundHandle sound) {
    if (sound >= impl_->sounds.size() || !impl_->sounds[sound]) return false;
    // Move the buffer aside instead of freeing: voices may still reference it.
    // It is reclaimed on stop(), when the audio thread is guaranteed joined.
    impl_->retired.push_back(std::move(impl_->sounds[sound]));
    impl_->sounds[sound].reset();
    return true;
}

VoiceHandle AudioEngine::play(SoundHandle sound, const PlayParams& params) {
    if (sound >= impl_->sounds.size() || !impl_->sounds[sound]) return kInvalidVoice;

    const VoiceHandle id =
        impl_->nextVoiceId.fetch_add(1, std::memory_order_relaxed);

    Command cmd;
    cmd.type   = CommandType::Play;
    cmd.buffer = impl_->sounds[sound].get();
    cmd.voice  = id;
    cmd.gain   = params.gain;
    cmd.pan    = params.pan;
    cmd.loop   = params.loop;

    if (!impl_->pushCommand(cmd)) {
        std::fprintf(stderr, "[rope] command queue full; play() dropped\n");
        return kInvalidVoice;
    }
    return id;
}

void AudioEngine::stopVoice(VoiceHandle voice) {
    Command cmd;
    cmd.type  = CommandType::Stop;
    cmd.voice = voice;
    impl_->pushCommand(cmd);
}

void AudioEngine::stopAll() {
    Command cmd;
    cmd.type = CommandType::StopAll;
    impl_->pushCommand(cmd);
}

void AudioEngine::setVoiceGain(VoiceHandle voice, float gain) {
    Command cmd;
    cmd.type  = CommandType::SetGain;
    cmd.voice = voice;
    cmd.gain  = gain;
    impl_->pushCommand(cmd);
}

void AudioEngine::setVoicePan(VoiceHandle voice, float pan) {
    Command cmd;
    cmd.type  = CommandType::SetPan;
    cmd.voice = voice;
    cmd.pan   = pan;
    impl_->pushCommand(cmd);
}

void AudioEngine::setMasterVolume(float gain) {
    impl_->masterShadow.store(gain, std::memory_order_relaxed);
    Command cmd;
    cmd.type = CommandType::SetMaster;
    cmd.gain = gain;
    impl_->pushCommand(cmd);
}

float AudioEngine::masterVolume() const noexcept {
    return impl_->masterShadow.load(std::memory_order_relaxed);
}

bool AudioEngine::pollEvent(Event& out) {
    EngineEvent e;
    // Control-origin events first (Suspended/Resumed).
    if (impl_->controlEvents.pop(e)) { out = translateEvent(e); return true; }
    // Then surface any dropped-event overflow as a single synthetic event.
    const std::uint32_t dropped =
        impl_->droppedEvents.exchange(0, std::memory_order_relaxed);
    if (dropped != 0) {
        out = Event{EventType::QueueOverflow, kInvalidVoice, VoiceEndReason::Natural, dropped};
        return true;
    }
    // Then the audio-thread events.
    if (impl_->audioEvents.pop(e)) { out = translateEvent(e); return true; }
    return false;
}

void AudioEngine::suspend() {
    if (!impl_->running || impl_->suspended) return;
    if (impl_->backend) impl_->backend->stop();
    impl_->suspended = true;
    impl_->controlEvents.push({EngineEvent::Kind::Suspended, kInvalidVoice, 0, 0});
}

void AudioEngine::resume() {
    if (!impl_->running || !impl_->suspended) return;
    if (impl_->backend &&
        impl_->backend->start(impl_->startConfig, &Impl::render, impl_.get())) {
        impl_->sampleRate = impl_->backend->sampleRate();
        impl_->channels   = impl_->backend->channels();
    } else {
        std::fprintf(stderr, "[rope] resume(): failed to restart the audio device\n");
    }
    impl_->suspended = false;
    impl_->controlEvents.push({EngineEvent::Kind::Resumed, kInvalidVoice, 0, 0});
}

unsigned int AudioEngine::sampleRate() const noexcept { return impl_->sampleRate; }
unsigned int AudioEngine::outputChannels() const noexcept { return impl_->channels; }

} // namespace rope
