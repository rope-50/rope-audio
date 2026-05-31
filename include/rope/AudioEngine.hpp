#pragma once

#include "rope/AudioBackend.hpp"
#include "rope/AudioBuffer.hpp"
#include "rope/WavLoader.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace rope {

/// Handle to a sound loaded into the engine's bank (see AudioEngine::loadWav).
using SoundHandle = std::uint32_t;

/// Handle to an individual playing instance (see AudioEngine::play).
using VoiceHandle = std::uint64_t;

inline constexpr SoundHandle kInvalidSound = 0xFFFFFFFFu;
inline constexpr VoiceHandle kInvalidVoice = 0;

/// Category bus a voice is routed through. Each bus has its own group volume
/// applied after the per-voice gain/pan and before the master — the usual game
/// mix split (separate SFX / Music / UI sliders).
enum class Bus : std::uint32_t { Sfx = 0, Music = 1, Ui = 2 };

/// Number of category buses.
inline constexpr std::size_t kBusCount = 3;

/// Per-voice resampling quality.
///   * Linear  — cheap 2-point interpolation (default).
///   * Sinc    — band-limited windowed-sinc; higher quality and anti-aliased on
///               downsampling / pitch-up, at a higher CPU cost.
enum class ResampleQuality { Linear, Sinc };

/// Per-voice playback options.
struct PlayParams {
    float gain   = 1.0f;  ///< linear gain multiplier
    float pan    = 0.0f;  ///< -1 = full left, 0 = center, +1 = full right
    bool  loop   = false; ///< restart from the top when the end is reached
    float pitch  = 1.0f;  ///< speed/pitch ratio (1 = original, 2 = +1 octave, 0.5 = -1 octave)
    float fadeIn = 0.0f;  ///< fade-in time in seconds (0 = start at full gain)
    Bus   bus    = Bus::Sfx; ///< category bus this voice is routed through
    /// One-pole low-pass cutoff in Hz for muffling (distance / occlusion /
    /// underwater). 0 (default) or >= Nyquist disables the filter.
    float lowpassHz = 0.0f;
    /// Absolute output-frame time at which the voice should start, on the
    /// engine's monotonic sample clock (see AudioEngine::currentFrame). 0 (the
    /// default) or any time already in the past means "start immediately". Use
    /// `currentFrame() + N` to start the voice sample-accurately N frames ahead.
    std::uint64_t startFrame = 0;
};

/// Reason a voice stopped (carried by Event::reason).
enum class VoiceEndReason { Natural, Stopped, Stolen };

/// Type of an engine -> app notification (see AudioEngine::pollEvent).
enum class EventType {
    None,
    VoiceFinished,    ///< a voice stopped producing audio (see voice, reason)
    VoicesExhausted,  ///< a play() was dropped because the voice pool was full
    QueueOverflow,    ///< one or more events were dropped (see count)
    Suspended,        ///< the audio device was suspended (mobile lifecycle)
    Resumed,          ///< the audio device resumed
};

/// An engine -> app notification, retrieved one at a time via pollEvent().
struct Event {
    EventType      type   = EventType::None;
    VoiceHandle    voice  = kInvalidVoice;        ///< valid for VoiceFinished
    VoiceEndReason reason = VoiceEndReason::Natural;
    std::uint32_t  count  = 0;                     ///< dropped count for QueueOverflow
};

