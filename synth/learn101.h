/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef LEARN101_H
#define LEARN101_H
#include <stdint.h>
#include <string.h>

/* MIDI learn: up to 16 controller CCs, each set to one of Telegram's controls (the ASSIGN page's
 * destinations). A CC sent while ASSIGN is open becomes the page's source; the arrows choose where it
 * goes, OFF included. Continuous controls pick up like the faders: the CC takes over once it reaches
 * or crosses the control's value, so nothing jumps. Switches follow at once. Ideas from FMini-1's
 * MIDI LEARN and OG Synth's knob pickup. Zero is empty: a zeroed map has nothing learned (CC 0 is
 * bank select, never learned), and a destination is stored as control + 1, 0 meaning OFF. */
enum { TG_LEARN_SLOTS = 16 };
typedef struct { uint8_t cc[TG_LEARN_SLOTS], to[TG_LEARN_SLOTS]; } tg_cc_map;
typedef struct { uint8_t last[TG_LEARN_SLOTS], applied[TG_LEARN_SLOTS]; uint16_t caught; } tg_cc_pickup;

/* Bank select, mod, data entry, sustain, RPN/NRPN and channel mode keep their usual jobs. */
static inline unsigned tg_cc_learnable(unsigned cc)
{
    return cc < 120u && cc != 0u && cc != 1u && cc != 6u && cc != 32u && cc != 38u && cc != 64u &&
           (cc < 96u || cc > 101u);
}

static inline int tg_cc_find(const tg_cc_map *m, unsigned cc)
{
    if (!cc) return -1;
    for (unsigned i = 0; i < TG_LEARN_SLOTS; i++) if (m->cc[i] == cc) return (int)i;
    return -1;
}

/* The CC's slot, made with destination `to` (control + 1, 0 OFF) when new; -1 when all 16 are taken
 * by CCs that still go somewhere. */
static inline int tg_cc_learn(tg_cc_map *m, unsigned cc, unsigned to)
{
    if (!tg_cc_learnable(cc)) return -1;
    int slot = tg_cc_find(m, cc);
    if (slot >= 0) return slot;
    for (unsigned pass = 0; pass < 2; pass++)
        for (unsigned i = 0; i < TG_LEARN_SLOTS; i++)
            if (pass ? !m->to[i] : !m->cc[i]) {
                m->cc[i] = (uint8_t)cc;
                m->to[i] = (uint8_t)to;
                return (int)i;
            }
    return -1;
}

static inline void tg_cc_pickup_reset(tg_cc_pickup *p)
{
    memset(p->last, 255, sizeof p->last);
    memset(p->applied, 255, sizeof p->applied);
    p->caught = 0;
}

/* value and target on the same 0-127 scale. Returns 1 when the control should take value. */
static inline unsigned tg_cc_pickup_take(tg_cc_pickup *p, unsigned slot, unsigned value, unsigned target, unsigned jump)
{
    if (slot >= TG_LEARN_SLOTS || value > 127u) return 0;
    uint16_t bit = (uint16_t)(1u << slot);
    unsigned last = p->last[slot];
    p->last[slot] = (uint8_t)value;
    /* Something else (a knob, a preset) moved the control: catch it again. */
    if ((p->caught & bit) && p->applied[slot] != target) p->caught &= (uint16_t)~bit;
    if (!(p->caught & bit)) {
        unsigned crossed = last <= 127u && ((last <= target && value >= target) || (last >= target && value <= target));
        if (!jump && value != target && !crossed) return 0;
        p->caught |= bit;
    }
    p->applied[slot] = (uint8_t)value;
    return value != target;
}
#endif
