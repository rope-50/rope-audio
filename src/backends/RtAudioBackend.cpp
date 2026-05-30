#include "backends/Backends.hpp"

#if defined(ROPE_AUDIO_BACKEND_RTAUDIO)

#include "RtAudio.h"

#include <cstdio>
#include <string>

namespace rope::backends {
namespace {

// Pick the initial API. On Windows, RtAudio::WINDOWS_ASIO forces the ASIO
// driver path (low latency, what DAWs want); UNSPECIFIED lets RtAudio choose
// its best compiled API (WASAPI on Windows). Both enum values always exist in
// RtAudio.h regardless of which APIs were compiled, so referencing ASIO here is
// safe even on non-Windows builds (RtAudio just falls back).
RtAudio::Api initialApi(bool preferAsio) {
    return preferAsio ? RtAudio::WINDOWS_ASIO : RtAudio::UNSPECIFIED;
}

class RtAudioBackend final : public AudioBackend {
public:
    explicit RtAudioBackend(bool preferAsio) : dac_(initialApi(preferAsio)) {}
    ~RtAudioBackend() override { stop(); }

    bool start(const AudioStreamConfig& config,
               RenderCallback render, void* user) override {
        render_ = render;
        user_   = user;

        if (dac_.getDeviceCount() < 1) {
            std::fprintf(stderr, "[rope] RtAudio: no output devices found\n");
            return false;
        }

        RtAudio::StreamParameters params;
        params.deviceId     = dac_.getDefaultOutputDevice();
        params.nChannels    = config.channels;
        params.firstChannel = 0;

        RtAudio::StreamOptions options;
        options.flags      = RTAUDIO_SCHEDULE_REALTIME;
        options.streamName = "rope-audioengine";

        unsigned int frames = config.bufferFrames ? config.bufferFrames : 512;

        RtAudioErrorType err = dac_.openStream(
            &params, /*input=*/nullptr, RTAUDIO_FLOAT32, config.sampleRate,
            &frames, &RtAudioBackend::rtCallback, this, &options);
        if (err != RTAUDIO_NO_ERROR) {
            std::fprintf(stderr, "[rope] RtAudio openStream: %s\n",
                         dac_.getErrorText().c_str());
            return false;
        }
        if (dac_.startStream() != RTAUDIO_NO_ERROR) {
            std::fprintf(stderr, "[rope] RtAudio startStream: %s\n",
                         dac_.getErrorText().c_str());
            dac_.closeStream();
            return false;
        }

        sampleRate_ = dac_.getStreamSampleRate();
        channels_   = config.channels;
        name_ = std::string("RtAudio (")
              + RtAudio::getApiDisplayName(dac_.getCurrentApi()) + ")";
        running_ = true;
        return true;
    }

    void stop() override {
        if (!running_) return;
        if (dac_.isStreamRunning()) dac_.stopStream();
        if (dac_.isStreamOpen())    dac_.closeStream();
        running_ = false;
    }

    [[nodiscard]] bool isRunning() const override { return running_; }
    [[nodiscard]] unsigned int sampleRate() const override { return sampleRate_; }
    [[nodiscard]] unsigned int channels() const override { return channels_; }
    [[nodiscard]] const char* name() const override { return name_.c_str(); }

private:
    static int rtCallback(void* output, void* /*input*/, unsigned int nFrames,
                          double /*streamTime*/, RtAudioStreamStatus /*status*/,
                          void* user) {
        auto* self = static_cast<RtAudioBackend*>(user);
        if (self->render_) {
            self->render_(static_cast<float*>(output), nFrames, self->user_);
        }
        return 0;
    }

    RtAudio        dac_;
    RenderCallback render_     = nullptr;
    void*          user_       = nullptr;
    unsigned int   sampleRate_ = 0;
    unsigned int   channels_   = 0;
    bool           running_    = false;
    std::string    name_       = "RtAudio";
};

} // namespace

std::unique_ptr<AudioBackend> createRtAudioBackend(bool preferAsio) {
    return std::make_unique<RtAudioBackend>(preferAsio);
}

} // namespace rope::backends

#endif // ROPE_AUDIO_BACKEND_RTAUDIO
