/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TG_CLOCKOUT101_H
#define TG_CLOCKOUT101_H
#include <stdint.h>

/* MIDI clock out from the internal transport: 24 pulses per quarter (six per
 * straight sixteenth), counted in audio frames with the same step length the
 * sequencer uses, so the clock cannot drift from the music. Swing stays the
 * Elite's own: the clock is straight, as MIDI clock is. Audio-ISR owned. */
enum { TG_CLOCKOUT_MAX_EVENTS = 8 };
enum { TG_MIDI_CLOCK = 0xf8, TG_MIDI_START = 0xfa, TG_MIDI_STOP = 0xfc };

typedef struct {
    uint32_t frame;      /* frames into the current sixteenth */
    uint8_t pulse;       /* next pulse index in the sixteenth, 0..6 */
    uint8_t running;
} tg_clockout;

/* Advance one block of `frames`. running: the internal transport plays.
 * Writes up to TG_CLOCKOUT_MAX_EVENTS status bytes (Start, Clock, Stop), in
 * order, into out; returns how many. */
unsigned tg_clockout_block(tg_clockout *c, unsigned running, unsigned tempo, unsigned frames,
                           uint8_t out[TG_CLOCKOUT_MAX_EVENTS]);
#endif
