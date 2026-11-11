/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ELITE_CONDUCTOR_MIXER_H
#define ELITE_CONDUCTOR_MIXER_H

#include <stdint.h>
#include "dry_bus.h"
#include "../synth/fx101.h"

enum { MIX_TELEGRAM, MIX_DILLO, MIX_808, MIX_LANES, MIX_808_B = MIX_LANES, MIX_AUDIO_LANES };
enum { MIX_LEVEL, MIX_PAN, MIX_DELAY, MIX_ROOM, MIX_DRIVE, MIX_CONTROLS };
enum { CONDUCTOR_DILLO_START_DRIVE = 48, CONDUCTOR_TELEGRAM_START_DRIVE = 36 };
typedef struct { uint8_t value[MIX_LANES][MIX_CONTROLS]; } conductor_mixer;

#define CONDUCTOR_MIX_DEFAULT {{{127,64,0,0,0}, {127,64,0,0,0}, {127,64,0,0,0}}}

typedef struct {
    uint16_t gain[MIX_AUDIO_LANES][MIX_DRIVE];
    uint8_t initialized, b_initialized, signal_lanes;
} conductor_mix_state;

typedef struct {
    uint16_t gain[MIX_AUDIO_LANES][MIX_DRIVE], drive_scale[MIX_AUDIO_LANES];
} conductor_mix_targets;

/* Derive targets outside the audio interrupt. Include B when it may render. */
void conductor_prepare_mix(conductor_mix_targets *, const conductor_mixer *, const uint8_t *mix_b);

/* Telegram is interleaved dual-mono Q23. Each side buffer uses
 * CONDUCTOR_DRY_STRIDE words per sample and can become an aux input in
 * place. Telegram's buffer becomes the stereo output. */
void conductor_mix_audio_prepared(conductor_mix_state *, tg_fx *, int32_t *telegram, int32_t *dillo,
                                  int32_t *drums, int32_t *drums_b, unsigned frames,
                                  const conductor_mix_targets *, unsigned master);
void conductor_mix_audio(conductor_mix_state *, tg_fx *, int32_t *telegram, int32_t *dillo,
                         int32_t *drums, int32_t *drums_b, unsigned frames, const conductor_mixer *,
                         const uint8_t *mix_b, unsigned master);

#endif