/// Real-time audio engine: opens one output stream and mixes any number of
/// simultaneously-playing WAV voices on the audio thread.
///
/// Threading:
///   * The public API is thread-safe: control calls (load/play/stop/set*/poll/
///     suspend/resume) are serialized by an internal mutex, so they may be made
///     from any thread (e.g. a game's job system).
///   * The audio callback runs on a separate real-time thread. It never takes
///     that mutex, locks, or allocates — control and audio communicate through
///     lock-free queues.
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&)            = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&&)                 = delete;
    AudioEngine& operator=(AudioEngine&&)      = delete;

    // ---- Lifecycle (any thread) ----

    /// Open the default output device and start streaming.
    /// @param sampleRate   preferred rate in Hz (0 = engine default, 48000)
    /// @param bufferFrames preferred callback buffer size (0 = engine default)
    /// @param backend      which platform backend to use (Default = miniaudio)
    /// @return true on success.
    bool start(unsigned int sampleRate   = 48000,
               unsigned int bufferFrames = 512,
               BackendType  backend      = BackendType::Default);

    /// Stop streaming and close the device. Safe to call multiple times.
    void stop();

    [[nodiscard]] bool isRunning() const noexcept;

    // ---- Asset management (any thread) ----

    /// Decode a WAV file and add it to the engine's sound bank.
    /// @return a SoundHandle, or kInvalidSound on failure.
    SoundHandle loadWav(const std::filesystem::path& path);

    /// Decode a WAV from an in-memory buffer (e.g. a bundled/packed asset) and
    /// add it to the sound bank. The data only needs to live for this call.
    /// @return a SoundHandle, or kInvalidSound on failure.
    SoundHandle loadWavMemory(const void* data, std::size_t size);

    /// Retire a sound: subsequent play() on this handle fails, and the decoded
    /// buffer is freed on the next stop(). Voices already playing it keep going
    /// until they finish (the buffer stays alive until then).
    /// @return false if the handle is invalid or already retired.
    bool unloadSound(SoundHandle sound);

    // ---- Playback (any thread) ----

    /// Start a new voice playing the given sound. Returns immediately;
    /// the voice begins on the next audio callback.
    /// @return a VoiceHandle, or kInvalidVoice if the command could not be queued.
    VoiceHandle play(SoundHandle sound, const PlayParams& params = {});

    /// Stop a specific voice (no-op if it already finished).
    /// @param fadeOut fade-out time in seconds (0 = stop immediately). With a
    ///                fade, the voice keeps playing while ramping to silence and
    ///                emits VoiceFinished when the fade completes.
    /// @return false if the command queue was full (try again next frame).
    bool stopVoice(VoiceHandle voice, float fadeOut = 0.0f);

    /// Stop every currently-playing voice.
    /// @return false if the command queue was full.
    bool stopAll();

    // ---- Live mix control (any thread) ----

    /// Change a playing voice's gain (no-op if it already finished).
    /// @return false if the command queue was full.
    bool setVoiceGain(VoiceHandle voice, float gain);

    /// Change a playing voice's pan in [-1, 1] (no-op if it already finished).
    /// @return false if the command queue was full.
    bool setVoicePan(VoiceHandle voice, float pan);

    /// Change a playing voice's pitch/speed ratio (no-op if it already finished).
    /// @return false if the command queue was full.
    bool setVoicePitch(VoiceHandle voice, float pitch);

    /// Set a voice's one-pole low-pass cutoff in Hz (muffling). 0 or >= Nyquist
    /// disables the filter. The filter state is continuous, so changes are
    /// click-free (no-op if the voice already finished).
    /// @return false if the command queue was full.
    bool setVoiceLowpass(VoiceHandle voice, float cutoffHz);

    /// Set the master output gain applied to the whole mix.
    /// @return false if the command queue was full.
    bool setMasterVolume(float gain);

    [[nodiscard]] float masterVolume() const noexcept;

    /// Set the group volume for a category bus (SFX / Music / UI). Applied to
    /// every voice routed through that bus, after per-voice gain and before the
    /// master. Smoothed (~5 ms) to avoid zipper noise.
    /// @return false if the command queue was full.
    bool setBusVolume(Bus bus, float gain);

    [[nodiscard]] float busVolume(Bus bus) const noexcept;

    /// Mute/unmute a category bus. A muted bus is silent regardless of its
    /// volume. Smoothed (~5 ms) to avoid clicks.
    /// @return false if the command queue was full.
    bool setBusMuted(Bus bus, bool muted);
    [[nodiscard]] bool busMuted(Bus bus) const noexcept;

    /// Solo/unsolo a category bus. While any bus is soloed, only soloed buses
    /// are audible (a muted bus stays silent even if soloed). Smoothed (~5 ms).
    /// @return false if the command queue was full.
    bool setBusSoloed(Bus bus, bool soloed);
    [[nodiscard]] bool busSoloed(Bus bus) const noexcept;

    /// Enable/disable the master-bus soft-clip limiter (on by default).
    /// Transparent below ~0.7, then smoothly limits peaks to +-1.0 — prevents
    /// harsh digital clipping when many voices sum hot.
    void setMasterLimiterEnabled(bool enabled);
    [[nodiscard]] bool masterLimiterEnabled() const noexcept;

    /// Choose the resampling quality used for all voices (default Linear).
    /// Sinc is band-limited and anti-aliased on pitch-up/downsampling; it costs
    /// more CPU. Safe to change at any time.
    void setResampleQuality(ResampleQuality quality);
    [[nodiscard]] ResampleQuality resampleQuality() const noexcept;

    // ---- Events (any thread) ----

    /// Retrieve the next pending engine event. Call repeatedly until it returns
    /// false (typically once per frame). The audio thread never calls back into
    /// your code — events are delivered only through this poll.
    /// @return true if an event was written to @p out, false if none pending.
    bool pollEvent(Event& out);

    // ---- Mobile lifecycle (any thread) ----

    /// Suspend the audio device (e.g. on app background / audio-focus loss).
    /// Voices and loaded sounds are preserved; emits an Suspended event.
    void suspend();

    /// Resume after suspend(); emits a Resumed event. The device may renegotiate
    /// its rate/channels, so re-query sampleRate()/outputChannels() afterwards.
    void resume();

    // ---- Queries ----

    [[nodiscard]] unsigned int sampleRate() const noexcept;
    [[nodiscard]] unsigned int outputChannels() const noexcept;

    /// The engine's monotonic output-frame clock: the number of frames the mixer
    /// has produced since start(). Read it to schedule sample-accurate playback,
    /// e.g. `play({.startFrame = currentFrame() + sampleRate()/2})` to start a
    /// voice ~half a second from now. Advances on the audio thread; the value
    /// read lags real "now" by up to one device buffer, so schedule a little
    /// ahead. Resets to 0 on start()/stop().
    [[nodiscard]] std::uint64_t currentFrame() const noexcept;

    /// Number of sounds currently holding decoded data in the bank (a memory/
    /// asset stat). A sound retired via unloadSound() while still feeding a
    /// voice keeps counting until that voice ends and its buffer is reclaimed.
    [[nodiscard]] std::size_t soundCount() const noexcept;

    // ---- Offline rendering (testing / headless) ----

    /// Render @p nFrames of interleaved float output by running the mixer
    /// synchronously on the calling thread. Use ONLY with BackendType::Null
    /// (or an unstarted engine) — never while a real device backend is
    /// streaming, which would race the audio thread. The output buffer must
    /// hold nFrames * outputChannels() floats.
    void renderOffline(float* out, unsigned int nFrames);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rope
