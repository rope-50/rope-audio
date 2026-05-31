#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "rope/rope.h"
#include "wav_util.hpp"

TEST(CAbi, OfflineRenderRoundtrip) {
    EXPECT_EQ(rope_abi_version(),
              (ROPE_ABI_VERSION_MAJOR << 16) | ROPE_ABI_VERSION_MINOR);

    rope_engine_t e = rope_engine_create();
    ASSERT_NE(e, nullptr);

    rope_config cfg;
    rope_config_default(&cfg);
    cfg.backend = ROPE_BACKEND_NULL;
    EXPECT_EQ(rope_engine_start(e, &cfg), ROPE_OK);
    EXPECT_EQ(rope_engine_is_running(e), 1);
    EXPECT_EQ(rope_engine_channels(e), 2u);

    std::vector<float> samples(1000, 0.5f);
    auto wav = test::makeWavPcm16(1, 48000, samples);
    rope_sound s = rope_load_wav_memory(e, wav.data(), wav.size());
    ASSERT_NE(s, ROPE_INVALID_SOUND);

    rope_play_params p;
    rope_play_params_default(&p);
    p.pan = -1.0f;
    rope_voice v = rope_play(e, s, &p);
    EXPECT_NE(v, ROPE_INVALID_VOICE);

    std::vector<float> out(64 * 2, 0.0f);
    rope_render_offline(e, out.data(), 64);
    EXPECT_NEAR(out[0], 0.5f, 0.02f);    // left
    EXPECT_NEAR(out[1], 0.0f, 0.001f);   // right silent

    EXPECT_EQ(rope_engine_stop(e), ROPE_OK);
    rope_engine_destroy(e);
}

TEST(CAbi, NullSafety) {
    EXPECT_EQ(rope_engine_start(nullptr, nullptr), ROPE_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(rope_engine_is_running(nullptr), 0);
    EXPECT_EQ(rope_play(nullptr, 0, nullptr), ROPE_INVALID_VOICE);
    rope_render_offline(nullptr, nullptr, 0); // no crash
    rope_engine_destroy(nullptr);             // no crash
}

TEST(CAbi, RejectsNonFiniteParams) {
    rope_engine_t e = rope_engine_create();
    ASSERT_NE(e, nullptr);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(rope_set_master_volume(e, nan), ROPE_ERR_INVALID_ARGUMENT);
    rope_engine_destroy(e);
}
