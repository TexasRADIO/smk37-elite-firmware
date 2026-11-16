/* SPDX-License-Identifier: GPL-3.0-only */
#include "midi101.h"
#include <string.h>

void tg_midi_release(tg_midi *m)
{
    memset(m->down, 0, sizeof m->down);
    memset(m->pedal_held, 0, sizeof m->pedal_held);
    memset(m->arp_held, 0, sizeof m->arp_held);
    m->count = m->sustain = m->mod = m->after_silence = 0;
    m->bend = 0;
}
void tg_midi_set_channel(tg_midi *m, unsigned channel)
{
    if (channel > 15 || m->channel == channel) return;
    tg_midi_release(m);
    m->channel = (uint8_t)channel;
}

static void remove_note(tg_midi *m, unsigned note)
{
    for (unsigned i = 0; i < m->count; i++) {
        if (m->order[i] != note) continue;
        for (unsigned j = i + 1; j < m->count; j++) m->order[j - 1] = m->order[j];
        m->count--; return;
    }
}
static void prune_notes(tg_midi *m)
{
    unsigned n = 0;
    for (unsigned i = 0; i < m->count; i++) {
        unsigned note = m->order[i];
        unsigned byte = note / 8u;
        unsigned owned = m->down[byte] | m->pedal_held[byte] | m->arp_held[byte];
        if (owned & (1u << (note % 8u))) m->order[n++] = (uint8_t)note;
    }
    m->count = (uint8_t)n;
}

static unsigned any_key_down(const tg_midi *m)
{
    for (unsigned i = 0; i < sizeof m->down; i++)
        if (m->down[i]) return 1;
    return 0;
}

void tg_midi_set_arp_latch(tg_midi *m, unsigned enabled)
{
    if (m->arp_latch == enabled) return;
    m->arp_latch = (uint8_t)enabled;
    if (enabled) memcpy(m->arp_held, m->down, sizeof m->down);
    else {
        memset(m->arp_held, 0, sizeof m->arp_held);
        prune_notes(m);
    }
}

int tg_midi_packet(tg_midi *m, uint32_t p)
{
    unsigned cin = p & 15u, status = (p >> 8) & 255u;
    unsigned a = (p >> 16) & 255u, b = p >> 24;
    if ((p & 240u) || (status & 15u) != m->channel || a > 127 || b > 127 ||
        (status >> 4) != cin || (cin != 8 && cin != 9 && cin != 11 && cin != 14)) {
        m->ignored++; return 0;
    }
    if (cin == 8 || cin == 9) {
        unsigned byte = a / 8u;
        uint8_t bit = (uint8_t)(1u << (a % 8u));
        if (cin == 9 && b) {
            if (m->arp_latch && !any_key_down(m)) {
                memset(m->arp_held, 0, sizeof m->arp_held);
                prune_notes(m);
            }
            remove_note(m, a); /* Duplicate presses change priority, not ownership count. */
            m->order[m->count++] = (uint8_t)a;
            m->velocity[a] = (uint8_t)b;
            m->down[byte] |= bit;
            m->pedal_held[byte] &= (uint8_t)~bit;
            if (m->arp_latch) m->arp_held[byte] |= bit;
            m->selected = (uint8_t)a;
            m->trigger++;
            m->after_silence = 1;
        } else {
            if (m->sustain && (m->down[byte] & bit)) m->pedal_held[byte] |= bit;
            m->down[byte] &= (uint8_t)~bit;
            if (!((m->pedal_held[byte] | m->arp_held[byte]) & bit)) remove_note(m, a);
        }
    } else if (cin == 14) {
        m->bend = (int16_t)(((int)(a | (b << 7)) - 8192) / 16);
    } else if (a == 1) m->mod = (uint8_t)b;
    else if (a == 64) {
        m->sustain = b >= 64;
        if (!m->sustain) {
            memset(m->pedal_held, 0, sizeof m->pedal_held);
            prune_notes(m);
        }
    } else if (a == 120) {
        memset(m->down, 0, sizeof m->down);
        memset(m->pedal_held, 0, sizeof m->pedal_held);
        memset(m->arp_held, 0, sizeof m->arp_held);
        m->count = m->after_silence = 0;
        m->silence++;
    } else if (a == 123) {
        if (m->sustain)
            for (unsigned i = 0; i < sizeof m->down; i++) m->pedal_held[i] |= m->down[i];
        memset(m->down, 0, sizeof m->down);
        prune_notes(m);
    } else if (a == 121) {
        m->sustain = m->mod = 0;
        m->bend = 0;
        memset(m->pedal_held, 0, sizeof m->pedal_held);
        prune_notes(m);
    } else { m->ignored++; return 0; }
    m->accepted++; return 1;
}
unsigned tg_midi_note(const tg_midi *m, unsigned trigger_mode)
{
    if (!m->count) return 0;
    unsigned note = 127;
    for (unsigned i = 0; i < m->count; i++) {
        if (trigger_mode == TG_GATE_TRIG && m->order[i] == m->selected)
            return m->selected ? m->selected : TG_NOTE_ZERO;
        if (m->order[i] < note) note = m->order[i];
    }
    return note ? note : TG_NOTE_ZERO;
}

unsigned tg_midi_arp_notes(const tg_midi *m, uint8_t notes[TG_ARP_NOTES], unsigned press_order)
{
    if (press_order) {
        for (unsigned i = 0; i < m->count; i++)
            notes[i] = m->order[i] ? m->order[i] : TG_NOTE_ZERO;
        return m->count;
    }
    /* A fixed bitmap sorts by pitch without sorting/mutating press ownership.
     * The 16-byte scratch and two bounded passes stay in the foreground. */
    uint8_t present[16] = {0};
    for (unsigned i = 0; i < m->count; i++) {
        unsigned note = m->order[i];
        present[note / 8u] |= (uint8_t)(1u << (note % 8u));
    }
    unsigned count = 0;
    for (unsigned note = 0; note < TG_ARP_NOTES; note++)
        if (present[note / 8u] & (1u << (note % 8u)))
            notes[count++] = (uint8_t)(note ? note : TG_NOTE_ZERO);
    return count;
}
