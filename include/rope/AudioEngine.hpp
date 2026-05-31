#pragma once

#include "rope/AudioBackend.hpp"
#include "rope/AudioBuffer.hpp"
#include "rope/WavLoader.hpp"

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

/// Per-voice playback options.
struct PlayParams {
    float gain = 1.0f;   ///< linear gain multiplier
    float pan  = 0.0f;   ///< -1 = full left, 0 = center, +1 = full right
    bool  loop = false;  ///< restart from the top when the end is reached
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
    /// @return false if the command queue was full (try again next frame).
    bool stopVoice(VoiceHandle voice);

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

    /// Set the master output gain applied to the whole mix.
    /// @return false if the command queue was full.
    bool setMasterVolume(float gain);

    [[nodiscard]] float masterVolume() const noexcept;

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
