/* SPDX-License-Identifier: GPL-3.0-only */
#include "fixture.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void little_endian(FILE *file, uint32_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i)
        fputc((int)((value >> (i * 8u)) & 255u), file);
}

static void wav_header(FILE *file, uint32_t frames)
{
    uint32_t bytes = frames * FM1_CHANNELS * 2u;
    fwrite("RIFF", 1, 4, file);
    little_endian(file, bytes + 36u, 4);
    fwrite("WAVEfmt ", 1, 8, file);
    little_endian(file, 16, 4);
    little_endian(file, 1, 2);
    little_endian(file, FM1_CHANNELS, 2);
    little_endian(file, FM1_SAMPLE_RATE, 4);
    little_endian(file, FM1_SAMPLE_RATE * FM1_CHANNELS * 2u, 4);
    little_endian(file, FM1_CHANNELS * 2u, 2);
    little_endian(file, 16, 2);
    fwrite("data", 1, 4, file);
    little_endian(file, bytes, 4);
}

int main(int argc, char **argv)
{
    const char *names[] = {"drum-a", "drum-b", "dillo", "telegram", "mix", "dense"};
    unsigned scene = 0;
    if (argc == 3)
        while (scene < sizeof names / sizeof names[0] && strcmp(names[scene], argv[1]))
            ++scene;
    if (argc != 3 || scene == sizeof names / sizeof names[0]) {
        fputs("Usage: elite-render {mix|drum-a|drum-b|dillo|telegram|dense} output.wav\n", stderr);
        return 2;
    }

    FILE *file = fopen(argv[2], "wb");
    if (!file) {
        perror(argv[2]);
        return 1;
    }
    static elite state;
    elite_fixture(&state, scene == 5);
    if (scene < ELITE_CHANNELS) {
        static const unsigned levels[] = {100, 80, 64, 82};
        static const int pans[] = {-25, 25, 0, 0};
        for (unsigned i = 0; i < ELITE_CHANNELS; ++i)
            elite_set_mix(&state, i, levels[i], pans[i], i != scene);
    }
    const unsigned play_blocks = FM1_SAMPLE_RATE * 12u / FM1_BLOCK_FRAMES;
    const unsigned tail_blocks = FM1_SAMPLE_RATE * 2u / FM1_BLOCK_FRAMES;
    wav_header(file, (play_blocks + tail_blocks) * FM1_BLOCK_FRAMES);
    elite_start(&state);
    uint64_t total_ns = 0, max_ns = 0;
    uint32_t peak = 0;
    for (unsigned block = 0; block < play_blocks + tail_blocks; ++block) {
        if (block == play_blocks)
            elite_stop(&state);
        int32_t pcm[FM1_BLOCK_WORDS];
        uint64_t begin = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        elite_render(&state, pcm, FM1_BLOCK_FRAMES);
        uint64_t elapsed = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - begin;
        total_ns += elapsed;
        if (elapsed > max_ns)
            max_ns = elapsed;
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) {
            uint32_t magnitude = (uint32_t)(pcm[i] < 0 ? -pcm[i] : pcm[i]);
            if (magnitude > peak)
                peak = magnitude;
            little_endian(file, (uint16_t)(pcm[i] / 256), 2);
        }
    }
    int failed = ferror(file);
    if (fclose(file) != 0)
        failed = 1;
    if (failed) {
        perror(argv[2]);
        return 1;
    }

    printf("{\"scene\":\"%s\",\"blocks\":%u,\"state_bytes\":%zu,"
           "\"hits\":[%" PRIu64 ",%" PRIu64 "],\"notes\":[%" PRIu64 ",%" PRIu64 "],"
           "\"peak_q23\":%u,\"limiter_frames\":%u,\"invalid_frames\":%u,"
           "\"host_mean_ns\":%" PRIu64 ",\"host_max_ns\":%" PRIu64 "}\n",
           argv[1], play_blocks + tail_blocks, sizeof state, state.hits[0], state.hits[1],
           state.notes[0], state.notes[1], peak, state.output.limited, state.output.faults,
           total_ns / (play_blocks + tail_blocks), max_ns);
    return state.output.faults ? 1 : 0;
}
