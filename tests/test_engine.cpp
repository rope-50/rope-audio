#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "rope/AudioEngine.hpp"
#include "rope/WavLoader.hpp"
#include "wav_util.hpp"

using namespace rope;

namespace {

// Load a constant-valued mono sound of `frames` frames into the engine.
SoundHandle loadConstMono(AudioEngine& e, float value, int frames) {
    std::vector<float> s(static_cast<std::size_t>(frames), value);
    auto wav = test::makeWavPcm16(1, 48000, s);
    return e.loadWavMemory(wav.data(), wav.size());
}

float energy(const std::vector<float>& v) {
    float sum = 0.0f;
    for (float x : v) sum += std::abs(x);
    return sum;
}

// Render `chunk`-frame blocks until voice `v` reports VoiceFinished; returns the
// total output frames rendered, or -1 if it never finished within maxChunks.
int renderUntilFinished(AudioEngine& e, VoiceHandle v, int chunk, int maxChunks) {
    std::vector<float> out(static_cast<std::size_t>(chunk) * 2, 0.0f);
    int rendered = 0;
    for (int i = 0; i < maxChunks; ++i) {
        e.renderOffline(out.data(), static_cast<unsigned int>(chunk));
        rendered += chunk;
        Event ev;
        while (e.pollEvent(ev)) {
            if (ev.type == EventType::VoiceFinished && ev.voice == v) return rendered;
        }
    }
    return -1;
}

} // namespace

TEST(Engine, StartsWithNullBackend) {
    AudioEngine e;
    EXPECT_TRUE(e.start(48000, 0, BackendType::Null));
    EXPECT_TRUE(e.isRunning());
    EXPECT_EQ(e.outputChannels(), 2u);
    EXPECT_EQ(e.sampleRate(), 48000u);
}

TEST(Engine, HardLeftPan) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.5f, 1000);
    ASSERT_NE(s, kInvalidSound);
    e.play(s, PlayParams{.gain = 1.0f, .pan = -1.0f});

    std::vector<float> out(256 * 2, 0.0f);
    e.renderOffline(out.data(), 256);
    EXPECT_NEAR(out[0], 0.5f, 0.02f);   // left has signal
    EXPECT_NEAR(out[1], 0.0f, 0.001f);  // right is silent
}

TEST(Engine, HardRightPan) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.5f, 1000);
    e.play(s, PlayParams{.gain = 1.0f, .pan = 1.0f});

    std::vector<float> out(256 * 2, 0.0f);
    e.renderOffline(out.data(), 256);
    EXPECT_NEAR(out[0], 0.0f, 0.001f);
    EXPECT_NEAR(out[1], 0.5f, 0.02f);
}

TEST(Engine, CenterPanIsConstantPower) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.5f, 1000);
    e.play(s, PlayParams{.gain = 1.0f, .pan = 0.0f});

    std::vector<float> out(64 * 2, 0.0f);
    e.renderOffline(out.data(), 64);
    EXPECT_NEAR(out[0], 0.5f * 0.70710678f, 0.02f);
    EXPECT_NEAR(out[1], 0.5f * 0.70710678f, 0.02f);
}

TEST(Engine, GainAndMasterScaleOutput) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.8f, 100000);
    e.setMasterVolume(0.5f);
    e.play(s, PlayParams{.gain = 0.5f, .pan = -1.0f});

    // Render past the master smoothing ramp (~5 ms = 240 frames) and check a
    // settled frame: L = 0.8 * gain(0.5) * panL(1.0) * master(0.5) = 0.2.
    std::vector<float> out(512 * 2, 0.0f);
    e.renderOffline(out.data(), 512);
    EXPECT_NEAR(out[500 * 2], 0.2f, 0.02f);
    EXPECT_FLOAT_EQ(e.masterVolume(), 0.5f);
}

TEST(Engine, VoiceFinishedEventFires) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.5f, 10);   // very short
    auto v = e.play(s);

    std::vector<float> out(64 * 2, 0.0f);
    e.renderOffline(out.data(), 64);       // renders past the end

    bool found = false;
    VoiceHandle finished = kInvalidVoice;
    Event ev;
    while (e.pollEvent(ev)) {
        if (ev.type == EventType::VoiceFinished) { found = true; finished = ev.voice; }
    }
    EXPECT_TRUE(found);
    EXPECT_EQ(finished, v);
}

