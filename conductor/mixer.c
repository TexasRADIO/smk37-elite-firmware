/* SPDX-License-Identifier: GPL-3.0-only */
#include "mixer.h"
#include <string.h>

enum { SLEW = 2 };

static unsigned approach(unsigned value, unsigned target)
{
    if (value + SLEW < target) return value + SLEW;
    if (target + SLEW < value) return value - SLEW;
    return target;
}

static int32_t overdrive(int32_t sample, unsigned scale)
{
    int32_t input = tg_fx_trunc_shift(sample, 8);
    unsigned magnitude = (unsigned)(input < 0 ? -input : input);
    magnitude = magnitude * scale / 128u;
    if (magnitude > 16384) {
        unsigned excess = magnitude - 16384u;
        if (excess > 32767u) excess = 32767u;
        magnitude = 16384u + excess - excess * excess / 65536u;
        if (magnitude > 32767u) magnitude = 32767u;
    }
    return input < 0 ? -(int32_t)(magnitude * 256u) : (int32_t)(magnitude * 256u);
}

/* Specialize each span for moving or settled gains. The output buses
 * alias their inputs, so consume all lanes before writing each frame. */
static inline __attribute__((always_inline))
void mix_frames(conductor_mix_state *state, int32_t *const input[], unsigned lanes,
                const uint16_t target[][MIX_DRIVE], const uint16_t drive_scale[],
                unsigned first, unsigned end, unsigned moving)
{
    unsigned signal_lanes = state->signal_lanes;
    for (unsigned frame = first; frame < end; frame++) {
        unsigned offset = frame * 2u;
        int32_t left = 0, right = 0, echo = 0, room = 0;
        for (unsigned lane = 0; lane < lanes; lane++) {
            uint16_t *g = state->gain[lane];
            if (moving) for (unsigned bus = 0; bus < MIX_DRIVE; bus++)
                g[bus] = (uint16_t)approach(g[bus], target[lane][bus]);
            unsigned dry_offset = lane == MIX_TELEGRAM ? offset : frame * CONDUCTOR_DRY_STRIDE;
            int32_t mono = input[lane][dry_offset];
            if (!mono) continue;
            if (drive_scale[lane]) mono = overdrive(mono, drive_scale[lane]);
            /* All gains are nonnegative. Share the sample's sign across
             * the four round-toward-zero shifts, including zero gains. */
            int32_t sign = mono >> 31;
            int32_t stereo_bias = sign & 255;
            int32_t lane_left = (mono * g[0] + stereo_bias) >> 8;
            int32_t lane_right = (mono * g[1] + stereo_bias) >> 8;
            left += lane_left;
            right += lane_right;
            if (!(signal_lanes & (1u << lane)) &&
                ((uint32_t)(lane_left + 256) > 512u || (uint32_t)(lane_right + 256) > 512u))
                signal_lanes |= 1u << lane;
            echo += (mono * g[2] + (sign & 65535)) >> 16;
            room += (mono * g[3] + (sign & 131071)) >> 17;
        }
        input[MIX_TELEGRAM][offset] = left;
        input[MIX_TELEGRAM][offset + 1] = right;
        input[MIX_DILLO][frame] = echo;
        input[MIX_808][frame] = room;
    }
    state->signal_lanes = (uint8_t)signal_lanes;
}

void conductor_prepare_mix(conductor_mix_targets *targets, const conductor_mixer *mixer, const uint8_t *mix_b)
{
    unsigned lanes = mix_b ? MIX_AUDIO_LANES : MIX_LANES;
    for (unsigned lane = 0; lane < lanes; lane++) {
        const uint8_t *v = lane == MIX_808_B ? mix_b : mixer->value[lane];
        /* Telegram has already applied its patch drive. */
        targets->drive_scale[lane] = lane == MIX_TELEGRAM || !v[MIX_DRIVE] ? 0 : (uint16_t)(128u + 4u * v[MIX_DRIVE]);
        uint16_t *target = targets->gain[lane];
        unsigned level = (v[MIX_LEVEL] * 256u + 63u) / 127u;
        unsigned pan = v[MIX_PAN];
        target[0] = (uint16_t)(pan <= 64 ? level : (level * (127u - pan) + 31u) / 63u);
        target[1] = (uint16_t)(pan >= 64 ? level : (level * pan + 32u) / 64u);
        target[2] = (uint16_t)((level * v[MIX_DELAY] + 63u) / 127u);
        target[3] = (uint16_t)((level * v[MIX_ROOM] + 63u) / 127u);
    }
}

void conductor_mix_audio_prepared(conductor_mix_state *state, tg_fx *fx, int32_t *telegram, int32_t *dillo,
                                  int32_t *drums, int32_t *drums_b, unsigned frames,
                                  const conductor_mix_targets *targets, unsigned master)
{
    unsigned lanes = drums_b ? MIX_AUDIO_LANES : MIX_LANES;
    int32_t *const input[] = {telegram, dillo, drums, drums_b};
    const uint16_t (*target)[MIX_DRIVE] = targets->gain;
    unsigned target_bytes = lanes * sizeof target[0];
    if (!state->initialized) {
        memcpy(state->gain, target, target_bytes);
        state->initialized = 1;
    }
    if (drums_b && !state->b_initialized) {
        memcpy(state->gain[MIX_808_B], target[MIX_808_B], sizeof target[0]);
        state->b_initialized = 1;
    } else if (!drums_b) state->b_initialized = 0;
    unsigned ramp_frames = 0;
    if (memcmp(state->gain, target, target_bytes)) {
        unsigned longest = 0;
        for (unsigned lane = 0; lane < lanes; lane++) {
            for (unsigned bus = 0; bus < MIX_DRIVE; bus++) {
                unsigned from = state->gain[lane][bus], to = target[lane][bus];
                unsigned distance = from > to ? from - to : to - from;
                if (distance > longest) longest = distance;
            }
        }
        ramp_frames = (longest + SLEW - 1u) / SLEW;
    }
    if (ramp_frames > frames) ramp_frames = frames;
    state->signal_lanes = 0;
    if (ramp_frames) mix_frames(state, input, lanes, target, targets->drive_scale, 0, ramp_frames, 1);
    mix_frames(state, input, lanes, target, targets->drive_scale, ramp_frames, frames, 0);
    tg_fx_render_bus(fx, telegram, dillo, drums, frames, master);
}

void conductor_mix_audio(conductor_mix_state *state, tg_fx *fx, int32_t *telegram, int32_t *dillo,
                         int32_t *drums, int32_t *drums_b, unsigned frames, const conductor_mixer *mixer,
                         const uint8_t *mix_b, unsigned master)
{
    conductor_mix_targets targets;
    conductor_prepare_mix(&targets, mixer, drums_b ? mix_b : NULL);
    conductor_mix_audio_prepared(state, fx, telegram, dillo, drums, drums_b, frames, &targets, master);
}
