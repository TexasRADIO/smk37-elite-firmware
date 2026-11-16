/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef PHRASE303_H
#define PHRASE303_H

#include "bb_dillo.h"

#define DILLO_STEPS 16

enum dillo_event { DILLO_REST, DILLO_NOTE, DILLO_TIE };
enum dillo_articulation { DILLO_ACCENT = 1, DILLO_SLIDE = 2 };

typedef struct {
    uint8_t event, pitch, gate_percent, flags;
} dillo_step;

typedef struct {
    dillo_step steps[DILLO_STEPS];
    uint8_t length, repeat_short;
} dillo_phrase;

/* Older phrases stay on the bar grid. Imported loops can wrap earlier. */
static inline unsigned dillo_phrase_grid(const dillo_phrase *phrase)
{
    return phrase && phrase->repeat_short && phrase->length && phrase->length < DILLO_STEPS ?
           phrase->length : DILLO_STEPS;
}

/* The panel writes one step at a time, retaining its absolute pad position. */
int dillo_phrase_set_step(dillo_phrase *, unsigned step, dillo_step);
/* Classic entry: only NOTE consumes a pitch. Its timing entry's pitch is
 * ignored. Replace the destination only when both passes are complete. */
int dillo_phrase_from_passes(dillo_phrase *, const uint8_t *pitches,
                             unsigned pitch_count, const dillo_step *timing,
                             unsigned length);

typedef struct {
    uint8_t tie_ready, outgoing_slide;
} dillo_phrase_playback;

/* The grid can extend beyond the written phrase. Its unwritten steps are rests.
 * Elapsed frames shorten a finite gate. Held articulation affects onsets. */
void dillo_phrase_tick(dillo_phrase_playback *, bb_dillo *,
                       const dillo_phrase *, unsigned step, unsigned grid, uint32_t frames,
                       uint32_t elapsed, unsigned articulation);
void dillo_phrase_release(dillo_phrase_playback *, bb_dillo *);

#endif