TEST(Engine, PolyphonyExhaustedEventFires) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.1f, 100000);
    for (int i = 0; i < 70; ++i) e.play(s, PlayParams{.loop = true}); // > 64 voices

    std::vector<float> out(8 * 2, 0.0f);
    e.renderOffline(out.data(), 8);        // drains all play commands

    bool exhausted = false;
    Event ev;
    while (e.pollEvent(ev)) {
        if (ev.type == EventType::VoicesExhausted) exhausted = true;
    }
    EXPECT_TRUE(exhausted);
}

TEST(Engine, StopAllSilencesAndNotifies) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.5f, 100000);
    e.play(s, PlayParams{.loop = true});

    std::vector<float> out(64 * 2, 0.0f);
    e.renderOffline(out.data(), 64);
    EXPECT_GT(energy(out), 0.1f);          // playing

    e.stopAll();
    std::fill(out.begin(), out.end(), 0.0f);
    e.renderOffline(out.data(), 64);
    EXPECT_NEAR(energy(out), 0.0f, 0.001f); // silent

    bool stopped = false;
    Event ev;
    while (e.pollEvent(ev)) {
        if (ev.type == EventType::VoiceFinished && ev.reason == VoiceEndReason::Stopped) {
            stopped = true;
        }
    }
    EXPECT_TRUE(stopped);
}

TEST(Engine, CommandQueueFullIsReported) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.1f, 1000);

    int dropped = 0;
    for (int i = 0; i < 400; ++i) {        // > queue capacity (256) without draining
        if (e.play(s) == kInvalidVoice) ++dropped;
    }
    EXPECT_GT(dropped, 0);
}

TEST(Engine, UnloadBlocksNewPlays) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.5f, 1000);
    EXPECT_TRUE(e.unloadSound(s));
    EXPECT_EQ(e.play(s), kInvalidVoice);   // can't play a retired sound
    EXPECT_FALSE(e.unloadSound(s));        // already retired
}

TEST(Engine, ResamplesLowerRateToDeviceRate) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);                 // device 48 kHz
    std::vector<float> s(100, 0.5f);
    auto wav = test::makeWavPcm16(1, 24000, s);           // 100 frames at 24 kHz
    auto v = e.play(e.loadWavMemory(wav.data(), wav.size()));
    // A 24 kHz / 100-frame sound lasts ~200 output frames at 48 kHz.
    int rendered = renderUntilFinished(e, v, 16, 64);
    ASSERT_GT(rendered, 0);
    EXPECT_NEAR(rendered, 200, 24);
}

TEST(Engine, PitchUpShortensPlayback) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    std::vector<float> s(200, 0.5f);
    auto wav = test::makeWavPcm16(1, 48000, s);           // 200 frames at device rate
    auto v = e.play(e.loadWavMemory(wav.data(), wav.size()),
                    PlayParams{.pitch = 2.0f});
    // pitch 2.0 consumes 2 source frames per output -> finishes in ~100 frames.
    int rendered = renderUntilFinished(e, v, 16, 64);
    ASSERT_GT(rendered, 0);
    EXPECT_NEAR(rendered, 100, 24);
}

TEST(Engine, ConstantSoundUnchangedByResampling) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    std::vector<float> s(10000, 0.5f);
    auto wav = test::makeWavPcm16(1, 32000, s);           // odd ratio 32k -> 48k
    e.play(e.loadWavMemory(wav.data(), wav.size()), PlayParams{.pan = 0.0f});
    std::vector<float> out(64 * 2, 0.0f);
    e.renderOffline(out.data(), 64);
    // Interpolating between equal samples yields the same value (center pan).
    EXPECT_NEAR(out[0], 0.5f * 0.70710678f, 0.02f);
    EXPECT_NEAR(out[1], 0.5f * 0.70710678f, 0.02f);
}

TEST(SoundBank, UnloadFreesIdleSoundImmediately) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.5f, 1000);
    EXPECT_EQ(e.soundCount(), 1u);
    EXPECT_TRUE(e.unloadSound(s));      // not playing -> data freed now
    EXPECT_EQ(e.soundCount(), 0u);
}

