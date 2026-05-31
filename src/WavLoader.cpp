// Multi-format audio decoder. Each single-header decoder's implementation is
// pulled in exactly once here (this is that translation unit):
//   * dr_wav   — WAV (PCM / IEEE-float)
//   * dr_flac  — FLAC
//   * dr_mp3   — MP3
//   * stb_vorbis — OGG/Vorbis (compiled separately as C; included header-only
//                  here for the prototypes, hence the extern "C" wrapper).
// Everything is decoded to interleaved float32 so the mixer has one code path.
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

extern "C" {
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
}

#include "rope/WavLoader.hpp"

#include <cstdint>
#include <fstream>
#include <vector>

namespace rope {
namespace {

// Pack a decoded interleaved-f32 result into an AudioBuffer, trimming to the
// number of frames actually produced. Returns nullopt if nothing decoded.
std::optional<AudioBuffer> pack(std::vector<float>&& samples, std::uint32_t channels,
                                std::uint32_t sampleRate, std::uint64_t framesRead) {
    if (framesRead == 0 || channels == 0) return std::nullopt;
    AudioBuffer buffer;
    buffer.channels   = channels;
    buffer.sampleRate = sampleRate;
    buffer.samples    = std::move(samples);
    buffer.samples.resize(static_cast<std::size_t>(framesRead) * channels);
    return buffer;
}

std::optional<AudioBuffer> decodeWavMem(const void* data, std::size_t size) {
    drwav wav;
    if (!drwav_init_memory(&wav, data, size, nullptr)) return std::nullopt;
    std::vector<float> s(static_cast<std::size_t>(wav.totalPCMFrameCount) * wav.channels);
    const drwav_uint64 read = drwav_read_pcm_frames_f32(&wav, wav.totalPCMFrameCount, s.data());
    const std::uint32_t ch = wav.channels, sr = wav.sampleRate;
    drwav_uninit(&wav);
    return pack(std::move(s), ch, sr, read);
}

std::optional<AudioBuffer> decodeFlacMem(const void* data, std::size_t size) {
    drflac* f = drflac_open_memory(data, size, nullptr);
    if (!f) return std::nullopt;
    std::vector<float> s(static_cast<std::size_t>(f->totalPCMFrameCount) * f->channels);
    const drflac_uint64 read = drflac_read_pcm_frames_f32(f, f->totalPCMFrameCount, s.data());
    const std::uint32_t ch = f->channels, sr = f->sampleRate;
    drflac_close(f);
    return pack(std::move(s), ch, sr, read);
}

std::optional<AudioBuffer> decodeMp3Mem(const void* data, std::size_t size) {
    drmp3 mp3;
    if (!drmp3_init_memory(&mp3, data, size, nullptr)) return std::nullopt;
    const drmp3_uint64 total = drmp3_get_pcm_frame_count(&mp3);
    std::vector<float> s(static_cast<std::size_t>(total) * mp3.channels);
    const drmp3_uint64 read = drmp3_read_pcm_frames_f32(&mp3, total, s.data());
    const std::uint32_t ch = mp3.channels, sr = mp3.sampleRate;
    drmp3_uninit(&mp3);
    return pack(std::move(s), ch, sr, read);
}

std::optional<AudioBuffer> decodeOggMem(const void* data, std::size_t size) {
    if (size > static_cast<std::size_t>(0x7fffffff)) return std::nullopt; // stb takes int
    int err = 0;
    stb_vorbis* v = stb_vorbis_open_memory(
        static_cast<const unsigned char*>(data), static_cast<int>(size), &err, nullptr);
    if (v == nullptr) return std::nullopt;

    const stb_vorbis_info info = stb_vorbis_get_info(v);
    const unsigned int ch    = info.channels;
    const unsigned int sr    = info.sample_rate;
    const unsigned int total = stb_vorbis_stream_length_in_samples(v); // frames/channel

    // Decode straight into OUR buffer (interleaved float). The only stb-owned
    // allocation is the decoder handle, freed by stb_vorbis_close in the same
    // (stb) translation unit — so no raw pointer is ever malloc'd in stb and
    // free'd here, sidestepping any CRT-heap mismatch.
    std::vector<float> s(static_cast<std::size_t>(total) * ch);
    const int frames = (ch == 0 || total == 0)
        ? 0
        : stb_vorbis_get_samples_float_interleaved(
              v, static_cast<int>(ch), s.data(), static_cast<int>(s.size()));
    stb_vorbis_close(v);
    if (frames <= 0) return std::nullopt;
    return pack(std::move(s), ch, sr, static_cast<std::uint64_t>(frames));
}

std::optional<AudioBuffer> decodeMemoryDispatch(const void* data, std::size_t size) {
    if (data == nullptr || size == 0) return std::nullopt;
    switch (detectAudioFormat(data, size)) {
    case AudioFormat::Wav:  return decodeWavMem(data, size);
    case AudioFormat::Flac: return decodeFlacMem(data, size);
    case AudioFormat::Ogg:  return decodeOggMem(data, size);
    case AudioFormat::Mp3:  return decodeMp3Mem(data, size);
    case AudioFormat::Unknown:
    default:
        // Raw MPEG audio without an ID3 tag can dodge the sniff; give dr_mp3 a try.
        return decodeMp3Mem(data, size);
    }
}

} // namespace

AudioFormat detectAudioFormat(const void* data, std::size_t size) {
    const auto* d = static_cast<const unsigned char*>(data);
    if (d == nullptr) return AudioFormat::Unknown;
    if (size >= 12 && d[0] == 'R' && d[1] == 'I' && d[2] == 'F' && d[3] == 'F' &&
        d[8] == 'W' && d[9] == 'A' && d[10] == 'V' && d[11] == 'E')
        return AudioFormat::Wav;
    if (size >= 4 && d[0] == 'f' && d[1] == 'L' && d[2] == 'a' && d[3] == 'C')
        return AudioFormat::Flac;
    if (size >= 4 && d[0] == 'O' && d[1] == 'g' && d[2] == 'g' && d[3] == 'S')
        return AudioFormat::Ogg;
    if (size >= 3 && d[0] == 'I' && d[1] == 'D' && d[2] == '3')
        return AudioFormat::Mp3;                                   // ID3v2-tagged MP3
    if (size >= 2 && d[0] == 0xFF && (d[1] & 0xE0) == 0xE0)
        return AudioFormat::Mp3;                                   // raw MPEG frame sync
    return AudioFormat::Unknown;
}

std::optional<AudioBuffer> decodeWav(const std::filesystem::path& path) {
    // Read the whole file into memory and decode from there. This keeps one
    // decode path for every format and sidesteps each decoder's per-platform
    // file/wide-path quirks (std::ifstream takes the wchar_t path on Windows).
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return std::nullopt;
    const std::streamoff size = in.tellg();
    if (size <= 0) return std::nullopt;
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!in) return std::nullopt;
    return decodeMemoryDispatch(bytes.data(), bytes.size());
}

std::optional<AudioBuffer> decodeWav(const void* data, std::size_t size) {
    return decodeMemoryDispatch(data, size);
}

} // namespace rope
