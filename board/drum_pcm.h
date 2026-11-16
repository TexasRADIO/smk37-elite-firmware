/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ELITE_DRUM_PCM_H
#define ELITE_DRUM_PCM_H

#include <stdint.h>

typedef struct {
    int predictor, index;
} drum_adpcm;

/* IMA ADPCM. Both the offline encoder and the Elite use this decoder. */
static const int16_t drum_step[] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
    598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707,
    1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static inline int drum_decode(drum_adpcm *state, unsigned code)
{
    static const int8_t adjust[] = {-1, -1, -1, -1, 2, 4, 6, 8};
    int step = drum_step[state->index];
    int delta = (step >> 3) + ((code & 4u) ? step : 0) +
                ((code & 2u) ? step >> 1 : 0) + ((code & 1u) ? step >> 2 : 0);
    int next = state->predictor + ((code & 8u) ? -delta : delta);
    int index = state->index + adjust[code & 7u];
    state->predictor = next < -32768 ? -32768 : next > 32767 ? 32767 : next;
    state->index = index < 0 ? 0 : index > 88 ? 88 : index;
    return state->predictor;
}

typedef struct {
    uint32_t offset, length;
    uint8_t initial_index;
} drum_sample;

typedef struct {
    drum_adpcm decoder;
    uint32_t frame;
    float gain;
    uint8_t active, choke;
} drum_playback;

#endif
