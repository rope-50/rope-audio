// Minimal demo of rope::AudioEngine.
//
//   play_wav kick.wav snare.wav hat.wav
//
// loads each WAV and triggers them all simultaneously, then plays for a few
// seconds so you can hear them mixed together.

#include "rope/AudioEngine.hpp"

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

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

    // Fire them all at once.
    for (const rope::SoundHandle s : sounds) {
        engine.play(s, rope::PlayParams{ .gain = 0.7f, .loop = false });
    }

    std::printf("playing %zu sound(s) simultaneously for 5 seconds...\n",
                sounds.size());
    std::this_thread::sleep_for(std::chrono::seconds(5));

    engine.stop();
    std::printf("done\n");
    return 0;
}
