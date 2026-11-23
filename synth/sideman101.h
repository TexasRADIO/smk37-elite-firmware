/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef SIDEMAN101_H
#define SIDEMAN101_H
#include <stdint.h>

 



enum { TG_SIDEMAN_BYTES = 19, TG_SIDEMAN_PACKETS = 7, TG_SIDEMAN_FW_BB37 = 3, TG_SIDEMAN_NONE = 127 };

typedef struct {
    uint8_t page, bank, mode, knob;   /* knob: 0-7, TG_SIDEMAN_NONE when none was touched */
    uint16_t bpm;                     /* 0-16383 */
    uint8_t instrument, running, section, help, fx_tab;
} tg_sideman_state;

/* tg_sideman_encode: the message, every data byte clamped to 0-127.
 * tg_sideman_packets: USB-MIDI packets on cable 0, as midi_out_event takes them (CIN in the low nibble). */

static inline uint8_t tg_sideman_seven(unsigned v) { return (uint8_t)(v > 127u ? 127u : v); }

static inline void tg_sideman_encode(const tg_sideman_state *s, uint8_t fw, uint8_t out[TG_SIDEMAN_BYTES])
{
    unsigned bpm = s->bpm > 16383u ? 16383u : s->bpm;
    const uint8_t head[6] = {0xf0, 0x00, 0x22, 0x24, 0x35, 0x01};
    for (unsigned i = 0; i < 6; i++) out[i] = head[i];
    out[6] = tg_sideman_seven(fw);
    out[7] = tg_sideman_seven(s->page);
    out[8] = tg_sideman_seven(s->bank);
    out[9] = tg_sideman_seven(s->mode);
    out[10] = tg_sideman_seven(s->knob);
    out[11] = (uint8_t)(bpm >> 7);
    out[12] = (uint8_t)(bpm & 0x7fu);
    out[13] = tg_sideman_seven(s->instrument);
    out[14] = tg_sideman_seven(s->running);
    out[15] = tg_sideman_seven(s->section);
    out[16] = tg_sideman_seven(s->help);
    out[17] = tg_sideman_seven(s->fx_tab);
    out[18] = 0xf7;
}

static inline unsigned tg_sideman_packets(const uint8_t *b, unsigned n, uint32_t *out, unsigned max)
{
    unsigned count = 0;
    for (unsigned i = 0; i < n && count < max; ) {
        unsigned left = n - i, take = left > 3u ? 3u : left;
        /* CIN 4: SysEx starts or continues with three bytes; 5, 6, 7: ends with one, two, three. */
        unsigned ends = take < 3u || i + 3u == n;
        unsigned cin = ends ? 4u + take : 4u;
        uint32_t p = cin;
        for (unsigned k = 0; k < take; k++) p |= (uint32_t)b[i + k] << (8u * (k + 1u));
        out[count++] = p;
        i += take;
    }
    return count;
}
#endif
