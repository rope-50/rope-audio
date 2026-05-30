#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rope {

/// An immutable, decoded audio asset.
///
/// Samples are stored **interleaved** as 32-bit float in the range [-1, 1]:
///   frame0_ch0, frame0_ch1, frame1_ch0, frame1_ch1, ...
///
/// A buffer is loaded once and then shared (by raw pointer) across many
/// simultaneously-playing voices, so it is never mutated after construction.
struct AudioBuffer {
    std::vector<float> samples;       ///< interleaved float samples
    std::uint32_t      channels = 0;  ///< 1 = mono, 2 = stereo
    std::uint32_t      sampleRate = 0;///< frames per second

    /// Number of audio frames (sample groups) in the buffer.
    [[nodiscard]] std::size_t frameCount() const noexcept {
        return channels ? samples.size() / channels : 0;
    }
};

} // namespace rope
