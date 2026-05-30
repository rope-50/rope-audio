// dr_wav is a single-header library: defining DR_WAV_IMPLEMENTATION in exactly
// one translation unit pulls in the implementation. This is that unit.
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include "rope/WavLoader.hpp"

namespace rope {
namespace {

// Shared decode tail: read an already-initialized drwav fully into an
// interleaved float32 AudioBuffer, then uninit it.
std::optional<AudioBuffer> decodeInitialized(drwav& wav) {
    AudioBuffer buffer;
    buffer.channels   = wav.channels;
    buffer.sampleRate = wav.sampleRate;
    buffer.samples.resize(static_cast<std::size_t>(wav.totalPCMFrameCount) * wav.channels);

    // Decode everything to interleaved float32 regardless of the on-disk format.
    const drwav_uint64 framesRead =
        drwav_read_pcm_frames_f32(&wav, wav.totalPCMFrameCount, buffer.samples.data());
    drwav_uninit(&wav);

    if (framesRead == 0) {
        return std::nullopt;
    }
    buffer.samples.resize(static_cast<std::size_t>(framesRead) * wav.channels);
    return buffer;
}

} // namespace

std::optional<AudioBuffer> decodeWav(const std::filesystem::path& path) {
    drwav wav;

#ifdef _WIN32
    // std::filesystem::path stores wchar_t on Windows; use the wide overload so
    // non-ASCII paths work.
    if (!drwav_init_file_w(&wav, path.wstring().c_str(), nullptr)) {
        return std::nullopt;
    }
#else
    if (!drwav_init_file(&wav, path.string().c_str(), nullptr)) {
        return std::nullopt;
    }
#endif

    return decodeInitialized(wav);
}

std::optional<AudioBuffer> decodeWav(const void* data, std::size_t size) {
    if (data == nullptr || size == 0) {
        return std::nullopt;
    }
    drwav wav;
    if (!drwav_init_memory(&wav, data, size, nullptr)) {
        return std::nullopt;
    }
    return decodeInitialized(wav);
}

} // namespace rope
