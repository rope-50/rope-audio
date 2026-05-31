#include "rope/AudioEngine.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
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
/// on each side, preserving perceived loudness. Computed on the control thread
/// only (never on the audio thread) — std::cos/sin are not guaranteed RT-safe.
inline void computePan(float pan, float& panL, float& panR) {
    if (pan < -1.0f) pan = -1.0f;
    else if (pan > 1.0f) pan = 1.0f;
    const float theta = (pan + 1.0f) * 0.25f * 3.14159265358979323846f; // (p+1)*pi/4
    panL = std::cos(theta);
    panR = std::sin(theta);
}

constexpr float kMinPitch = 0.0625f;  // 4 octaves down
constexpr float kMaxPitch = 16.0f;    // 4 octaves up

/// Clamp a pitch ratio to a sane range. 0 / NaN / negative (e.g. a zero-init
/// struct coming across FFI) collapse to the neutral 1.0.
inline float clampPitch(float p) {
    if (!(p > 0.0f)) return 1.0f;
    if (p < kMinPitch) return kMinPitch;
    if (p > kMaxPitch) return kMaxPitch;
    return p;
}

// --- Control -> audio thread messages --------------------------------------
enum class CommandType { Play, Stop, StopAll, SetGain, SetPan, SetPitch, SetMaster };

struct Command {
    CommandType        type{};
    const AudioBuffer* buffer = nullptr;          // Play only
    VoiceHandle        voice  = kInvalidVoice;
    float              gain   = 1.0f;             // Play / SetGain / SetMaster
    float              panL   = kCenterPanGain;   // Play / SetPan (precomputed)
    float              panR   = kCenterPanGain;
    float              pitch  = 1.0f;             // Play / SetPitch
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
    double             position = 0.0;   // fractional source frame index (for resampling)
    float              gain     = 1.0f;
    float              panL     = kCenterPanGain;
    float              panR     = kCenterPanGain;
    float              pitch    = 1.0f;
    bool               loop     = false;
    bool               active   = false;
    VoiceHandle        id       = kInvalidVoice;
};

} // namespace

// ---------------------------------------------------------------------------
// Implementation (pimpl)
// ---------------------------------------------------------------------------
struct AudioEngine::Impl {
    std::unique_ptr<AudioBackend> backend;

    // Serializes ALL control-thread API calls so they may come from any thread.
    // The audio thread never takes this mutex.
    std::mutex controlMutex;

    // Cross-thread scalar state — atomic so the audio thread / queries read it
    // without the mutex and without UB.
    std::atomic<bool>         running{false};
    std::atomic<unsigned int> sampleRate{kDefaultSampleRate};
    std::atomic<unsigned int> channels{kDefaultOutChannels};

    bool              suspended = false;   // control thread only (under controlMutex)
    AudioStreamConfig startConfig{};       // remembered for resume()

    // Sound bank — control thread only (under controlMutex). unique_ptr gives
    // stable addresses so the audio thread can hold raw pointers into it safely.
    std::vector<std::unique_ptr<AudioBuffer>> sounds;
    // Retired-but-still-referenced buffers; freed on stop() (RT thread joined).
    std::vector<std::unique_ptr<AudioBuffer>> retired;

    // Voice pool — audio thread only.
    std::array<Voice, kMaxVoices> voices{};
    float master = 1.0f;                          // audio thread only

