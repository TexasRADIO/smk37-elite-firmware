/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "../synth/telegram101.h"
#include "../vendor/bb/x0x/drum808.h"
#ifdef ELITE_PERCUSSION_727
#include "layer727.h"
#endif

enum { DRUM_STEPS = 16 };

typedef struct {
    uint16_t hits[D8_NUM], accents[D8_NUM];
} drum_pattern;

enum { DRUM_TUNE, DRUM_DECAY, DRUM_LEVEL, DRUM_KIT_PARAMS, DRUM_USER_KITS = 3 };
/* The sixteen 808 sounds (D8S_*, 8W8 lane order) on eleven tracks, as on the TR-808:
 * LT, MT and HT play tom or conga, RS rim shot or claves, CP clap or maracas. A kit
 * keeps each track's switch, one bit per switching track, and its three pots act on
 * the sound the switch selects. */
enum { DRUM_SOUNDS = D8S_NUM, DRUM_ALTERNATES = 5, DRUM_ALTERNATE_MASK = (1u << DRUM_ALTERNATES) - 1u };
typedef struct { uint8_t value[D8_NUM][DRUM_KIT_PARAMS]; uint8_t alternate; } drum_kit;
typedef struct {
    float value[D8_NUM][DRUM_KIT_PARAMS];
    bb_drum808_tuning tuning;
} drum_kit_values;

enum { DRUM_ROLL_SWING_MAX = 25 };
enum { DRUM_ROLL_NO_EDGE, DRUM_ROLL_BEFORE_STEP, DRUM_ROLL_ON_STEP, DRUM_ROLL_AFTER_STEP };
typedef struct { uint8_t velocity[D8_NUM], rate, swing; } drum_roll;
typedef struct { uint8_t generation, track, accent; } drum_audition;

/* Odd hit position in a pair, in clock pulses or sixths of a sample. */
static inline unsigned drum_roll_odd(unsigned interval, unsigned swing)
{
    return interval + (2u * interval * swing + 50u) / 100u;
}

typedef struct {
    drum808_t voice;
#ifdef ELITE_PERCUSSION_727
    percussion_layer percussion;
    uint8_t percussion_sounds[D8_NUM];
#endif
    drum_roll roll;
    drum_audition audition;
    drum_kit kit;
    bb_drum808_tuning tuning;
    uint32_t repeat_phase;
    uint16_t repeat_blocked;
    uint32_t silence;
    uint8_t step, head, repeat_tail, audition_seen, audition_tail;
} drum_layer;

static inline int drum_layer_active(const drum_layer *layer)
{
#ifdef ELITE_PERCUSSION_727
    if (layer->percussion.bank) return layer->percussion.active != 0;
#endif
    return drum808_active(&layer->voice);
}

extern const drum_pattern drum_demo, drum_demo_b;
void drum_kit_default(drum_kit *kit);
unsigned drum_track_alternate_bit(unsigned track); /* zero for a track without a switch */
unsigned drum_sound_track(unsigned sound);
unsigned drum_kit_sound(const drum_kit *kit, unsigned track);
/* Selects a sound's switch. A changed switch starts that track at the sound's factory pots. */
int drum_kit_select_sound(drum_kit *kit, unsigned sound);
/* With a previous kit, values already contains its resolved settings. */
void drum_kit_resolve(const drum_kit *kit, const drum_kit *previous, drum_kit_values *values);
unsigned drum_layer_apply_kit(drum_layer *layer, const drum_kit *kit, const drum_kit_values *values);
void drum_layer_init(drum_layer *layer);
void drum_layer_clear(drum_layer *layer);
int drum_layer_mix(drum_layer *layer, int32_t *stereo, unsigned frames,
                   const tg_controls *controls, unsigned next_edge, const drum_pattern *pattern);
int drum_layer_mix_usb(drum_layer *, int32_t *stereo, unsigned frames, const tg_controls *,
                       const drum_pattern *, unsigned next_step, unsigned tick,
                       unsigned roll_edge, unsigned detach);
