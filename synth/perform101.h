/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef PERFORM101_H
#define PERFORM101_H
#include <stdint.h>

/* PERFORM: hold FX and play the lowest octave. White keys hold an effect on the whole mix, black keys
 * mute a part, and letting go ends it. After FMini-1's PERFORM layer (GPL-3.0, Leo Kuroshita); the code
 * here is BB-37's own.
 *
 * Buffer effects play from a 16 KB ring of the mix (mono, 22.05 kHz, 371 ms): REPEAT loops the last 1/8,
 * 1/16 or 1/32 of a beat, REVERSE plays the last 1/8 backwards on a loop, TAPE STOP slows the mix to a halt
 * over a beat. Of those held, the last pressed plays. REPEAT and REVERSE freeze the ring while they play;
 * a slice longer than the ring (REPEAT 1/8 or REVERSE below 81 BPM) does nothing. LPF and HPF sweep a
 * resonant filter over a bar and stack with the rest. Every change crossfades over 2.9 ms; with nothing
 * held and the fades done, the mix passes untouched. */
enum { TG_PERF_RING = 8192, TG_PERF_FADE = 128 };
enum { TG_PF_R8, TG_PF_R16, TG_PF_R32, TG_PF_REV, TG_PF_TAPE, TG_PF_LPF, TG_PF_HPF, TG_PF_EFFECTS,
       TG_PF_NONE = 255 };
enum { TG_PF_LANES = 4 }; /* Telegram, Dillo, 808 A, 808 B */

typedef struct {
    int16_t *ring;          /* TG_PERF_RING entries, in the pool */
    uint16_t head;          /* next ring write */
    uint8_t half, mode;     /* a pair's first sample waiting; the buffer effect playing (TG_PF_NONE) */
    int32_t odd;
    uint32_t anchor, length; /* the slice: ring entry it starts at, entries */
    uint32_t pos;           /* Q16 entries into the slice (TAPE: behind the anchor's start) */
    uint32_t speed;         /* TAPE: Q16 entries per output sample */
    uint32_t tape_left, tape_total;
    uint16_t wet;           /* 0..TG_PERF_FADE, buffer effect against the live mix */
    uint8_t filter_on[2];   /* LPF, HPF held */
    uint32_t filter_age[2]; /* samples since pressed */
    float cutoff[2];        /* Hz now */
    float ic1[2][2], ic2[2][2]; /* filter states: [lpf/hpf][left/right] */
    uint16_t lane_gain[TG_PF_LANES]; /* 0..TG_PERF_FADE */
} tg_perform;

void tg_perform_init(tg_perform *, int16_t *ring);
/* The slice in ring entries for a buffer effect at tempo; 0 when it does not fit (or it is not one).
 * The ring is frozen while a slice plays, so a slice may use all but two entries of it. */
static inline uint32_t tg_perform_slice(unsigned effect, unsigned tempo)
{
    if (!tempo) return 0;
    uint32_t beat = 22050u * 60u / tempo;
    uint32_t length = effect == TG_PF_R8 || effect == TG_PF_REV ? beat / 2u :
                      effect == TG_PF_R16 ? beat / 4u : effect == TG_PF_R32 ? beat / 8u : 0u;
    return length && length <= TG_PERF_RING - 2u ? length : 0u;
}
/* On the finished mix, interleaved 24-bit stereo. buffer_fx: the buffer effect held last, or TG_PF_NONE.
 * filters: bit 0 LPF, bit 1 HPF. */
void tg_perform_render(tg_perform *, int32_t *stereo, unsigned frames, unsigned tempo, unsigned buffer_fx,
                       unsigned filters);
/* A part's buffer (stride 2 for interleaved stereo, 1 for mono) faded out while muted. */
void tg_perform_lane(tg_perform *, unsigned lane, int32_t *buffer, unsigned frames, unsigned stride, unsigned muted);
/* Whether anything is still sounding through the effects (the render may not be skipped). */
unsigned tg_perform_busy(const tg_perform *);
#endif
