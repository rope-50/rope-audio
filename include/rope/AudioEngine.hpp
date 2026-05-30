#pragma once

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
    bool  loop = false;  ///< restart from the top when the end is reached
};

/// Real-time audio engine: opens one output stream and mixes any number of
/// simultaneously-playing WAV voices on the audio thread.
///
/// Threading contract:
///   * Construct/start/stop and all asset/playback calls happen on a single
///     "control thread" (typically your game/main thread).
///   * The audio callback runs on a separate real-time thread owned by RtAudio.
///   * The two communicate through a lock-free queue — the audio thread never
///     locks or allocates.
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&)            = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&&)                 = delete;
    AudioEngine& operator=(AudioEngine&&)      = delete;

    // ---- Lifecycle (control thread) ----

    /// Open the default output device and start streaming.
    /// @param sampleRate   preferred rate in Hz (0 = engine default, 48000)
    /// @param bufferFrames preferred callback buffer size (0 = engine default)
    /// @return true on success.
    bool start(unsigned int sampleRate = 48000, unsigned int bufferFrames = 512);

    /// Stop streaming and close the device. Safe to call multiple times.
    void stop();

    [[nodiscard]] bool isRunning() const noexcept;

    // ---- Asset management (control thread) ----

    /// Decode a WAV and add it to the engine's sound bank.
    /// @return a SoundHandle, or kInvalidSound on failure.
    SoundHandle loadWav(const std::filesystem::path& path);

    // ---- Playback (control thread) ----

    /// Start a new voice playing the given sound. Returns immediately;
    /// the voice begins on the next audio callback.
    /// @return a VoiceHandle, or kInvalidVoice if the command could not be queued.
    VoiceHandle play(SoundHandle sound, const PlayParams& params = {});

    /// Stop a specific voice (no-op if it already finished).
    void stopVoice(VoiceHandle voice);

    /// Stop every currently-playing voice.
    void stopAll();

    // ---- Queries ----

    [[nodiscard]] unsigned int sampleRate() const noexcept;
    [[nodiscard]] unsigned int outputChannels() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rope
