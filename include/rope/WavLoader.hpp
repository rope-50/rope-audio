#pragma once

#include "rope/AudioBuffer.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>

namespace rope {

/// Compressed/uncompressed audio container the loader recognizes.
enum class AudioFormat { Unknown, Wav, Flac, Ogg, Mp3 };

/// Sniff the container format from the first bytes of an encoded buffer (magic
/// numbers: "RIFF"/WAVE, "fLaC", "OggS", "ID3"/MPEG sync). Pure and cheap — used
/// to route to the right decoder. Returns AudioFormat::Unknown if nothing matches.
[[nodiscard]] AudioFormat detectAudioFormat(const void* data, std::size_t size);

/// Decode an audio file into a float AudioBuffer. The container is detected from
/// the file contents (not the extension): **WAV, FLAC, OGG/Vorbis and MP3** are
/// supported, mono or stereo, decoded to interleaved float32. Returns
/// std::nullopt on any failure (missing file, unknown/corrupt format, empty).
///
/// This is a plain control-thread function: it allocates and does file I/O, so
/// never call it from the audio callback.
///
/// (Named `decodeWav` for source/ABI compatibility — it now decodes any of the
/// supported formats.)
[[nodiscard]] std::optional<AudioBuffer> decodeWav(const std::filesystem::path& path);

/// Decode audio from an in-memory buffer (e.g. a bundled/packed asset). The
/// container is detected from the data; the same formats as the file overload
/// are supported. The data only needs to live for the duration of this call.
/// Returns std::nullopt on failure. Control-thread only.
[[nodiscard]] std::optional<AudioBuffer> decodeWav(const void* data, std::size_t size);

} // namespace rope
