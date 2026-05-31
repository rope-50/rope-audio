// Headless mixer benchmark: measures offline render throughput for N
// simultaneous looping voices, comparing the linear and windowed-sinc
// resamplers. Uses the Null backend, so it needs no audio device.
//
//   rope_bench
//
// Reports millions of output frames per second, the real-time factor (how many
// seconds of audio rendered per wall-clock second), and ms per 512-frame block.

#include "rope/AudioEngine.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace rope;
using Clock = std::chrono::steady_clock;

namespace {

constexpr unsigned kRate     = 48000;
constexpr unsigned kBlock    = 512;
constexpr int      kBlocks   = 2000;   // ~21 s of audio per run

// Minimal 16-bit PCM mono WAV of a 440 Hz sine, in memory.
std::vector<unsigned char> makeSineWav(int frames) {
    std::vector<std::int16_t> pcm(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        const double s = 0.25 * std::sin(2.0 * 3.14159265358979 * 440.0 * i / kRate);
        pcm[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(s * 32767.0);
    }
    const std::uint32_t dataSize = static_cast<std::uint32_t>(frames) * 2u;
    std::vector<unsigned char> w;
    auto u32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) w.push_back((v >> (8 * i)) & 0xFF); };
    auto u16 = [&](std::uint16_t v) { for (int i = 0; i < 2; ++i) w.push_back((v >> (8 * i)) & 0xFF); };
    auto tag = [&](const char* t) { for (int i = 0; i < 4; ++i) w.push_back(static_cast<unsigned char>(t[i])); };
    tag("RIFF"); u32(36u + dataSize); tag("WAVE");
    tag("fmt "); u32(16u); u16(1); u16(1); u32(kRate); u32(kRate * 2u); u16(2); u16(16);
    tag("data"); u32(dataSize);
    const unsigned char* p = reinterpret_cast<const unsigned char*>(pcm.data());
    w.insert(w.end(), p, p + dataSize);
    return w;
}

void benchOne(AudioEngine& e, SoundHandle s, int nVoices, ResampleQuality q, const char* qname) {
    e.stopAll();
    std::vector<float> out(static_cast<std::size_t>(kBlock) * 2u, 0.0f);
    e.renderOffline(out.data(), kBlock);          // flush previous voices
    e.setResampleQuality(q);
    for (int i = 0; i < nVoices; ++i) {
        e.play(s, PlayParams{.gain = 0.15f, .pan = (i % 2) ? 0.4f : -0.4f,
                             .loop = true, .pitch = 1.37f}); // fractional -> exercise resampler
    }
    e.renderOffline(out.data(), kBlock);          // let them start

    const auto t0 = Clock::now();
    for (int b = 0; b < kBlocks; ++b) e.renderOffline(out.data(), kBlock);
    const auto t1 = Clock::now();

    const double secs   = std::chrono::duration<double>(t1 - t0).count();
    const double frames = static_cast<double>(kBlocks) * kBlock;
    const double rt     = (frames / kRate) / secs;
    std::printf("  %-6s voices=%-3d  %5.1f Mframe/s  %6.0fx realtime  %.3f ms/block\n",
                qname, nVoices, frames / secs / 1e6, rt, secs / kBlocks * 1000.0);
}

} // namespace

int main() {
    AudioEngine e;
    if (!e.start(kRate, kBlock, BackendType::Null)) {
        std::fprintf(stderr, "failed to start Null backend\n");
        return 1;
    }
    const auto wav = makeSineWav(4096);
    const SoundHandle s = e.loadWavMemory(wav.data(), wav.size());
    if (s == kInvalidSound) { std::fprintf(stderr, "load failed\n"); return 1; }

    std::printf("rope mixer benchmark (Null backend, %u Hz, %u-frame blocks)\n", kRate, kBlock);
    for (int n : {1, 8, 32, 64}) {
        benchOne(e, s, n, ResampleQuality::Linear, "linear");
        benchOne(e, s, n, ResampleQuality::Sinc,   "sinc");
    }
    return 0;
}
