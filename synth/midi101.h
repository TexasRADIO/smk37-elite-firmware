/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MIDI101_H
#define MIDI101_H
#include "telegram101.h"
/* USB cable 0. Foreground-owned, bounded to 128 held notes. */
typedef struct {
    uint8_t order[128], velocity[128], down[16], pedal_held[16], arp_held[16];
    uint8_t count, sustain, arp_latch, mod, trigger, selected;
    int16_t bend;
    uint8_t after_silence, channel; /* Zero-based receive channel. */
    uint32_t silence;
    uint32_t accepted, ignored;
} tg_midi;
void tg_midi_release(tg_midi *);
void tg_midi_set_channel(tg_midi *, unsigned channel);
void tg_midi_set_arp_latch(tg_midi *, unsigned enabled);
int tg_midi_packet(tg_midi *, uint32_t packet);
unsigned tg_midi_note(const tg_midi *, unsigned trigger_mode);
static inline unsigned tg_midi_velocity(const tg_midi *m, unsigned note)
{
    return note ? m->velocity[note == TG_NOTE_ZERO ? 0 : note] : 0;
}
/* Pitch or press order, including sustain-owned notes. MIDI 0 uses its token. */
unsigned tg_midi_arp_notes(const tg_midi *, uint8_t notes[TG_ARP_NOTES], unsigned press_order);
#endif
