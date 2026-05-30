#pragma once

#include <memory>

namespace rope {

/// Requested stream settings. The backend may negotiate different actual
/// values (mobile devices in particular dictate their own rate/buffer size),
/// so always read back AudioBackend::sampleRate()/channels() after start().
struct AudioStreamConfig {
    unsigned int sampleRate   = 48000; ///< preferred rate in Hz
    unsigned int bufferFrames = 0;     ///< preferred callback size (0 = backend default)
    unsigned int channels     = 2;     ///< output channels
};

/// Called on the backend's real-time thread to fill `output` with `frames`
/// frames of interleaved float32 (frames * channels samples). MUST be
/// real-time safe: no locks, no allocation, no I/O.
using RenderCallback = void (*)(float* output, unsigned int frames, void* user);

/// Abstraction over one platform audio output API. Each backend's only job is
/// to open the default output device and repeatedly pull audio by invoking the
/// RenderCallback. The engine's mixer is the callback, so it stays completely
/// backend-agnostic.
class AudioBackend {
public:
    virtual ~AudioBackend() = default;

    /// Open the default output device and begin streaming.
    virtual bool start(const AudioStreamConfig& config,
                       RenderCallback render, void* user) = 0;

    /// Stop streaming and release the device. Safe to call when not running.
    virtual void stop() = 0;

    [[nodiscard]] virtual bool isRunning() const = 0;

    /// Actual negotiated values (valid after a successful start()).
    [[nodiscard]] virtual unsigned int sampleRate() const = 0;
    [[nodiscard]] virtual unsigned int channels() const = 0;

    /// Human-readable name, e.g. "miniaudio (WASAPI)" or "RtAudio (ASIO)".
    [[nodiscard]] virtual const char* name() const = 0;
};

/// Selects which backend implementation to create.
enum class BackendType {
    Default,      ///< best available for the platform (currently miniaudio)
    Miniaudio,    ///< cross-platform: Windows / macOS / Linux / Android / iOS
    RtAudio,      ///< desktop; auto-selects the native API (WASAPI on Windows)
    RtAudioAsio,  ///< desktop Windows; forces ASIO (low-latency, pro/DAW)
};

/// Create a backend. Returns nullptr if the requested backend was not compiled
/// in (e.g. RtAudio on a mobile build).
std::unique_ptr<AudioBackend> createAudioBackend(BackendType type = BackendType::Default);

} // namespace rope
