/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TG_FX101_H
#define TG_FX101_H

#include <stdint.h>

enum { TG_DELAY_SIZE = 16384, TG_ROOM_SIZE = 2048 };
typedef struct {
    int16_t delay[TG_DELAY_SIZE], room[4][TG_ROOM_SIZE];
    int32_t damp[4];
    uint16_t delay_pos, room_pos[4];
    uint16_t delay_send, room_send;
    uint16_t delay_age, room_age[4];
    uint16_t tap[2], tap_goal[2]; /* echo taps L/R in samples; zero until a tempo is set (then 11025/14700) */
    uint32_t eighth;              /* the eighth note the goals were set from, in samples */
} tg_fx;

/* PI32 emits a signed divide for these constant divisors. Bias negative
 * inputs before the arithmetic shift to retain C's round toward zero. */
_Static_assert((-1 >> 1) == -1, "FX fixed-point math requires arithmetic right shift");
static inline int32_t tg_fx_trunc_shift(int32_t value, unsigned bits)
{
    int32_t bias = (int32_t)((uint32_t)(value >> 31) & ((1u << bits) - 1u));
    return (value + bias) >> bits;
}

/* Echoes in time: left on 1/8, right on a 1/4 triplet (at 120 BPM exactly the old fixed 11025 and
 * 14700 samples). A tap too long for the line halves until it fits. Taps glide one sample per sample to a
 * new length, as a tape delay would. Call outside or inside the audio interrupt, before rendering. */
void tg_fx_set_tempo(tg_fx *, unsigned tempo);
/* The same from an eighth note in samples. slack_permille ignores smaller changes: an external clock's
 * beat estimate wanders a little, and following it would bend the echoes' pitch (after fm1-x0x 1.2.2). */
void tg_fx_set_eighth(tg_fx *, uint32_t eighth, unsigned slack_permille);
uint16_t tg_fx_tap_for(unsigned samples_at_tempo);

/* Constant work: invalidate old taps without clearing the sample arrays. */
void tg_fx_clear(tg_fx *);

void tg_fx_render(tg_fx *, int32_t *stereo, unsigned frames,
                  unsigned delay_send, unsigned room_send, unsigned master);

/* Mix pre-scaled mono auxiliary inputs into a stereo dry bus with a linked soft knee. */
void tg_fx_render_bus(tg_fx *, int32_t *stereo, const int32_t *delay, const int32_t *room,
                      unsigned frames, unsigned master);

#endif
