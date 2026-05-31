// Minimal consumer of the installed rope package. Exercises both the C++ API
// (rope::AudioEngine) and the C ABI symbol (rope_abi_version) to prove the
// installed static library links cleanly via find_package(rope).
#include "rope/AudioEngine.hpp"
#include "rope/rope.h"

#include <cstdio>

int main() {
    rope::AudioEngine engine;
    if (!engine.start(48000, 0, rope::BackendType::Null)) {
        std::fprintf(stderr, "consumer: failed to start engine\n");
        return 1;
    }

    float out[128 * 2] = {0};
    engine.renderOffline(out, 128);   // run the mixer once
    const bool running = engine.isRunning();
    engine.stop();

    std::printf("rope consumer OK: abi=0x%08x, running=%d\n",
                rope_abi_version(), running ? 1 : 0);
    return running ? 0 : 1;
}
