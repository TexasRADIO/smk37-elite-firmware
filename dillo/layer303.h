/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef LAYER303_H
#define LAYER303_H

#include "phrase303.h"
#include "../synth/telegram101.h"

enum {
    DILLO_DEFAULT_SOUND = 1, DILLO_USER_SOUNDS = 4,
    DILLO_SOUND_PAGE_SLOTS = BB_DILLO_PATCHES + DILLO_USER_SOUNDS,
    DILLO_EXPANSION_SOUNDS = DILLO_SOUND_PAGE_SLOTS,
    DILLO_CLASSIC_SOUNDS = DILLO_SOUND_PAGE_SLOTS, /* Classic 303 characters, one page. */
    DILLO_CLASSIC_START = DILLO_SOUND_PAGE_SLOTS + DILLO_EXPANSION_SOUNDS,
    DILLO_DEVIL_SOUNDS = DILLO_SOUND_PAGE_SLOTS, /* Devilfish-style extremes, one page. */
    DILLO_DEVIL_START = DILLO_CLASSIC_START + DILLO_CLASSIC_SOUNDS,
    DILLO_SOUND_SLOTS = DILLO_DEVIL_START + DILLO_DEVIL_SOUNDS
};

enum { DILLO_DECAY_MAX = 100, DILLO_DECAY_STEP_MS = 30 };
enum { DILLO_SWEEP_STOCK, DILLO_SWEEP_LAG, DILLO_SWEEP_LONG, DILLO_SWEEP_COUNT };
typedef struct { uint8_t accent, amp; uint16_t sweep; } dillo_decay;
typedef struct {
    bb_dillo_patch tone;
    dillo_decay decay; /* Zero keeps the retained voice's default. */
} dillo_sound;
typedef struct { uint8_t note, velocity, strike, gap, mod, keep_after_panic; int16_t bend; } dillo_keys;

typedef struct {
    bb_dillo voice;
    dillo_phrase_playback phrase;
    uint32_t silence;
    dillo_keys keys, requested_keys;
    dillo_decay decay;
    uint8_t step, head, running, started, blocked_key, live_tail, articulation;
} dillo_layer;

typedef struct { const char *name; dillo_sound sound; } dillo_named_sound;
extern const dillo_named_sound dillo_expansion[DILLO_EXPANSION_SOUNDS];
extern const dillo_named_sound dillo_classic[DILLO_CLASSIC_SOUNDS];
extern const dillo_named_sound dillo_devilfish[DILLO_DEVIL_SOUNDS];
extern const dillo_phrase dillo_demo;
void dillo_layer_init(dillo_layer *);
int dillo_patch_valid(const bb_dillo_patch *);
int dillo_layer_sound(dillo_layer *, const dillo_sound *);
/* Mix after Telegram's effects. next_edge is Telegram's sample countdown
 * before it rendered this span. A zero countdown places an edge at sample 0. */
int dillo_layer_mix(dillo_layer *, int32_t *stereo, unsigned frames,
                    const tg_controls *, unsigned next_edge, const dillo_phrase *pattern);

int dillo_layer_mix_grid(dillo_layer *, int32_t *stereo, unsigned frames, const tg_controls *,
                         unsigned next_edge, unsigned next_step, const dillo_phrase *pattern);

int dillo_layer_mix_usb(dillo_layer *, int32_t *stereo, unsigned frames, const tg_controls *,
                       const dillo_phrase *, unsigned next_step, unsigned events,
                       unsigned detach, unsigned period, unsigned elapsed_frames);

#endif
