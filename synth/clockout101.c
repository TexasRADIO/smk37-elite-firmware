/* SPDX-License-Identifier: GPL-3.0-only */
#include "synth/clockout101.h"
#include "synth/telegram101.h"

static unsigned step_frames(unsigned tempo)
{
    if (tempo < 30) tempo = 30;
    return (TG_RATE * 15u + tempo / 2u) / tempo;   /* telegram101.c clock_step, straight */
}

unsigned tg_clockout_block(tg_clockout *c, unsigned running, unsigned tempo, unsigned frames,
                           uint8_t out[TG_CLOCKOUT_MAX_EVENTS])
{
    unsigned n = 0;
    if (!running) {
        if (c->running) out[n++] = TG_MIDI_STOP;
        c->running = 0;
        c->frame = 0;
        c->pulse = 0;
        return n;
    }
    if (!c->running) {
        out[n++] = TG_MIDI_START;
        c->running = 1;
        c->frame = 0;
        c->pulse = 0;
    }
    unsigned interval = step_frames(tempo), end = c->frame + frames;
    while (n < TG_CLOCKOUT_MAX_EVENTS) {
        if (c->pulse >= 6u) {
            if (end < interval) break;
            end -= interval;
            c->pulse = 0;
            continue;
        }
        unsigned at = c->pulse * interval / 6u;    /* pulse k of the sixteenth at floor(k * interval / 6) */
        if (at >= end) break;
        out[n++] = TG_MIDI_CLOCK;
        c->pulse++;
    }
    if (c->pulse >= 6u && end >= interval) { end -= interval; c->pulse = 0; }
    c->frame = end;
    return n;
}
