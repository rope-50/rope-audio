#pragma once

// Internal header (not installed): per-backend factory functions. Each is
// compiled only when its ROPE_AUDIO_BACKEND_* macro is defined, so the heavy
// platform headers (miniaudio.h, RtAudio.h) never leak into the public API.

#include "rope/AudioBackend.hpp"

#include <memory>

namespace rope::backends {

// Always available — no device, for offline/headless rendering and tests.
std::unique_ptr<AudioBackend> createNullBackend();

#if defined(ROPE_AUDIO_BACKEND_MINIAUDIO)
std::unique_ptr<AudioBackend> createMiniaudioBackend();
#endif

#if defined(ROPE_AUDIO_BACKEND_RTAUDIO)
std::unique_ptr<AudioBackend> createRtAudioBackend(bool preferAsio);
#endif

} // namespace rope::backends