TEST(SoundBank, RetiredSoundStaysAliveUntilVoiceEnds) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.5f, 100);   // short
    auto v = e.play(s);

    std::vector<float> out(16 * 2, 0.0f);
    e.renderOffline(out.data(), 16);        // voice now playing
    EXPECT_TRUE(e.unloadSound(s));          // retired, but a voice still plays it
    EXPECT_EQ(e.soundCount(), 1u);          // buffer kept alive

    int rendered = renderUntilFinished(e, v, 16, 64);
    ASSERT_GT(rendered, 0);
    Event ev;
    while (e.pollEvent(ev)) {}              // drains events + reclaims
    EXPECT_EQ(e.soundCount(), 0u);          // last voice gone -> freed
}

TEST(SoundBank, StaleHandleAfterUnloadFails) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.5f, 1000);
    EXPECT_TRUE(e.unloadSound(s));
    Event ev;
    while (e.pollEvent(ev)) {}             // reclaim
    EXPECT_EQ(e.play(s), kInvalidVoice);   // stale handle no longer resolves
    EXPECT_FALSE(e.unloadSound(s));        // already retired/freed
}

TEST(SoundBank, ReclamationBoundsLiveDataAcrossCycles) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    for (int i = 0; i < 200; ++i) {
        auto s = loadConstMono(e, 0.3f, 50);   // short one-shot
        auto v = e.play(s);
        renderUntilFinished(e, v, 16, 16);     // play to completion
        e.unloadSound(s);                      // reclaims the just-finished voice
        Event ev;
        while (e.pollEvent(ev)) {}
    }
    // Despite 200 load/unload cycles, decoded data is reclaimed each time, so
    // almost nothing stays resident (not 200 buffers).
    EXPECT_LE(e.soundCount(), 2u);
}

TEST(Master, LimiterTamesHotMixAndIsTransparentWhenOff) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    EXPECT_TRUE(e.masterLimiterEnabled());        // on by default
    auto s = loadConstMono(e, 1.0f, 1000);        // full-scale source
    for (int i = 0; i < 8; ++i) {                 // 8 loud voices -> sum >> 1.0
        e.play(s, PlayParams{.gain = 1.0f, .pan = 0.0f});
    }

    std::vector<float> out(64 * 2, 0.0f);
    e.renderOffline(out.data(), 64);
    for (float x : out) EXPECT_LE(std::abs(x), 1.0f);  // soft-clipped to within +-1
    EXPECT_GT(std::abs(out[0]), 0.7f);                 // and still loud (limiting engaged)

    // With the limiter off, the same hot mix overshoots full-scale.
    e.setMasterLimiterEnabled(false);
    EXPECT_FALSE(e.masterLimiterEnabled());
    std::fill(out.begin(), out.end(), 0.0f);
    e.renderOffline(out.data(), 64);
    EXPECT_GT(std::abs(out[0]), 1.0f);
}

TEST(Fades, FadeInRampsGainUp) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.5f, 100000);
    e.play(s, PlayParams{.pan = -1.0f, .fadeIn = 0.01f});  // 10 ms = 480 frames

    std::vector<float> out(512 * 2, 0.0f);
    e.renderOffline(out.data(), 512);
    EXPECT_LT(std::abs(out[0]), 0.05f);         // starts near silence
    EXPECT_NEAR(out[500 * 2], 0.5f, 0.05f);     // settles to full (src 0.5 * panL 1)
}

TEST(Fades, FadeOutStaysAudibleThenFinishes) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.5f, 100000);
    auto v = e.play(s, PlayParams{.pan = -1.0f});      // instant start, full

    std::vector<float> out(64 * 2, 0.0f);
    e.renderOffline(out.data(), 64);
    EXPECT_GT(std::abs(out[0]), 0.3f);                 // playing
    EXPECT_TRUE(e.stopVoice(v, 0.01f));                // 10 ms fade-out

    std::fill(out.begin(), out.end(), 0.0f);
    e.renderOffline(out.data(), 64);                   // 64 < 480: still fading
    bool finishedEarly = false;
    Event ev;
    while (e.pollEvent(ev)) {
        if (ev.type == EventType::VoiceFinished && ev.voice == v) finishedEarly = true;
    }
    EXPECT_FALSE(finishedEarly);                       // not done yet
    EXPECT_GT(std::abs(out[0]), 0.0f);                 // still audible during fade

    EXPECT_GT(renderUntilFinished(e, v, 64, 32), 0);   // fade completes -> finishes
}

