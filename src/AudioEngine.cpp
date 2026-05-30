#include "rope/AudioEngine.hpp"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace rope {
namespace {

constexpr std::size_t kMaxVoices            = 64;   ///< polyphony limit
constexpr std::size_t kCommandQueueCap      = 256;  ///< pending control->audio commands
constexpr unsigned int kDefaultSampleRate   = 48000;
constexpr unsigned int kDefaultOutChannels  = 2;    ///< requested stereo output

// --- Control -> audio thread messages --------------------------------------
enum class CommandType { Play, Stop, StopAll };

struct Command {
    CommandType        type{};
    const AudioBuffer* buffer = nullptr; // Play only
    VoiceHandle        voice  = kInvalidVoice;
    float              gain   = 1.0f;
    bool               loop   = false;
};

/// Minimal single-producer / single-consumer lock-free ring buffer.
/// Producer = control thread, consumer = audio thread.
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
    bool         running    = false;
    unsigned int sampleRate = kDefaultSampleRate;
    unsigned int channels   = kDefaultOutChannels;

    // Sound bank — control thread only. unique_ptr gives stable addresses so the
    // audio thread can hold raw pointers into it safely.
    std::vector<std::unique_ptr<AudioBuffer>> sounds;

    // Voice pool — audio thread only.
    std::array<Voice, kMaxVoices> voices{};

    // Control -> audio command channel.
    SpscQueue<Command, kCommandQueueCap> commands;

    std::atomic<VoiceHandle> nextVoiceId{1};

    // ---- audio thread ----

    void drainCommands() {
        Command cmd;
        while (commands.pop(cmd)) {
            switch (cmd.type) {
            case CommandType::Play: {
                Voice* slot = nullptr;
                for (Voice& v : voices) {
                    if (!v.active) { slot = &v; break; }
                }
                if (!slot) break; // polyphony exhausted; drop the note
                slot->buffer   = cmd.buffer;
                slot->position = 0;
                slot->gain     = cmd.gain;
                slot->loop     = cmd.loop;
                slot->id       = cmd.voice;
                slot->active   = true;
                break;
            }
            case CommandType::Stop:
                for (Voice& v : voices) {
                    if (v.active && v.id == cmd.voice) { v.active = false; break; }
                }
                break;
            case CommandType::StopAll:
                for (Voice& v : voices) { v.active = false; }
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
                    if (v.loop) { v.position = 0; }
                    else        { v.active = false; break; }
                }
                const float* src = &buf.samples[v.position * srcCh];
                // Mono source -> both ears; stereo+ -> first two channels.
                const float l = src[0];
                const float r = (srcCh == 1) ? src[0] : src[1];

                if (outCh == 1) {
                    out[f] += 0.5f * (l + r) * v.gain;       // downmix to mono
                } else {
                    out[f * outCh + 0] += l * v.gain;
                    out[f * outCh + 1] += r * v.gain;
                    // Output channels beyond stereo are left silent for now.
                }
                ++v.position;
            }
        }
        // NOTE: summed voices can exceed [-1, 1]. A production engine would apply
        // a master limiter / soft-clip here; left out so the boilerplate stays
        // transparent.
    }

    // RenderCallback invoked by whichever backend is active, on its RT thread.
    static void render(float* out, unsigned int frames, void* user) {
        auto* self = static_cast<Impl*>(user);
        self->drainCommands();
        self->mix(out, frames);
    }
};

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
    impl_->sampleRate = impl_->backend->sampleRate();
    impl_->channels   = impl_->backend->channels();
    impl_->running    = true;

    std::printf("[rope] backend: %s | %u Hz | %u ch\n",
                impl_->backend->name(), impl_->sampleRate, impl_->channels);
    return true;
}

void AudioEngine::stop() {
    if (!impl_->running) return;
    if (impl_->backend) impl_->backend->stop();
    impl_->backend.reset();
    impl_->running = false;
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

VoiceHandle AudioEngine::play(SoundHandle sound, const PlayParams& params) {
    if (sound >= impl_->sounds.size()) return kInvalidVoice;

    const VoiceHandle id =
        impl_->nextVoiceId.fetch_add(1, std::memory_order_relaxed);

    Command cmd;
    cmd.type   = CommandType::Play;
    cmd.buffer = impl_->sounds[sound].get();
    cmd.voice  = id;
    cmd.gain   = params.gain;
    cmd.loop   = params.loop;

    if (!impl_->commands.push(cmd)) {
        std::fprintf(stderr, "[rope] command queue full; play() dropped\n");
        return kInvalidVoice;
    }
    return id;
}

void AudioEngine::stopVoice(VoiceHandle voice) {
    Command cmd;
    cmd.type  = CommandType::Stop;
    cmd.voice = voice;
    impl_->commands.push(cmd);
}

void AudioEngine::stopAll() {
    Command cmd;
    cmd.type = CommandType::StopAll;
    impl_->commands.push(cmd);
}

unsigned int AudioEngine::sampleRate() const noexcept { return impl_->sampleRate; }
unsigned int AudioEngine::outputChannels() const noexcept { return impl_->channels; }

} // namespace rope