    // Control -> audio command channel + audio/control -> poll event channels.
    SpscQueue<Command, kCommandQueueCap>     commands;
    SpscQueue<EngineEvent, kEventQueueCap>   audioEvents;
    SpscQueue<EngineEvent, kControlEventCap> controlEvents;
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
                slot->position = 0.0;
                slot->gain     = cmd.gain;
                slot->panL     = cmd.panL;   // precomputed on the control thread
                slot->panR     = cmd.panR;
                slot->pitch    = cmd.pitch;
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
                        v.panL = cmd.panL; v.panR = cmd.panR; break;
                    }
                }
                break;
            case CommandType::SetPitch:
                for (Voice& v : voices) {
                    if (v.active && v.id == cmd.voice) { v.pitch = cmd.pitch; break; }
                }
                break;
            case CommandType::SetMaster:
                master = cmd.gain;
                break;
            }
        }
    }

    void mix(float* out, unsigned int nFrames) {
        const unsigned int outCh = channels.load(std::memory_order_relaxed);
        std::memset(out, 0, sizeof(float) * nFrames * outCh);

        const unsigned int rate = sampleRate.load(std::memory_order_relaxed);

        for (Voice& v : voices) {
            if (!v.active || v.buffer == nullptr) continue;

            const AudioBuffer&  buf    = *v.buffer;
            const std::size_t   frames = buf.frameCount();
            if (frames == 0) { v.active = false; continue; }
            const std::uint32_t srcCh  = buf.channels;

            // Source frames advanced per output frame: resample to the device
            // rate and apply pitch. step == 1.0 when the rates match and pitch
            // is 1.0, so matched-rate audio is read sample-for-sample.
            const double denom = rate ? static_cast<double>(rate)
                                      : static_cast<double>(buf.sampleRate);
            const double step  = (static_cast<double>(buf.sampleRate) / denom) * v.pitch;

            for (unsigned int f = 0; f < nFrames; ++f) {
                if (v.position >= static_cast<double>(frames)) {
                    if (v.loop) {
                        do { v.position -= static_cast<double>(frames); }
                        while (v.position >= static_cast<double>(frames));
                    } else {
                        v.active = false;
                        pushEvent({EngineEvent::Kind::VoiceFinished, v.id,
                                   static_cast<std::uint32_t>(VoiceEndReason::Natural), 0});
                        break;
                    }
                }

                // Linear interpolation between source frames i0 and i1.
                const std::size_t i0   = static_cast<std::size_t>(v.position);
                const float       frac = static_cast<float>(v.position - static_cast<double>(i0));
                std::size_t       i1   = i0 + 1;
                if (i1 >= frames) i1 = v.loop ? 0 : i0;

                const float* a = &buf.samples[i0 * srcCh];
                const float* b = &buf.samples[i1 * srcCh];
                const float  l = a[0] + (b[0] - a[0]) * frac;                  // mono / left
                const float  r = (srcCh == 1) ? l : (a[1] + (b[1] - a[1]) * frac);

                if (outCh == 1) {
                    out[f] += 0.5f * (l * v.panL + r * v.panR) * v.gain; // downmix
                } else {
                    out[f * outCh + 0] += l * v.gain * v.panL;
                    out[f * outCh + 1] += r * v.gain * v.panR;
                }
                v.position += step;
            }
        }

        if (master != 1.0f) {
            const std::size_t total = static_cast<std::size_t>(nFrames) * outCh;
            for (std::size_t i = 0; i < total; ++i) out[i] *= master;
        }
    }

    static void render(float* out, unsigned int frames, void* user) {
        auto* self = static_cast<Impl*>(user);
        self->drainCommands();
        self->mix(out, frames);
    }

    bool pushCommand(const Command& cmd) { return commands.push(cmd); }
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
// Public API (all control methods serialized by impl_->controlMutex)
// ---------------------------------------------------------------------------
AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}

AudioEngine::~AudioEngine() { stop(); }

bool AudioEngine::start(unsigned int sampleRate, unsigned int bufferFrames,
                        BackendType backendType) {
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    if (impl_->running.load(std::memory_order_relaxed)) return true;

    impl_->backend = createAudioBackend(backendType);
    if (!impl_->backend) {
        std::fprintf(stderr, "[rope] requested audio backend is not available\n");
        return false;
    }

    AudioStreamConfig config;
    config.sampleRate   = sampleRate ? sampleRate : kDefaultSampleRate;
    config.bufferFrames = bufferFrames;
    config.channels     = impl_->channels.load(std::memory_order_relaxed);

    if (!impl_->backend->start(config, &Impl::render, impl_.get())) {
        std::fprintf(stderr, "[rope] failed to start audio backend\n");
        impl_->backend.reset();
        return false;
    }

    impl_->startConfig = config;
    impl_->sampleRate.store(impl_->backend->sampleRate(), std::memory_order_relaxed);
    impl_->channels.store(impl_->backend->channels(), std::memory_order_relaxed);
    impl_->suspended = false;
    impl_->running.store(true, std::memory_order_relaxed);

    std::printf("[rope] backend: %s | %u Hz | %u ch\n",
                impl_->backend->name(),
                impl_->sampleRate.load(), impl_->channels.load());
    return true;
}

void AudioEngine::stop() {
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    if (!impl_->running.load(std::memory_order_relaxed)) return;
    if (impl_->backend) impl_->backend->stop();   // joins the RT thread
    impl_->backend.reset();
    impl_->running.store(false, std::memory_order_relaxed);
    impl_->suspended = false;
    impl_->retired.clear();   // RT thread is joined; safe to free retired buffers
}

bool AudioEngine::isRunning() const noexcept {
    return impl_->running.load(std::memory_order_relaxed);
}

SoundHandle AudioEngine::loadWav(const std::filesystem::path& path) {
    std::optional<AudioBuffer> decoded = decodeWav(path);
    if (!decoded || decoded->channels == 0) {
        std::fprintf(stderr, "[rope] failed to decode WAV: %s\n", path.string().c_str());
        return kInvalidSound;
    }
    // Any sample rate is fine: the mixer resamples each voice to the device rate.
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    impl_->sounds.push_back(std::make_unique<AudioBuffer>(std::move(*decoded)));
    return static_cast<SoundHandle>(impl_->sounds.size() - 1);
}

