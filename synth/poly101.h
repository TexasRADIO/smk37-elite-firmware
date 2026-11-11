/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef POLY101_H
#define POLY101_H

#include "telegram101.h"

enum { TG_POLY_VOICES = 3 };

typedef struct {
    tg_voice voice;
    int32_t last, stolen_sample;
    uint8_t note, held, fade_left;
} tg_poly_slot;

typedef struct {
    tg_poly_slot slots[TG_POLY_VOICES];
    uint32_t silence;
} tg_poly;

/* Start with zeroed state. Pass live note tokens oldest first. The newest
 * three distinct pitches sound together. Release tails use the same pool. */
void tg_poly_apply(tg_poly *, const tg_controls *, const uint8_t *notes, unsigned count);
/* Scratch and output must be distinct, each with frames * 2 words. */
int tg_poly_render(tg_poly *, int32_t *output, int32_t *scratch, unsigned frames);

#endif
