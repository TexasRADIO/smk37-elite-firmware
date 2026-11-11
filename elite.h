/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BB_ELITE_H
#define BB_ELITE_H

#include "fm1_block.h"
#include "bb_core.h"
#include "bb_dillo.h"
#include "bb_telegram.h"
#include "bb_output.h"
#include "drum808.h"
#ifdef ELITE_DRUM_CACHE
#include "board/drum_pcm.h"
#endif

enum elite_channel { ELITE_DRUM_A, ELITE_DRUM_B, ELITE_DILLO, ELITE_TELEGRAM, ELITE_CHANNELS };
enum { ELITE_DRUMS = 2, ELITE_GAIN_RAMP = 64 };

typedef struct {
    float left, right, target_left, target_right, delta_left, delta_right;
    unsigned remaining;
} elite_fader;

/* Place this state in the audio owner's static storage. Serialize every API
 * call with render. Board code must hand off commands at block
 * boundaries and keep this state private to the audio owner. */
typedef struct {
    bb_core clock;
#ifdef ELITE_DRUM_CACHE
    drum_playback drums[ELITE_DRUMS][D8_NUM];
#else
    drum808_t drums[ELITE_DRUMS];
#endif
    bb_dillo dillo;
    bb_telegram telegram;
    bb_output output;
    uint8_t live_note[BB_SYNTHS], live_velocity[BB_SYNTHS];
    elite_fader mix[ELITE_CHANNELS];
    uint64_t hits[ELITE_DRUMS];
    uint64_t notes[BB_SYNTHS];
    float left[FM1_BLOCK_FRAMES], right[FM1_BLOCK_FRAMES];
    float mono[FM1_BLOCK_FRAMES];
#ifndef ELITE_DRUM_CACHE
    float delay[FM1_BLOCK_FRAMES], reverb[FM1_BLOCK_FRAMES];
#endif
    unsigned offset;
} elite;

void elite_init(elite *);
int elite_set_drum_pattern(elite *, unsigned drum, const bb_drum_step *, unsigned length);
int elite_set_notes(elite *, unsigned synth, const bb_note *, unsigned length);
int elite_set_drum_kit(elite *, unsigned drum, unsigned kit);
int elite_set_synth_patch(elite *, unsigned synth, unsigned patch);
/* Level 0..127, pan -100..100. Mute ramps gain to zero while the pattern
 * and voice state keep advancing. Set before Start for immediate level. */
int elite_set_mix(elite *, unsigned channel, unsigned level, int pan, int mute);
void elite_start(elite *);
void elite_stop(elite *);
int elite_strike(elite *, unsigned drum, unsigned lane, unsigned velocity);
int elite_live_note(elite *, unsigned synth, unsigned note, unsigned velocity);
/* OPEN-compatible output shape. Signed Q23, interleaved stereo, 128 frames.
 * Caller supplies the complete destination. Invalid calls leave it intact.
 * This is a standalone renderer entry point with no board/SDK runtime calls. */
int elite_render(elite *, int32_t *pcm, unsigned frames);

#endif
