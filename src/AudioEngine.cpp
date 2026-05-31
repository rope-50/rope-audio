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
constexpr std::size_t kRetireQueueCap       = 512;  ///< pending voice-retire records
constexpr unsigned int kDefaultSampleRate   = 48000;
constexpr unsigned int kDefaultOutChannels  = 2;    ///< requested stereo output
constexpr std::size_t  kBusBlockPrealloc    = 8192; ///< per-bus per-frame gain scratch (grows if exceeded)
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

constexpr float kSoftClipThreshold = 0.7f;

/// Master-bus soft clipper: transparent below ±threshold, then a smooth knee
/// that asymptotes to ±1.0 — prevents harsh digital clipping when many voices
/// sum hot, without a hard edge. Stateless and RT-safe (tanh only on peaks).
inline float softClip(float x) {
    const float a = x < 0.0f ? -x : x;
    if (a <= kSoftClipThreshold) return x;
    const float t    = kSoftClipThreshold;
    const float over = (a - t) / (1.0f - t);
    const float y    = t + (1.0f - t) * std::tanh(over);
    return x < 0.0f ? -y : y;
}

constexpr float kSmoothSeconds = 0.005f;  // ~5 ms anti-zipper ramp for set* changes

/// A linear parameter ramp (current -> target over N frames). Kills zipper
/// noise on gain/pan/master changes and powers fade in/out. Advanced once per
/// output frame on the audio thread.
struct Ramp {
    float current = 0.0f;
    float target  = 0.0f;
    float inc     = 0.0f;
    int   frames  = 0;     // frames remaining
    void jump(float v) { current = target = v; inc = 0.0f; frames = 0; }
    void to(float t, int rampFrames) {
        target = t;
        if (rampFrames <= 0) { current = t; inc = 0.0f; frames = 0; }
        else { inc = (t - current) / static_cast<float>(rampFrames); frames = rampFrames; }
    }
    float next() {        // value to use this frame, then advance
        const float v = current;
        if (frames > 0) { current += inc; if (--frames == 0) current = target; }
        return v;
    }
};

// --- Control -> audio thread messages --------------------------------------
enum class CommandType { Play, Stop, StopAll, SetGain, SetPan, SetPitch, SetMaster,
                         SetBus, SetBusMute, SetBusSolo };

struct Command {
    CommandType        type{};
    const AudioBuffer* buffer = nullptr;          // Play only
    VoiceHandle        voice  = kInvalidVoice;
    std::uint32_t      soundSlot = 0;             // Play only (-> sound bank index)
    float              gain   = 1.0f;             // Play / SetGain / SetMaster / SetBus
    float              panL   = kCenterPanGain;   // Play / SetPan (precomputed)
    float              panR   = kCenterPanGain;
    float              pitch  = 1.0f;             // Play / SetPitch
    std::uint32_t      rampFrames = 0;            // Play fade-in / Stop fade-out / set* smoothing
    std::uint32_t      bus    = 0;                // Play / SetBus (-> category bus index)
    std::uint64_t      startFrame = 0;            // Play: absolute start time on the sample clock
    bool               flag   = false;            // SetBusMute / SetBusSolo value
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
    const AudioBuffer* buffer    = nullptr;
    double             position  = 0.0;   // fractional source frame index (resampling)
    Ramp               gain;              // smoothed; powers fade in/out
    Ramp               panL;
    Ramp               panR;
    float              pitch     = 1.0f;
    std::uint32_t      soundSlot = 0;     // owning sound-bank index (for reclamation)
    int                bus       = 0;     // category bus index [0, kBusCount)
    std::uint64_t      startFrame = 0;    // absolute sample-clock frame to begin at
    bool               loop      = false;
    bool               active    = false;
    bool               stopAtRampEnd = false; // fade-out: finish when gain ramp hits 0
    VoiceHandle        id        = kInvalidVoice;
};

/// A sound-bank slot. The decoded buffer is kept alive while voices reference it
/// (refCount) and freed once refCount reaches 0 after unloadSound (retired).
/// Indices are stable and never reused, so a stale handle resolves to a freed
/// slot and fails cleanly (no aliasing).
struct SoundSlot {
    std::unique_ptr<AudioBuffer> buffer;
    std::uint32_t                refCount = 0;     // in-flight + playing voices
    bool                         retired  = false; // unloadSound called
};

} // namespace

