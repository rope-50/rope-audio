#include <gtest/gtest.h>

#include "rope/WavLoader.hpp"
#include "wav_util.hpp"

TEST(WavLoader, DecodeMemoryMono) {
    std::vector<float> samples(100, 0.5f);
    auto wav = test::makeWavPcm16(1, 48000, samples);

    auto buf = rope::decodeWav(wav.data(), wav.size());
    ASSERT_TRUE(buf.has_value());
    EXPECT_EQ(buf->channels, 1u);
    EXPECT_EQ(buf->sampleRate, 48000u);
    EXPECT_EQ(buf->frameCount(), 100u);
    EXPECT_NEAR(buf->samples[0], 0.5f, 0.001f);
}

TEST(WavLoader, DecodeMemoryStereo) {
    std::vector<float> s;
    for (int i = 0; i < 50; ++i) { s.push_back(0.25f); s.push_back(-0.25f); }
    auto wav = test::makeWavPcm16(2, 44100, s);

    auto buf = rope::decodeWav(wav.data(), wav.size());
    ASSERT_TRUE(buf.has_value());
    EXPECT_EQ(buf->channels, 2u);
    EXPECT_EQ(buf->sampleRate, 44100u);
    EXPECT_EQ(buf->frameCount(), 50u);
    EXPECT_NEAR(buf->samples[0], 0.25f, 0.001f);
    EXPECT_NEAR(buf->samples[1], -0.25f, 0.001f);
}

TEST(WavLoader, RejectsGarbageAndNull) {
    std::vector<std::uint8_t> junk(64, 0xAB);
    EXPECT_FALSE(rope::decodeWav(junk.data(), junk.size()).has_value());
    EXPECT_FALSE(rope::decodeWav(nullptr, 0).has_value());
}
