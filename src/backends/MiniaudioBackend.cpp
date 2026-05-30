#include "backends/Backends.hpp"

#if defined(ROPE_AUDIO_BACKEND_MINIAUDIO)

// Strip miniaudio down to the device/playback layer only — rope provides its
// own mixer, WAV decoder and (eventually) resampler, so we don't need
// miniaudio's high-level engine, resource manager, decoders or node graph.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION

// Silence third-party warnings from the single-header implementation.
#if defined(_MSC_VER)
#  pragma warning(push, 0)
#endif
#include "miniaudio.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

#include <cstdio>
#include <string>

#if defined(_WIN32)
#  include <objbase.h>
#  pragma comment(lib, "ole32.lib")
#endif

namespace rope::backends {
namespace {

class MiniaudioBackend final : public AudioBackend {
public:
    ~MiniaudioBackend() override { stop(); }

    bool start(const AudioStreamConfig& config,
               RenderCallback render, void* user) override {
        render_ = render;
        user_   = user;

        holdComReference();

        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format   = ma_format_f32;
        cfg.playback.channels = config.channels;
        cfg.sampleRate        = config.sampleRate;
        if (config.bufferFrames > 0) {
            cfg.periodSizeInFrames = config.bufferFrames;
        }
        cfg.dataCallback = &MiniaudioBackend::dataCallback;
        cfg.pUserData    = this;

        if (ma_device_init(nullptr, &cfg, &device_) != MA_SUCCESS) {
            std::fprintf(stderr, "[rope] miniaudio: ma_device_init failed\n");
            releaseComReference();
            return false;
        }
        if (ma_device_start(&device_) != MA_SUCCESS) {
            std::fprintf(stderr, "[rope] miniaudio: ma_device_start failed\n");
            ma_device_uninit(&device_);
            releaseComReference();
            return false;
        }

        sampleRate_ = device_.sampleRate;
        channels_   = device_.playback.channels;
        name_ = std::string("miniaudio (")
              + ma_get_backend_name(device_.pContext->backend) + ")";
        running_ = true;
        return true;
    }

    void stop() override {
        if (!running_) return;
        ma_device_uninit(&device_); // stops the device and joins its thread
        releaseComReference();
        running_ = false;
    }

    [[nodiscard]] bool isRunning() const override { return running_; }
    [[nodiscard]] unsigned int sampleRate() const override { return sampleRate_; }
    [[nodiscard]] unsigned int channels() const override { return channels_; }
    [[nodiscard]] const char* name() const override { return name_.c_str(); }

private:
    static void dataCallback(ma_device* dev, void* output,
                             const void* /*input*/, ma_uint32 frameCount) {
        auto* self = static_cast<MiniaudioBackend*>(dev->pUserData);
        if (self->render_) {
            self->render_(static_cast<float*>(output), frameCount, self->user_);
        }
    }

    // miniaudio's WASAPI backend does an internal CoInitialize/CoUninitialize
    // dance while initializing the device. If the application doesn't keep COM
    // initialized, that CoUninitialize can drop the apartment's last reference
    // and tear COM down mid-initialization — invalidating the device enumerator
    // and crashing inside GetDefaultAudioEndpoint. Holding our own reference for
    // the device's lifetime (joining whichever apartment the thread already
    // uses) keeps COM alive throughout. No-op on non-Windows platforms.
    void holdComReference() {
#if defined(_WIN32)
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (hr == RPC_E_CHANGED_MODE) {
            // Thread is already in a single-threaded apartment; join it.
            hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        }
        comInitialized_ = (hr == S_OK || hr == S_FALSE);
#endif
    }

    void releaseComReference() {
#if defined(_WIN32)
        if (comInitialized_) { CoUninitialize(); comInitialized_ = false; }
#endif
    }

    ma_device      device_{};
    RenderCallback render_     = nullptr;
    void*          user_       = nullptr;
    unsigned int   sampleRate_ = 0;
    unsigned int   channels_   = 0;
    bool           running_    = false;
#if defined(_WIN32)
    bool           comInitialized_ = false;
#endif
    std::string    name_       = "miniaudio";
};

} // namespace

std::unique_ptr<AudioBackend> createMiniaudioBackend() {
    return std::make_unique<MiniaudioBackend>();
}

} // namespace rope::backends

#endif // ROPE_AUDIO_BACKEND_MINIAUDIO
