#ifndef SCOPE101_H
#define SCOPE101_H

#include <stdint.h>

/* Telegram's live waveform for the play page. The audio interrupt writes Telegram's own voice (left
 * channel, before drums and Dillo are mixed in) into a ring; the screen picks a rising zero crossing,
 * shows about three periods of the sounding note and scales quiet sounds up, at most 4x. The idea
 * comes from OG Synth's pitch-synced scope; the code is ours. */
/* The ring holds every other sample (pairs averaged, 22.05 kHz): 2048 entries are 93 ms, three periods
 * down to about E1. */
enum { TG_SCOPE_RING = 2048, TG_SCOPE_DECIMATE = 2, TG_SCOPE_PERIODS = 3, TG_SCOPE_MAX_GAIN = 4,
       TG_SCOPE_BLOCK_GUARD = 128 /* oldest entries the interrupt may be overwriting */ };

typedef struct {
    int16_t ring[TG_SCOPE_RING];
    volatile uint16_t head; /* next write; only the audio interrupt moves it, after a whole block */
    int32_t odd; uint8_t half; /* a pair's first sample waiting for its second */
} tg_scope;

/* Audio interrupt: take the left channel of interleaved 24-bit stereo, any frame count. */
void tg_scope_write(tg_scope *s, const int32_t *stereo, unsigned frames);

/* Samples per period of a MIDI note at 44.1 kHz, in 1/16ths; 0 means no note. */
uint32_t tg_scope_period16(unsigned note);

/* Screen: fill y[0..columns) with offsets from the centre line, each within +-half. Returns 0 when the
 * window is silent (nothing to draw), else 1. period16 = 0 shows a fixed 512-sample window. */
unsigned tg_scope_trace(const tg_scope *s, uint32_t period16, int8_t *y, unsigned columns, unsigned half);

#endif