// ---------------------------------------------------------------------------
// Implementation (pimpl)
// ---------------------------------------------------------------------------
struct AudioEngine::Impl {
    std::unique_ptr<AudioBackend> backend;

    // Serializes ALL control-thread API calls so they may come from any thread.
    // The audio thread never takes this mutex. (mutable for const queries.)
    mutable std::mutex controlMutex;

    // Cross-thread scalar state — atomic so the audio thread / queries read it
    // without the mutex and without UB.
    std::atomic<bool>         running{false};
    std::atomic<unsigned int> sampleRate{kDefaultSampleRate};
    std::atomic<unsigned int> channels{kDefaultOutChannels};

    bool              suspended = false;   // control thread only (under controlMutex)
    AudioStreamConfig startConfig{};       // remembered for resume()

    // Sound bank — control thread only (under controlMutex). unique_ptr gives
    // stable addresses so the audio thread can hold raw pointers into it safely.
    std::vector<SoundSlot> sounds;

    // Voice pool — audio thread only.
    std::array<Voice, kMaxVoices> voices{};
    Ramp master{1.0f, 1.0f, 0.0f, 0};             // audio thread only (smoothed)
    std::atomic<bool> limiterEnabled{true};       // master-bus soft clip

    // Monotonic output-frame clock for sample-accurate scheduling. framePos is
    // the absolute frame index of the next block, advanced on the audio thread;
    // frameClock mirrors it for the control thread to read (currentFrame()).
    std::uint64_t              framePos = 0;       // audio thread only
    std::atomic<std::uint64_t> frameClock{0};      // control-thread mirror

    // Category buses (SFX/Music/UI): smoothed group gains applied per-frame after
    // the per-voice gain/pan and before the master. busBlock holds the precomputed
    // per-frame gain for the current callback — one pass advances each bus ramp
    // exactly once per frame, shared by every voice routed to that bus.
    std::array<Ramp, kBusCount>               busGain{};   // audio thread only
    std::array<std::vector<float>, kBusCount> busBlock{};  // per-callback scratch
    std::array<std::atomic<float>, kBusCount> busShadow{}; // control-side mirror

    // Per-bus mute/solo. busMuted/busSoloed are the audio-thread truth (set via
    // command); busAudible is the smoothed 0..1 factor folded into the bus gain;
    // the *Shadow atomics back the control-thread getters.
    std::array<bool, kBusCount>              busMuted{};
    std::array<bool, kBusCount>              busSoloed{};
    std::array<Ramp, kBusCount>              busAudible{}; // audio thread only
    std::array<std::atomic<bool>, kBusCount> busMuteShadow{};
    std::array<std::atomic<bool>, kBusCount> busSoloShadow{};

    Impl() {
        for (std::size_t i = 0; i < kBusCount; ++i) {
            busGain[i].jump(1.0f);
            busAudible[i].jump(1.0f);
            busShadow[i].store(1.0f, std::memory_order_relaxed);
            busMuteShadow[i].store(false, std::memory_order_relaxed);
            busSoloShadow[i].store(false, std::memory_order_relaxed);
            busBlock[i].assign(kBusBlockPrealloc, 1.0f);  // preallocate (RT-safe steady state)
        }
    }

    // Recompute each bus's audible factor (audio thread) after a mute/solo change.
    // DAW-style solo: while any bus is soloed, only soloed buses pass; a muted bus
    // is always silent. Smoothed over rampFrames to avoid clicks.
    void recomputeAudible(int rampFrames) {
        bool anySolo = false;
        for (std::size_t b = 0; b < kBusCount; ++b)
            if (busSoloed[b]) { anySolo = true; break; }
        for (std::size_t b = 0; b < kBusCount; ++b) {
            const float target = busMuted[b]                 ? 0.0f
                               : (anySolo && !busSoloed[b])  ? 0.0f
                                                             : 1.0f;
            busAudible[b].to(target, rampFrames);
        }
    }

