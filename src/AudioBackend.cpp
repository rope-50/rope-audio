#include "rope/AudioBackend.hpp"

#include "backends/Backends.hpp"

namespace rope {

namespace {

std::unique_ptr<AudioBackend> makeMiniaudio() {
#if defined(ROPE_AUDIO_BACKEND_MINIAUDIO)
    return backends::createMiniaudioBackend();
#else
    return nullptr;
#endif
}

std::unique_ptr<AudioBackend> makeRtAudio(bool preferAsio) {
#if defined(ROPE_AUDIO_BACKEND_RTAUDIO)
    return backends::createRtAudioBackend(preferAsio);
#else
    (void)preferAsio;
    return nullptr;
#endif
}

} // namespace

std::unique_ptr<AudioBackend> createAudioBackend(BackendType type) {
    switch (type) {
    case BackendType::Miniaudio:
        return makeMiniaudio();
    case BackendType::RtAudio:
        return makeRtAudio(/*preferAsio=*/false);
    case BackendType::RtAudioAsio:
        return makeRtAudio(/*preferAsio=*/true);
    case BackendType::Default:
    default:
        // miniaudio is the cross-platform default; fall back to RtAudio if a
        // mobile/desktop build excluded miniaudio for some reason.
        if (auto b = makeMiniaudio()) return b;
        return makeRtAudio(/*preferAsio=*/false);
    }
}

} // namespace rope
