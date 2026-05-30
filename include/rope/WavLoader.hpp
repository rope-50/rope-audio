#pragma once

#include "rope/AudioBuffer.hpp"

#include <filesystem>
#include <optional>

namespace rope {

/// Decode a WAV file (mono or stereo; PCM or IEEE-float) into a float
/// AudioBuffer. Returns std::nullopt on any failure (missing file, bad
/// format, empty data).
///
/// This is a plain control-thread function: it allocates and does file I/O,
/// so never call it from the audio callback.
[[nodiscard]] std::optional<AudioBuffer> decodeWav(const std::filesystem::path& path);

} // namespace rope