TEST(Fades, SetVoiceGainSmoothsInsteadOfJumping) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.5f, 100000);
    auto v = e.play(s, PlayParams{.pan = -1.0f});

    std::vector<float> out(8 * 2, 0.0f);
    e.renderOffline(out.data(), 8);
    e.setVoiceGain(v, 0.0f);                            // ramps to 0 over ~5 ms

    std::fill(out.begin(), out.end(), 0.0f);
    e.renderOffline(out.data(), 8);                     // 8 << 240: not silent yet
    EXPECT_GT(std::abs(out[0]), 0.3f);                  // did not jump straight to 0

    std::vector<float> settle(512 * 2, 0.0f);
    e.renderOffline(settle.data(), 512);
    EXPECT_NEAR(settle[500 * 2], 0.0f, 0.01f);          // now silent
}

TEST(Decode, DetectsContainerByMagic) {
    auto wav = test::makeWavPcm16(1, 48000, std::vector<float>(16, 0.1f));
    EXPECT_EQ(detectAudioFormat(wav.data(), wav.size()), AudioFormat::Wav);

    const unsigned char flac[] = {'f', 'L', 'a', 'C', 0, 0, 0, 0};
    EXPECT_EQ(detectAudioFormat(flac, sizeof flac), AudioFormat::Flac);
    const unsigned char ogg[] = {'O', 'g', 'g', 'S', 0, 0, 0, 0};
    EXPECT_EQ(detectAudioFormat(ogg, sizeof ogg), AudioFormat::Ogg);
    const unsigned char id3[] = {'I', 'D', '3', 4, 0, 0, 0};
    EXPECT_EQ(detectAudioFormat(id3, sizeof id3), AudioFormat::Mp3);
    const unsigned char sync[] = {0xFF, 0xFB, 0x90, 0x00};        // MPEG frame sync
    EXPECT_EQ(detectAudioFormat(sync, sizeof sync), AudioFormat::Mp3);
    const unsigned char junk[] = {1, 2, 3, 4};
    EXPECT_EQ(detectAudioFormat(junk, sizeof junk), AudioFormat::Unknown);
    EXPECT_EQ(detectAudioFormat(nullptr, 0), AudioFormat::Unknown);
}

namespace {
// Locate a codec fixture under ROPE_FIXTURE_DIR (generated in CI from a WAV).
std::optional<std::filesystem::path> fixturePath(const char* name) {
    const char* dir = std::getenv("ROPE_FIXTURE_DIR");
    if (dir == nullptr || *dir == '\0') return std::nullopt;
    std::filesystem::path p = std::filesystem::path(dir) / name;
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) return std::nullopt;
    return p;
}

void decodeFixtureCheck(const char* name) {
    auto p = fixturePath(name);
    if (!p) GTEST_SKIP() << "fixture '" << name
                         << "' absent (set ROPE_FIXTURE_DIR to enable)";
    auto buf = decodeWav(*p);
    ASSERT_TRUE(buf.has_value()) << "failed to decode " << name;
    EXPECT_GE(buf->channels, 1u);
    EXPECT_GT(buf->sampleRate, 0u);
    ASSERT_FALSE(buf->samples.empty());
    float e = 0.0f;
    for (float x : buf->samples) e += std::abs(x);
    EXPECT_GT(e, 0.0f);   // decoded real audio, not silence
}
} // namespace

TEST(Decode, FlacFixture) { decodeFixtureCheck("tone.flac"); }
TEST(Decode, Mp3Fixture)  { decodeFixtureCheck("tone.mp3"); }
TEST(Decode, OggFixture)  { decodeFixtureCheck("tone.ogg"); }

TEST(Scheduling, ClockAdvancesAndResets) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    EXPECT_EQ(e.currentFrame(), 0u);
    std::vector<float> out(128 * 2, 0.0f);
    e.renderOffline(out.data(), 128);
    EXPECT_EQ(e.currentFrame(), 128u);
    e.renderOffline(out.data(), 128);
    EXPECT_EQ(e.currentFrame(), 256u);
    e.stop();
    EXPECT_EQ(e.currentFrame(), 0u);   // reset on stop
}

