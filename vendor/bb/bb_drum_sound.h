/* SPDX-License-Identifier: GPL-3.0-only
 * Portable sound ownership for the pinned X0X/8W8 808 engine. Numeric sound
 * order, pot slots and defaults derive from its licensed drum808 schema.
 * No DSP pointers or oscillator state cross the audio/UI boundary. */
#ifndef BB_DRUM_SOUND_H
#define BB_DRUM_SOUND_H
#define BB_DRUM_SOUND_SCHEMA 1
#include <stdint.h>
#define BB_DRUM_TRACKS 11u
#define BB_DRUM_SOUNDS 16u
#define BB_DRUM_POTS 9u
#define BB_DRUM_KITS 8u
typedef enum {
    BB_DRUM_LEVEL, BB_DRUM_TUNE, BB_DRUM_DECAY, BB_DRUM_DRIVE,
    BB_DRUM_DIST, BB_DRUM_TONE, BB_DRUM_ATTACK, BB_DRUM_SNAPPY,
    BB_DRUM_MODE, BB_DRUM_SPRING, BB_DRUM_ECHO, BB_DRUM_PARAMETER_COUNT
} bb_drum_parameter;
typedef struct {
    uint8_t pot[BB_DRUM_SOUNDS][BB_DRUM_POTS];
    uint8_t mode[BB_DRUM_TRACKS];
    uint8_t kit_level,kit_accent,edited;
} bb_drum_sound;
extern const char *const bb_drum_kit_names[BB_DRUM_KITS];
unsigned bb_drum_sound_variant(unsigned track,unsigned mode);
unsigned bb_drum_sound_index(const bb_drum_sound *,unsigned track);
int bb_drum_parameter_max(const bb_drum_sound *,unsigned track,unsigned parameter);
const char *bb_drum_parameter_name(unsigned parameter);
const char *bb_drum_voice_name(const bb_drum_sound *,unsigned track);
unsigned bb_drum_parameter_get(const bb_drum_sound *,unsigned track,unsigned parameter);
int bb_drum_parameter_set(bb_drum_sound *,unsigned track,unsigned parameter,unsigned value);
int bb_drum_sound_recall(bb_drum_sound *,unsigned kit);
#endif