SoundHandle AudioEngine::loadWavMemory(const void* data, std::size_t size) {
    std::optional<AudioBuffer> decoded = decodeWav(data, size);
    if (!decoded || decoded->channels == 0) {
        std::fprintf(stderr, "[rope] failed to decode WAV from memory (%zu bytes)\n", size);
        return kInvalidSound;
    }
    // Any sample rate is fine: the mixer resamples each voice to the device rate.
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    impl_->sounds.push_back(std::make_unique<AudioBuffer>(std::move(*decoded)));
    return static_cast<SoundHandle>(impl_->sounds.size() - 1);
}

bool AudioEngine::unloadSound(SoundHandle sound) {
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    if (sound >= impl_->sounds.size() || !impl_->sounds[sound]) return false;
    // Move the buffer aside instead of freeing: voices may still reference it.
    // It is reclaimed on stop(), when the audio thread is guaranteed joined.
    impl_->retired.push_back(std::move(impl_->sounds[sound]));
    impl_->sounds[sound].reset();
    return true;
}

VoiceHandle AudioEngine::play(SoundHandle sound, const PlayParams& params) {
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    if (sound >= impl_->sounds.size() || !impl_->sounds[sound]) return kInvalidVoice;

    const VoiceHandle id =
        impl_->nextVoiceId.fetch_add(1, std::memory_order_relaxed);

    Command cmd;
    cmd.type   = CommandType::Play;
    cmd.buffer = impl_->sounds[sound].get();
    cmd.voice  = id;
    cmd.gain   = params.gain;
    computePan(params.pan, cmd.panL, cmd.panR);   // off the audio thread
    cmd.pitch  = clampPitch(params.pitch);
    cmd.loop   = params.loop;

    if (!impl_->pushCommand(cmd)) {
        std::fprintf(stderr, "[rope] command queue full; play() dropped\n");
        return kInvalidVoice;
    }
    return id;
}

bool AudioEngine::stopVoice(VoiceHandle voice) {
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    Command cmd;
    cmd.type  = CommandType::Stop;
    cmd.voice = voice;
    return impl_->pushCommand(cmd);
}

bool AudioEngine::stopAll() {
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    Command cmd;
    cmd.type = CommandType::StopAll;
    return impl_->pushCommand(cmd);
}

bool AudioEngine::setVoiceGain(VoiceHandle voice, float gain) {
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    Command cmd;
    cmd.type  = CommandType::SetGain;
    cmd.voice = voice;
    cmd.gain  = gain;
    return impl_->pushCommand(cmd);
}

bool AudioEngine::setVoicePan(VoiceHandle voice, float pan) {
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    Command cmd;
    cmd.type  = CommandType::SetPan;
    cmd.voice = voice;
    computePan(pan, cmd.panL, cmd.panR);          // off the audio thread
    return impl_->pushCommand(cmd);
}

bool AudioEngine::setVoicePitch(VoiceHandle voice, float pitch) {
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    Command cmd;
    cmd.type  = CommandType::SetPitch;
    cmd.voice = voice;
    cmd.pitch = clampPitch(pitch);
    return impl_->pushCommand(cmd);
}

bool AudioEngine::setMasterVolume(float gain) {
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    impl_->masterShadow.store(gain, std::memory_order_relaxed);
    Command cmd;
    cmd.type = CommandType::SetMaster;
    cmd.gain = gain;
    return impl_->pushCommand(cmd);
}

float AudioEngine::masterVolume() const noexcept {
    return impl_->masterShadow.load(std::memory_order_relaxed);
}

bool AudioEngine::pollEvent(Event& out) {
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
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
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    if (!impl_->running.load(std::memory_order_relaxed) || impl_->suspended) return;
    if (impl_->backend) impl_->backend->stop();   // joins the RT thread
    impl_->suspended = true;
    impl_->controlEvents.push({EngineEvent::Kind::Suspended, kInvalidVoice, 0, 0});
}

void AudioEngine::resume() {
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    if (!impl_->running.load(std::memory_order_relaxed) || !impl_->suspended) return;
    if (impl_->backend &&
        impl_->backend->start(impl_->startConfig, &Impl::render, impl_.get())) {
        impl_->sampleRate.store(impl_->backend->sampleRate(), std::memory_order_relaxed);
        impl_->channels.store(impl_->backend->channels(), std::memory_order_relaxed);
        impl_->suspended = false;
        impl_->controlEvents.push({EngineEvent::Kind::Resumed, kInvalidVoice, 0, 0});
    } else {
        // Stay suspended so the host can retry; do NOT emit a false Resumed.
        std::fprintf(stderr,
            "[rope] resume(): failed to restart the audio device; staying suspended\n");
    }
}

unsigned int AudioEngine::sampleRate() const noexcept {
    return impl_->sampleRate.load(std::memory_order_relaxed);
}
unsigned int AudioEngine::outputChannels() const noexcept {
    return impl_->channels.load(std::memory_order_relaxed);
}

void AudioEngine::renderOffline(float* out, unsigned int nFrames) {
    // Offline path: the caller drives the mixer directly (no RT thread). Single-
    // threaded use is assumed, so no controlMutex is taken here.
    impl_->drainCommands();
    impl_->mix(out, nFrames);
}

} // namespace rope