TEST(Scheduling, VoiceStartsAtExactFrame) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.5f, 100000);

    std::vector<float> warm(100 * 2, 0.0f);
    e.renderOffline(warm.data(), 100);                       // clock -> 100
    EXPECT_EQ(e.currentFrame(), 100u);

    e.play(s, PlayParams{.pan = -1.0f, .startFrame = 150});  // 50 frames into next block

    std::vector<float> out(100 * 2, 0.0f);
    e.renderOffline(out.data(), 100);                        // absolute frames [100,200)
    EXPECT_EQ(e.currentFrame(), 200u);
    EXPECT_NEAR(out[49 * 2], 0.0f, 1e-6f);                   // abs 149: still silent
    EXPECT_NEAR(out[50 * 2], 0.5f, 0.02f);                   // abs 150: starts exactly here
    EXPECT_NEAR(out[80 * 2], 0.5f, 0.02f);                   // and keeps playing
}

TEST(Scheduling, PendingVoiceIsSilentThenPlays) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.5f, 100000);
    e.play(s, PlayParams{.pan = -1.0f, .startFrame = 500});

    std::vector<float> early(100 * 2, 0.0f);
    e.renderOffline(early.data(), 100);                      // [0,100): entirely before 500
    EXPECT_EQ(energy(early), 0.0f);                          // silent while pending
    Event ev;
    bool finished = false;
    while (e.pollEvent(ev)) {
        if (ev.type == EventType::VoiceFinished) finished = true;
    }
    EXPECT_FALSE(finished);                                  // not started, not finished

    std::vector<float> big(600 * 2, 0.0f);
    e.renderOffline(big.data(), 600);                        // [100,700): crosses 500
    EXPECT_NEAR(big[(500 - 100) * 2], 0.5f, 0.02f);          // audible from abs 500
}

TEST(Filter, LowpassAttenuatesHighFrequencies) {
    // Nyquist-frequency signal: alternating +/-0.5 each sample.
    std::vector<float> alt(4000);
    for (std::size_t i = 0; i < alt.size(); ++i) alt[i] = (i % 2 == 0) ? 0.5f : -0.5f;
    auto wav = test::makeWavPcm16(1, 48000, alt);

    auto measure = [&](float cutoffHz) {
        AudioEngine e;
        e.start(48000, 0, BackendType::Null);
        auto s = e.loadWavMemory(wav.data(), wav.size());
        e.play(s, PlayParams{.pan = -1.0f, .lowpassHz = cutoffHz});
        std::vector<float> out(1000 * 2, 0.0f);
        e.renderOffline(out.data(), 1000);
        return energy(out);
    };

    const float refEnergy  = measure(0.0f);     // filter off
    const float filtEnergy = measure(200.0f);   // strong low-pass
    EXPECT_GT(refEnergy, 0.0f);
    EXPECT_LT(filtEnergy, refEnergy * 0.25f);   // high frequencies strongly cut
}

TEST(Filter, LowpassPassesLowFrequencies) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.5f, 100000);    // DC / constant signal
    e.play(s, PlayParams{.pan = -1.0f, .lowpassHz = 200.0f});
    std::vector<float> out(2000 * 2, 0.0f);
    e.renderOffline(out.data(), 2000);
    EXPECT_NEAR(out[1900 * 2], 0.5f, 0.02f);    // DC passes once the filter settles
}

TEST(Filter, LiveLowpassMuffles) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    std::vector<float> alt(8000);
    for (std::size_t i = 0; i < alt.size(); ++i) alt[i] = (i % 2 == 0) ? 0.5f : -0.5f;
    auto wav = test::makeWavPcm16(1, 48000, alt);
    auto s = e.loadWavMemory(wav.data(), wav.size());

    auto v = e.play(s, PlayParams{.pan = -1.0f});      // unfiltered
    std::vector<float> open(500 * 2, 0.0f);
    e.renderOffline(open.data(), 500);
    e.setVoiceLowpass(v, 150.0f);                      // muffle live
    std::vector<float> muffled(500 * 2, 0.0f);
    e.renderOffline(muffled.data(), 500);
    EXPECT_LT(energy(muffled), energy(open) * 0.3f);
}

TEST(Resampler, DefaultsToLinearAndToggles) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    EXPECT_EQ(e.resampleQuality(), ResampleQuality::Linear);
    e.setResampleQuality(ResampleQuality::Sinc);
    EXPECT_EQ(e.resampleQuality(), ResampleQuality::Sinc);
}

TEST(Resampler, SincPreservesLevelAtFractionalRate) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    e.setResampleQuality(ResampleQuality::Sinc);
    auto s = loadConstMono(e, 0.5f, 100000);
    e.play(s, PlayParams{.pan = -1.0f, .pitch = 1.7f});   // fractional rate -> sinc path
    std::vector<float> out(1000 * 2, 0.0f);
    e.renderOffline(out.data(), 1000);
    EXPECT_NEAR(out[800 * 2], 0.5f, 0.01f);               // constant (DC) preserved
}

