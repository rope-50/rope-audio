// Minimal demo of rope::AudioEngine.
//
//   play_wav kick.wav snare.wav hat.wav
//
// Loads each WAV and triggers them all at once, spreading them across the
// stereo field (pan), applying a master volume, and draining engine events
// (e.g. "voice finished") while they play.

#include "rope/AudioEngine.hpp"

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

static const char* eventName(rope::EventType t) {
    switch (t) {
    case rope::EventType::VoiceFinished:   return "VoiceFinished";
    case rope::EventType::VoicesExhausted: return "VoicesExhausted";
    case rope::EventType::QueueOverflow:   return "QueueOverflow";
    case rope::EventType::Suspended:       return "Suspended";
    case rope::EventType::Resumed:         return "Resumed";
    default:                               return "None";
    }
}

int main(int argc, char** argv) {
    rope::AudioEngine engine;

    if (!engine.start(/*sampleRate=*/48000, /*bufferFrames=*/512)) {
        std::fprintf(stderr, "failed to start the audio engine\n");
        return 1;
    }
    std::printf("engine running at %u Hz, %u channels\n",
                engine.sampleRate(), engine.outputChannels());

    if (argc < 2) {
        std::printf("usage: %s <file1.wav> [file2.wav ...]\n", argv[0]);
        engine.stop();
        return 0;
    }

    std::vector<rope::SoundHandle> sounds;
    for (int i = 1; i < argc; ++i) {
        const rope::SoundHandle h = engine.loadWav(argv[i]);
        if (h == rope::kInvalidSound) continue;
        sounds.push_back(h);
        std::printf("loaded [%d] %s\n", static_cast<int>(h), argv[i]);
    }

    // Master volume below unity so the summed voices don't clip.
    engine.setMasterVolume(0.8f);

    // Fire them all at once, spread across the stereo field: first sound full
    // left, last sound full right, evenly in between.
    const std::size_t n = sounds.size();
    for (std::size_t i = 0; i < n; ++i) {
        const float pan = (n <= 1) ? 0.0f
                                   : -1.0f + 2.0f * static_cast<float>(i) /
                                                    static_cast<float>(n - 1);
        engine.play(sounds[i], rope::PlayParams{ .gain = 0.7f, .pan = pan, .loop = false });
        std::printf("  -> sound %d at pan %+.2f\n", static_cast<int>(sounds[i]), pan);
    }

    std::printf("playing %zu sound(s) for 5 seconds (master 0.8)...\n", n);

    // Poll engine events each ~50 ms while playing.
    rope::Event ev;
    for (int tick = 0; tick < 100; ++tick) {
        while (engine.pollEvent(ev)) {
            if (ev.type == rope::EventType::VoiceFinished) {
                std::printf("[event] VoiceFinished voice=%llu reason=%d\n",
                            static_cast<unsigned long long>(ev.voice),
                            static_cast<int>(ev.reason));
            } else {
                std::printf("[event] %s\n", eventName(ev.type));
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    engine.stop();
    std::printf("done\n");
    return 0;
}
