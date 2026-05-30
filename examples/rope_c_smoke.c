/* Smoke test for the rope C ABI (include/rope/rope.h).
 *
 * Proves the ABI is callable from plain C: create an engine, load one sound
 * from a file and one from a MEMORY buffer (exercising rope_load_wav_memory),
 * play them panned with a master volume, and drain events.
 *
 *   rope_c_smoke [file.wav] [memory.wav]
 *
 * Run from the repo root so the default assets/ paths resolve.
 */
#define _CRT_SECURE_NO_WARNINGS
#include "rope/rope.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <time.h>
static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
#endif

static unsigned char* read_file(const char* path, size_t* out_size) {
    FILE* f;
    long n;
    unsigned char* buf;
    size_t rd;

    f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    buf = (unsigned char*)malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    rd = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (rd != (size_t)n) { free(buf); return NULL; }
    *out_size = (size_t)n;
    return buf;
}

int main(int argc, char** argv) {
    const char* file_wav = (argc > 1) ? argv[1] : "assets/tone_a4_mono.wav";
    const char* mem_wav  = (argc > 2) ? argv[2] : "assets/chord_stereo.wav";

    rope_engine_t e;
    rope_config cfg;
    rope_play_params p;
    rope_result r;
    rope_sound s_file, s_mem;
    unsigned char* bytes;
    size_t sz;
    rope_event ev;
    int i;

    printf("rope ABI version: 0x%08x\n", rope_abi_version());

    e = rope_engine_create();
    if (!e) { fprintf(stderr, "engine create failed\n"); return 1; }

    rope_config_default(&cfg);
    r = rope_engine_start(e, &cfg);
    if (r != ROPE_OK) {
        fprintf(stderr, "start failed: %s\n", rope_result_str(r));
        rope_engine_destroy(e);
        return 1;
    }
    printf("engine: %u Hz, %u ch\n",
           rope_engine_sample_rate(e), rope_engine_channels(e));

    s_file = rope_load_wav_file(e, file_wav);
    printf("load file   '%s' -> sound %u\n", file_wav, s_file);

    s_mem = ROPE_INVALID_SOUND;
    sz = 0;
    bytes = read_file(mem_wav, &sz);
    if (bytes) {
        s_mem = rope_load_wav_memory(e, bytes, sz);
        free(bytes); /* engine copied the decoded samples; buffer no longer needed */
        printf("load memory '%s' (%lu bytes) -> sound %u\n",
               mem_wav, (unsigned long)sz, s_mem);
    } else {
        printf("could not read '%s' for memory test\n", mem_wav);
    }

    rope_set_master_volume(e, 0.8f);

    rope_play_params_default(&p);
    if (s_file != ROPE_INVALID_SOUND) { p.gain = 0.7f; p.pan = -1.0f; rope_play(e, s_file, &p); }
    if (s_mem  != ROPE_INVALID_SOUND) { p.gain = 0.7f; p.pan = +1.0f; rope_play(e, s_mem,  &p); }

    printf("playing ~4s, polling events...\n");
    for (i = 0; i < 80; ++i) {
        while (rope_poll_event(e, &ev)) {
            if (ev.type == ROPE_EVENT_VOICE_FINISHED) {
                printf("[event] VoiceFinished voice=%llu reason=%d\n",
                       (unsigned long long)ev.voice, (int)ev.reason);
            } else {
                printf("[event] type=%d data=%u\n", (int)ev.type, ev.data);
            }
        }
        sleep_ms(50);
    }

    rope_engine_stop(e);
    rope_engine_destroy(e);
    printf("done\n");
    return 0;
}