TEST(Resampler, SincAntiAliasesOnDownsampling) {
    // Nyquist-frequency source. Played at pitch 2.0 (2x decimation) it folds to
    // DC with linear interpolation, but a band-limited sinc rejects it.
    std::vector<float> alt(8000);
    for (std::size_t i = 0; i < alt.size(); ++i) alt[i] = (i % 2 == 0) ? 0.5f : -0.5f;
    auto wav = test::makeWavPcm16(1, 48000, alt);

    auto measure = [&](ResampleQuality q) {
        AudioEngine e;
        e.start(48000, 0, BackendType::Null);
        e.setResampleQuality(q);
        auto s = e.loadWavMemory(wav.data(), wav.size());
        e.play(s, PlayParams{.pan = -1.0f, .pitch = 2.0f});
        std::vector<float> out(1000 * 2, 0.0f);
        e.renderOffline(out.data(), 1000);
        return energy(out);
    };

    const float linE = measure(ResampleQuality::Linear);
    const float sinE = measure(ResampleQuality::Sinc);
    EXPECT_GT(linE, 0.0f);
    EXPECT_LT(sinE, linE * 0.5f);   // sinc removes the aliased Nyquist energy
}

TEST(Buses, DefaultsToSfxAtUnityGain) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    EXPECT_FLOAT_EQ(e.busVolume(Bus::Sfx), 1.0f);
    EXPECT_FLOAT_EQ(e.busVolume(Bus::Music), 1.0f);
    EXPECT_FLOAT_EQ(e.busVolume(Bus::Ui), 1.0f);
}

TEST(Buses, BusVolumeScalesOnlyItsGroup) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.5f, 100000);

    // SFX voice hard-left, Music voice hard-right -> isolated on L / R channels.
    e.play(s, PlayParams{.pan = -1.0f, .bus = Bus::Sfx});
    e.play(s, PlayParams{.pan = 1.0f, .bus = Bus::Music});
    e.setBusVolume(Bus::Music, 0.5f);                  // half the music group only

    std::vector<float> out(512 * 2, 0.0f);
    e.renderOffline(out.data(), 512);                  // past the ~5 ms bus ramp
    EXPECT_NEAR(out[500 * 2 + 0], 0.5f, 0.02f);        // L: SFX unaffected
    EXPECT_NEAR(out[500 * 2 + 1], 0.25f, 0.02f);       // R: Music halved
    EXPECT_FLOAT_EQ(e.busVolume(Bus::Music), 0.5f);
}

TEST(Buses, BusVolumeSmoothsInsteadOfJumping) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.5f, 100000);
    e.play(s, PlayParams{.pan = 1.0f, .bus = Bus::Music});

    std::vector<float> out(8 * 2, 0.0f);
    e.renderOffline(out.data(), 8);
    e.setBusVolume(Bus::Music, 0.0f);                  // ramps to 0 over ~5 ms

    std::fill(out.begin(), out.end(), 0.0f);
    e.renderOffline(out.data(), 8);                    // 8 << 240: not silent yet
    EXPECT_GT(std::abs(out[1]), 0.3f);                 // did not jump straight to 0

    std::vector<float> settle(512 * 2, 0.0f);
    e.renderOffline(settle.data(), 512);
    EXPECT_NEAR(settle[500 * 2 + 1], 0.0f, 0.01f);     // now silent
}

TEST(Buses, MuteSilencesItsGroup) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.5f, 100000);
    e.play(s, PlayParams{.pan = -1.0f, .bus = Bus::Sfx});
    e.play(s, PlayParams{.pan = 1.0f, .bus = Bus::Music});
    e.setBusMuted(Bus::Music, true);
    EXPECT_TRUE(e.busMuted(Bus::Music));

    std::vector<float> out(512 * 2, 0.0f);
    e.renderOffline(out.data(), 512);
    EXPECT_NEAR(out[500 * 2 + 0], 0.5f, 0.02f);        // SFX still audible
    EXPECT_NEAR(out[500 * 2 + 1], 0.0f, 0.01f);        // Music muted
}

