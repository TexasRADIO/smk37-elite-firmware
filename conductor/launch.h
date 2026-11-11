/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ELITE_CONDUCTOR_LAUNCH_H
#define ELITE_CONDUCTOR_LAUNCH_H

#include "mixer.h"
#include "../dillo/layer303.h"
#include "../drums/layer808.h"

enum { CONDUCTOR_SLOTS = 16, CONDUCTOR_ROW = 8 };

/* Active synthesized 808 parts. ELITE_SINGLE_808 renders only 808 A and the
 * panel offers one 808; B's patterns, kit and setup references stay saved. */
#ifdef ELITE_SINGLE_808
#define ELITE_DRUM_PARTS 1
#else
#define ELITE_DRUM_PARTS 2
#endif
enum { CUE_DRUM_A = 1, CUE_DRUM_B = 2, CUE_DILLO = 4, CUE_TELEGRAM = 8,
       CUE_RHYTHM = CUE_DRUM_A | CUE_DRUM_B | CUE_DILLO, CUE_ALL = CUE_RHYTHM | CUE_TELEGRAM };

typedef struct {
    drum_pattern pattern;
    drum_roll roll;
    drum_kit kit;
    uint32_t cue;
    uint8_t mix[MIX_CONTROLS];
    uint8_t enabled, slot, repeat, muted;
#ifdef ELITE_PERCUSSION_727
    uint8_t percussion_kit; /* Kit plus one, or zero to keep the current kit. */
#endif
} conductor_drum_part;

typedef struct {
    uint32_t dillo_cue, drums_cue, telegram_cue;
    /* Zero keeps the direct-call contract. A nonzero publication identifies
     * an immutable pair of patterns and controls through both render calls. */
    uint32_t publication;
    drum_pattern drums;
    drum_roll drum_keys;
    drum_audition drum_auditions[2];
    drum_kit kit;
    dillo_keys dillo_live;
    dillo_sound dillo_sound;
    conductor_mixer mixer;
    dillo_phrase dillo;
    conductor_drum_part drums_b;
    uint8_t dillo_on, drums_on, dillo_slot, drums_slot, telegram_slot, has_kit, dillo_articulation, scene;
    uint8_t telegram_library;
    uint8_t drum_init; /* Bit 0 is A, bit 1 is B. */
    uint8_t kit_slots; /* A in low nibble, B in high nibble. */
#ifdef ELITE_BAND_PRESETS
    uint16_t band_outgoing_tempo; /* Nonzero when sound and timing belong to the queued bar. */
    uint8_t band_outgoing_swing, studio_cues;
#endif
    const drum_kit_values *kit_values; /* Optional prepared pair in A/B order. */
} conductor_patterns;

typedef struct {
    dillo_layer dillo;
    drum_layer drums, drums_b;
    conductor_patterns active;
    tg_clock_snapshot clock;
    uint32_t consumed_publication;
    /* Per callback, as sample offset plus one. Zero means no launch.
     * rhythm_bar requires all three rhythm lanes in the same launch. */
    uint16_t telegram_bar, rhythm_bar, rhythm_cue_offset;
    uint8_t rhythm_cues, late_usb_step; /* Lateness since first crossed USB bar, saturated. */
    uint8_t kit_changes; /* Per callback. Bit 0 is A, bit 1 is B. */
    uint8_t drums_b_update; /* Latest publication still needs B's renderer. */
#ifdef ELITE_RENDER_PROFILE
    uint32_t layer_ticks[MIX_AUDIO_LANES];
#endif
} conductor_launch;

void conductor_init(conductor_launch *);
#ifdef ELITE_PERCUSSION_727
/* Configure the B renderer while stopped. The bank must outlive playback. */
int conductor_set_percussion_kit(conductor_launch *, const percussion_bank *, unsigned kit);
int conductor_set_percussion_sound(conductor_launch *, unsigned track, unsigned sound);
#endif
/* Foreground writes only the buffer that the audio request does not own. */
void conductor_prepare_kits(conductor_patterns *next, const conductor_patterns *published,
                            drum_kit_values buffers[2][2]);
int conductor_render_telegram(conductor_launch *, tg_voice *, int32_t *stereo, unsigned frames,
                              const tg_controls *, const conductor_patterns *, unsigned *next_edge);
int conductor_mix(conductor_launch *, int32_t *dillo, int32_t *drums, int32_t *drums_b, unsigned frames,
                  const tg_controls *, unsigned next_edge, const conductor_patterns *);

/* Can be called before clearing or mixing the optional fourth dry buffer. */
int conductor_drums_b_active(const conductor_launch *, const conductor_patterns *);

#endif
