#include "backends/Backends.hpp"

#include <string>

// A do-nothing backend: opens no device and spawns no real-time thread. The
// engine reports as "running" so the control API behaves normally, but audio is
// advanced manually via AudioEngine::renderOffline(). Used for offline/headless
// rendering, deterministic unit tests, and CI runners without audio hardware.
namespace rope::backends {
namespace {

class NullBackend final : public AudioBackend {
public:
    bool start(const AudioStreamConfig& config,
               RenderCallback /*render*/, void* /*user*/) override {
        sampleRate_ = config.sampleRate ? config.sampleRate : 48000u;
        channels_   = config.channels   ? config.channels   : 2u;
        running_    = true;
        return true;
    }

    void stop() override { running_ = false; }

    [[nodiscard]] bool isRunning() const override { return running_; }
    [[nodiscard]] unsigned int sampleRate() const override { return sampleRate_; }
    [[nodiscard]] unsigned int channels() const override { return channels_; }
    [[nodiscard]] const char* name() const override { return "null (offline)"; }

private:
    unsigned int sampleRate_ = 48000;
    unsigned int channels_   = 2;
    bool         running_    = false;
};

} // namespace

std::unique_ptr<AudioBackend> createNullBackend() {
    return std::make_unique<NullBackend>();
}

} // namespace rope::backends