TEST(Buses, SoloLeavesOnlySoloedAudible) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.5f, 100000);
    e.play(s, PlayParams{.pan = -1.0f, .bus = Bus::Sfx});
    e.play(s, PlayParams{.pan = 1.0f, .bus = Bus::Music});

    e.setBusSoloed(Bus::Sfx, true);
    EXPECT_TRUE(e.busSoloed(Bus::Sfx));
    std::vector<float> out(512 * 2, 0.0f);
    e.renderOffline(out.data(), 512);
    EXPECT_NEAR(out[500 * 2 + 0], 0.5f, 0.02f);        // soloed SFX audible
    EXPECT_NEAR(out[500 * 2 + 1], 0.0f, 0.01f);        // Music silenced by solo

    e.setBusSoloed(Bus::Sfx, false);                   // clearing solo restores both
    std::vector<float> out2(512 * 2, 0.0f);
    e.renderOffline(out2.data(), 512);
    EXPECT_NEAR(out2[500 * 2 + 0], 0.5f, 0.02f);
    EXPECT_NEAR(out2[500 * 2 + 1], 0.5f, 0.02f);
}

TEST(Soak, VoicePoolOverloadIsGraceful) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    auto s = loadConstMono(e, 0.5f, 100000);
    for (int i = 0; i < 300; ++i) e.play(s, PlayParams{.gain = 0.3f}); // >> 64-voice pool

    int exhausted = 0;
    std::vector<float> out(128 * 2, 0.0f);
    for (int blk = 0; blk < 10; ++blk) {
        e.renderOffline(out.data(), 128);
        for (float x : out) ASSERT_TRUE(std::isfinite(x));   // mix never blows up
        Event ev;
        while (e.pollEvent(ev)) {
            if (ev.type == EventType::VoicesExhausted) ++exhausted;
        }
    }
    EXPECT_GT(exhausted, 0);   // overflow is reported, not silently mishandled
}

TEST(Soak, RandomizedChurnStaysStableAndLeakFree) {
    AudioEngine e;
    e.start(48000, 0, BackendType::Null);
    std::mt19937 rng(1234567u);   // fixed seed -> deterministic

    std::vector<SoundHandle> live;
    std::vector<float> out(256 * 2, 0.0f);

    auto makeSound = [&]() {
        const int frames = 50 + static_cast<int>(rng() % 2000);
        const float amp  = 0.1f + static_cast<float>(rng() % 100) / 200.0f;
        return loadConstMono(e, amp, frames);
    };

    for (int iter = 0; iter < 4000; ++iter) {
        const unsigned op = rng() % 6u;
        if (op == 0 || live.empty()) {                       // load
            const SoundHandle s = makeSound();
            if (s != kInvalidSound) live.push_back(s);
        } else if (op == 1 || op == 2) {                     // play (sometimes a burst)
            const SoundHandle s = live[rng() % live.size()];
            const int burst = 1 + static_cast<int>(rng() % 4);
            for (int b = 0; b < burst; ++b) {
                e.play(s, PlayParams{.gain  = 0.4f,
                                     .pan   = -1.0f + static_cast<float>(rng() % 200) / 100.0f,
                                     .pitch = 0.5f + static_cast<float>(rng() % 300) / 100.0f});
            }
        } else if (op == 3) {                                // unload a live sound
            const std::size_t i = rng() % live.size();
            e.unloadSound(live[i]);
            live.erase(live.begin() + static_cast<std::ptrdiff_t>(i));
        } else if (op == 4) {                                // toggle global state
            e.setResampleQuality((rng() & 1u) ? ResampleQuality::Sinc
                                              : ResampleQuality::Linear);
            e.setMasterVolume(0.2f + static_cast<float>(rng() % 100) / 100.0f);
        } else {                                             // render + drain
            const unsigned n = 64u + (rng() % 192u);
            e.renderOffline(out.data(), n);
            for (unsigned i = 0; i < n * 2; ++i) ASSERT_TRUE(std::isfinite(out[i]));
            Event ev;
            while (e.pollEvent(ev)) { /* drain */ }
        }
    }

    // Leak check: unload everything, flush voices, and confirm live reclamation
    // emptied the bank (no buffer left holding decoded data).
    for (const SoundHandle s : live) e.unloadSound(s);
    e.stopAll();
    for (int i = 0; i < 300; ++i) {
        e.renderOffline(out.data(), 256);
        Event ev;
        while (e.pollEvent(ev)) {}
    }
    EXPECT_EQ(e.soundCount(), 0u);
}
