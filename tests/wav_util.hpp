#pragma once

// Test helper: build a minimal 16-bit PCM WAV in memory from interleaved float
// samples, so tests are self-contained (no asset files needed in CI).

#include <cmath>
#include <cstdint>
#include <vector>

namespace test {

inline std::vector<std::uint8_t> makeWavPcm16(std::uint16_t channels,
                                              std::uint32_t sampleRate,
                                              const std::vector<float>& interleaved) {
    const std::uint32_t numSamples = static_cast<std::uint32_t>(interleaved.size());
    const std::uint16_t bits       = 16;
    const std::uint16_t blockAlign = static_cast<std::uint16_t>(channels * bits / 8);
    const std::uint32_t byteRate   = sampleRate * blockAlign;
    const std::uint32_t dataSize   = numSamples * 2u;

    std::vector<std::uint8_t> b;
    b.reserve(44u + dataSize);
    auto w32 = [&](std::uint32_t v) {
        b.push_back(std::uint8_t(v));       b.push_back(std::uint8_t(v >> 8));
        b.push_back(std::uint8_t(v >> 16)); b.push_back(std::uint8_t(v >> 24));
    };
    auto w16 = [&](std::uint16_t v) {
        b.push_back(std::uint8_t(v)); b.push_back(std::uint8_t(v >> 8));
    };
    auto ws = [&](const char* s) { for (int i = 0; i < 4; ++i) b.push_back(std::uint8_t(s[i])); };

    ws("RIFF"); w32(36u + dataSize); ws("WAVE");
    ws("fmt "); w32(16u); w16(1u); w16(channels); w32(sampleRate);
    w32(byteRate); w16(blockAlign); w16(bits);
    ws("data"); w32(dataSize);

    for (float f : interleaved) {
        long v = std::lround(f * 32767.0f);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        w16(static_cast<std::uint16_t>(static_cast<std::int16_t>(v)));
    }
    return b;
}

} // namespace test