    // Control -> audio command channel + audio/control -> poll event channels.
    SpscQueue<Command, kCommandQueueCap>      commands;
    SpscQueue<EngineEvent, kEventQueueCap>    audioEvents;
    SpscQueue<EngineEvent, kControlEventCap>  controlEvents;
    // audio -> control: a finished/dropped voice's owning sound slot, so the
    // control thread can drop a refcount and reclaim retired buffers live.
    SpscQueue<std::uint32_t, kRetireQueueCap> retireQueue;
    std::atomic<std::uint32_t> droppedEvents{0};

    std::atomic<VoiceHandle>   nextVoiceId{1};
    std::atomic<float>         masterShadow{1.0f}; // control-side mirror of master

    // ---- audio thread ----

    void pushEvent(const EngineEvent& e) {
        if (!audioEvents.push(e)) {
            droppedEvents.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Deactivate a voice, notify the app, and hand its sound slot back to the
    // control thread for refcount/reclamation. RT-safe (lock-free pushes).
    void finish(Voice& v, VoiceEndReason reason) {
        v.active = false;
        pushEvent({EngineEvent::Kind::VoiceFinished, v.id,
                   static_cast<std::uint32_t>(reason), 0});
        retireQueue.push(v.soundSlot);   // best-effort; if dropped, reclaimed on stop()
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
                    // Polyphony exhausted: the voice never starts, so release the
                    // refcount that play() took for it.
                    pushEvent({EngineEvent::Kind::VoicesExhausted, kInvalidVoice, 0, 0});
                    retireQueue.push(cmd.soundSlot);
                    break;
                }
                slot->buffer    = cmd.buffer;
                slot->position  = 0.0;
                if (cmd.rampFrames > 0) {                 // fade-in: gain 0 -> target
                    slot->gain.current = 0.0f;
                    slot->gain.to(cmd.gain, static_cast<int>(cmd.rampFrames));
                } else {
                    slot->gain.jump(cmd.gain);
                }
                slot->panL.jump(cmd.panL);                // precomputed on the control thread
                slot->panR.jump(cmd.panR);
                slot->pitch     = cmd.pitch;
                slot->soundSlot = cmd.soundSlot;
                slot->bus       = (cmd.bus < kBusCount) ? static_cast<int>(cmd.bus) : 0;
                slot->startFrame = cmd.startFrame;
                slot->loop      = cmd.loop;
                slot->stopAtRampEnd = false;
                slot->id        = cmd.voice;
                slot->active    = true;
                break;
            }
            case CommandType::Stop:
                for (Voice& v : voices) {
                    if (v.active && v.id == cmd.voice) {
                        if (cmd.rampFrames > 0) {       // fade out, then finish
                            v.gain.to(0.0f, static_cast<int>(cmd.rampFrames));
                            v.stopAtRampEnd = true;
                        } else {
                            finish(v, VoiceEndReason::Stopped);
                        }
                        break;
                    }
                }
                break;
            case CommandType::StopAll:
                for (Voice& v : voices) {
                    if (v.active) finish(v, VoiceEndReason::Stopped);
                }
                break;
            case CommandType::SetGain:
                for (Voice& v : voices) {
                    if (v.active && v.id == cmd.voice) {
                        v.gain.to(cmd.gain, static_cast<int>(cmd.rampFrames)); break;
                    }
                }
                break;
            case CommandType::SetPan:
                for (Voice& v : voices) {
                    if (v.active && v.id == cmd.voice) {
                        v.panL.to(cmd.panL, static_cast<int>(cmd.rampFrames));
                        v.panR.to(cmd.panR, static_cast<int>(cmd.rampFrames));
                        break;
                    }
                }
                break;
            case CommandType::SetPitch:
                for (Voice& v : voices) {
                    if (v.active && v.id == cmd.voice) { v.pitch = cmd.pitch; break; }
                }
                break;
            case CommandType::SetMaster:
                master.to(cmd.gain, static_cast<int>(cmd.rampFrames));
                break;
            case CommandType::SetBus:
                if (cmd.bus < kBusCount)
                    busGain[cmd.bus].to(cmd.gain, static_cast<int>(cmd.rampFrames));
                break;
            case CommandType::SetBusMute:
                if (cmd.bus < kBusCount) {
                    busMuted[cmd.bus] = cmd.flag;
                    recomputeAudible(static_cast<int>(cmd.rampFrames));
                }
                break;
            case CommandType::SetBusSolo:
                if (cmd.bus < kBusCount) {
                    busSoloed[cmd.bus] = cmd.flag;
                    recomputeAudible(static_cast<int>(cmd.rampFrames));
                }
                break;
            }
        }
    }

    void mix(float* out, unsigned int nFrames) {
        const unsigned int outCh = channels.load(std::memory_order_relaxed);
        std::memset(out, 0, sizeof(float) * nFrames * outCh);

        const unsigned int rate = sampleRate.load(std::memory_order_relaxed);
        const std::uint64_t blockStart = framePos;   // absolute frame index of out[0]

        // Precompute each bus's smoothed per-frame gain for this block. A single
        // pass advances each bus ramp exactly once per frame; voices on the bus
        // then just index it (so a shared ramp is never advanced per-voice).
        for (std::size_t b = 0; b < kBusCount; ++b) {
            if (busBlock[b].size() < nFrames) busBlock[b].resize(nFrames); // offline overflow only
            float* bg = busBlock[b].data();
            for (unsigned int f = 0; f < nFrames; ++f)
                bg[f] = busGain[b].next() * busAudible[b].next();   // volume * mute/solo
        }

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

            // Sample-accurate start: voices scheduled in a later block produce
            // nothing yet; one starting inside this block begins at its exact
            // local frame (earlier frames stay silent for this voice).
            unsigned int fStart = 0;
            if (v.startFrame > blockStart) {
                const std::uint64_t delta = v.startFrame - blockStart;
                if (delta >= nFrames) continue;
                fStart = static_cast<unsigned int>(delta);
            }

            for (unsigned int f = fStart; f < nFrames; ++f) {
                if (v.stopAtRampEnd && v.gain.frames == 0) { // fade-out complete
                    finish(v, VoiceEndReason::Stopped);
                    break;
                }
                if (v.position >= static_cast<double>(frames)) {
                    if (v.loop) {
                        do { v.position -= static_cast<double>(frames); }
                        while (v.position >= static_cast<double>(frames));
                    } else {
                        finish(v, VoiceEndReason::Natural);
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

                const float g  = v.gain.next();   // smoothed gain (and fades)
                const float pl = v.panL.next();
                const float pr = v.panR.next();
                const float bg = busBlock[v.bus][f];   // category bus group gain
                if (outCh == 1) {
                    out[f] += 0.5f * (l * pl + r * pr) * g * bg; // downmix
                } else {
                    out[f * outCh + 0] += l * g * pl * bg;
                    out[f * outCh + 1] += r * g * pr * bg;
                }
                v.position += step;
            }
        }

        // Master gain (smoothed) + optional soft-clip limiter over the mix.
        const bool limit = limiterEnabled.load(std::memory_order_relaxed);
        const bool masterActive = master.frames > 0 || master.current != 1.0f;
        if (masterActive || limit) {
            for (unsigned int f = 0; f < nFrames; ++f) {
                const float m = master.next();
                for (unsigned int c = 0; c < outCh; ++c) {
                    float s = out[f * outCh + c] * m;
                    out[f * outCh + c] = limit ? softClip(s) : s;
                }
            }
        }

        // Advance the monotonic sample clock and publish it for currentFrame().
        framePos += nFrames;
        frameClock.store(framePos, std::memory_order_relaxed);
    }

    static void render(float* out, unsigned int frames, void* user) {
        auto* self = static_cast<Impl*>(user);
        self->drainCommands();
        self->mix(out, frames);
    }

    bool pushCommand(const Command& cmd) { return commands.push(cmd); }

    // ---- control thread (called under controlMutex) ----

    // Process finished/dropped voices: drop the sound's refcount and free any
    // retired buffer whose last voice has now gone. Bounds memory for the
    // load/play/unload-per-level pattern (no waiting for stop()).
    void reclaim() {
        std::uint32_t slot;
        while (retireQueue.pop(slot)) {
            if (slot < sounds.size() && sounds[slot].refCount > 0) {
                if (--sounds[slot].refCount == 0 && sounds[slot].retired) {
                    sounds[slot].buffer.reset();
                }
            }
        }
    }

    // Convert a duration in seconds to a frame count at the current device rate.
    std::uint32_t framesForSeconds(float seconds) const {
        if (!(seconds > 0.0f)) return 0;
        return static_cast<std::uint32_t>(
            seconds * static_cast<float>(sampleRate.load(std::memory_order_relaxed)));
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

    impl_->framePos = 0;                                  // reset the sample clock
    impl_->frameClock.store(0, std::memory_order_relaxed);

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

    // The RT thread is joined: flush any in-flight commands (so a later start()
    // can't replay a Play that points at a buffer we are about to free), drop
    // pending retire records, and free every retired buffer (no voices remain).
    Command c;          while (impl_->commands.pop(c)) {}
    std::uint32_t slot; while (impl_->retireQueue.pop(slot)) {}
    for (auto& s : impl_->sounds) {
        s.refCount = 0;
        if (s.retired) { s.buffer.reset(); s.retired = false; }
    }
    impl_->framePos = 0;                                  // reset the sample clock
    impl_->frameClock.store(0, std::memory_order_relaxed);
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
    impl_->reclaim();
    SoundSlot s;
    s.buffer = std::make_unique<AudioBuffer>(std::move(*decoded));
    impl_->sounds.push_back(std::move(s));
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
    impl_->reclaim();
    SoundSlot s;
    s.buffer = std::make_unique<AudioBuffer>(std::move(*decoded));
    impl_->sounds.push_back(std::move(s));
    return static_cast<SoundHandle>(impl_->sounds.size() - 1);
}

bool AudioEngine::unloadSound(SoundHandle sound) {
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    impl_->reclaim();
    if (sound >= impl_->sounds.size()) return false;
    SoundSlot& s = impl_->sounds[sound];
    if (!s.buffer || s.retired) return false;
    s.retired = true;                       // new play() on this handle now fails
    if (s.refCount == 0) s.buffer.reset();  // not in use -> free the decoded data now
    return true;
}

VoiceHandle AudioEngine::play(SoundHandle sound, const PlayParams& params) {
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    impl_->reclaim();
    if (sound >= impl_->sounds.size()) return kInvalidVoice;
    SoundSlot& s = impl_->sounds[sound];
    if (!s.buffer || s.retired) return kInvalidVoice;

    const VoiceHandle id =
        impl_->nextVoiceId.fetch_add(1, std::memory_order_relaxed);

    Command cmd;
    cmd.type      = CommandType::Play;
    cmd.buffer    = s.buffer.get();
    cmd.voice     = id;
    cmd.soundSlot = sound;
    cmd.gain       = params.gain;
    computePan(params.pan, cmd.panL, cmd.panR);   // off the audio thread
    cmd.pitch      = clampPitch(params.pitch);
    cmd.rampFrames = impl_->framesForSeconds(params.fadeIn);   // fade-in
    cmd.bus        = static_cast<std::uint32_t>(params.bus);   // clamped on the audio thread
    cmd.startFrame = params.startFrame;                        // sample-accurate start
    cmd.loop       = params.loop;

    if (!impl_->pushCommand(cmd)) {
        std::fprintf(stderr, "[rope] command queue full; play() dropped\n");
        return kInvalidVoice;
    }
    s.refCount += 1;   // a voice is now in flight for this sound (released on finish)
    return id;
}

bool AudioEngine::stopVoice(VoiceHandle voice, float fadeOut) {
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    Command cmd;
    cmd.type       = CommandType::Stop;
    cmd.voice      = voice;
    cmd.rampFrames = impl_->framesForSeconds(fadeOut);   // 0 = instant stop
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
    cmd.type       = CommandType::SetGain;
    cmd.voice      = voice;
    cmd.gain       = gain;
    cmd.rampFrames = impl_->framesForSeconds(kSmoothSeconds);  // anti-zipper
    return impl_->pushCommand(cmd);
}

bool AudioEngine::setVoicePan(VoiceHandle voice, float pan) {
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    Command cmd;
    cmd.type       = CommandType::SetPan;
    cmd.voice      = voice;
    computePan(pan, cmd.panL, cmd.panR);          // off the audio thread
    cmd.rampFrames = impl_->framesForSeconds(kSmoothSeconds);  // anti-zipper
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
    cmd.type       = CommandType::SetMaster;
    cmd.gain       = gain;
    cmd.rampFrames = impl_->framesForSeconds(kSmoothSeconds);  // anti-zipper
    return impl_->pushCommand(cmd);
}

float AudioEngine::masterVolume() const noexcept {
    return impl_->masterShadow.load(std::memory_order_relaxed);
}

bool AudioEngine::setBusVolume(Bus bus, float gain) {
    const std::size_t idx = static_cast<std::size_t>(bus);
    if (idx >= kBusCount) return false;
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    impl_->busShadow[idx].store(gain, std::memory_order_relaxed);
    Command cmd;
    cmd.type       = CommandType::SetBus;
    cmd.bus        = static_cast<std::uint32_t>(idx);
    cmd.gain       = gain;
    cmd.rampFrames = impl_->framesForSeconds(kSmoothSeconds);  // anti-zipper
    return impl_->pushCommand(cmd);
}

float AudioEngine::busVolume(Bus bus) const noexcept {
    const std::size_t idx = static_cast<std::size_t>(bus);
    if (idx >= kBusCount) return 0.0f;
    return impl_->busShadow[idx].load(std::memory_order_relaxed);
}

bool AudioEngine::setBusMuted(Bus bus, bool muted) {
    const std::size_t idx = static_cast<std::size_t>(bus);
    if (idx >= kBusCount) return false;
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    impl_->busMuteShadow[idx].store(muted, std::memory_order_relaxed);
    Command cmd;
    cmd.type       = CommandType::SetBusMute;
    cmd.bus        = static_cast<std::uint32_t>(idx);
    cmd.flag       = muted;
    cmd.rampFrames = impl_->framesForSeconds(kSmoothSeconds);  // anti-click
    return impl_->pushCommand(cmd);
}

bool AudioEngine::busMuted(Bus bus) const noexcept {
    const std::size_t idx = static_cast<std::size_t>(bus);
    if (idx >= kBusCount) return false;
    return impl_->busMuteShadow[idx].load(std::memory_order_relaxed);
}

bool AudioEngine::setBusSoloed(Bus bus, bool soloed) {
    const std::size_t idx = static_cast<std::size_t>(bus);
    if (idx >= kBusCount) return false;
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    impl_->busSoloShadow[idx].store(soloed, std::memory_order_relaxed);
    Command cmd;
    cmd.type       = CommandType::SetBusSolo;
    cmd.bus        = static_cast<std::uint32_t>(idx);
    cmd.flag       = soloed;
    cmd.rampFrames = impl_->framesForSeconds(kSmoothSeconds);  // anti-click
    return impl_->pushCommand(cmd);
}

bool AudioEngine::busSoloed(Bus bus) const noexcept {
    const std::size_t idx = static_cast<std::size_t>(bus);
    if (idx >= kBusCount) return false;
    return impl_->busSoloShadow[idx].load(std::memory_order_relaxed);
}

void AudioEngine::setMasterLimiterEnabled(bool enabled) {
    impl_->limiterEnabled.store(enabled, std::memory_order_relaxed);
}

bool AudioEngine::masterLimiterEnabled() const noexcept {
    return impl_->limiterEnabled.load(std::memory_order_relaxed);
}

bool AudioEngine::pollEvent(Event& out) {
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    impl_->reclaim();
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

std::size_t AudioEngine::soundCount() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->controlMutex);
    std::size_t n = 0;
    for (const auto& s : impl_->sounds) if (s.buffer) ++n;
    return n;
}

std::uint64_t AudioEngine::currentFrame() const noexcept {
    return impl_->frameClock.load(std::memory_order_relaxed);
}

void AudioEngine::renderOffline(float* out, unsigned int nFrames) {
    // Offline path: the caller drives the mixer directly (no RT thread). Single-
    // threaded use is assumed, so no controlMutex is taken here.
    impl_->drainCommands();
    impl_->mix(out, nFrames);
}

} // namespace rope
