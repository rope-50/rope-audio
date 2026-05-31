#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "rope/AudioEngine.hpp"
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
    auto s = loadConstMono(e, 0.8f, 1000);
    e.setMasterVolume(0.5f);
    e.play(s, PlayParams{.gain = 0.5f, .pan = -1.0f});

    std::vector<float> out(64 * 2, 0.0f);
    e.renderOffline(out.data(), 64);
    // L = 0.8 * gain(0.5) * panL(1.0) * master(0.5) = 0.2
    EXPECT_NEAR(out[0], 0.2f, 0.02f);
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
