/* SPDX-License-Identifier: GPL-3.0-only */
#include "synth/miditrs101.h"

unsigned tg_trs_packet_bytes(uint32_t packet, uint8_t out[3])
{
    unsigned cin = packet & 15u, status = packet >> 8 & 255u;
    out[0] = (uint8_t)status;
    out[1] = (uint8_t)(packet >> 16 & 127u);
    out[2] = (uint8_t)(packet >> 24 & 127u);
    if (packet >> 4 & 15u) return 0;                         /* cable 0 only */
    if (cin >= 8u && cin <= 14u) {
        if ((status >> 4) != cin || status < 0x80u) return 0;
        return cin == 12u || cin == 13u ? 2u : 3u;
    }
    if (cin == 15u) return status >= 0xf8u ? 1u : 0u;         /* single realtime byte */
    if (cin == 2u) return status == 0xf1u || status == 0xf3u ? 2u : 0u;
    if (cin == 3u) return status == 0xf2u ? 3u : 0u;
    return 0;
}

unsigned tg_trs_queue(tg_trs_out *t, uint32_t packet)
{
    uint8_t b[3];
    unsigned n = tg_trs_packet_bytes(packet, b);
    if (!n || t->quiet) return 0;
    uint16_t used = (uint16_t)(t->w - t->r);
    if (used + n > TG_TRS_RING) return 0;
    for (unsigned i = 0; i < n; i++) t->ring[(uint16_t)(t->w + i) % TG_TRS_RING] = b[i];
    t->w = (uint16_t)(t->w + n);
    return 1;
}

int tg_trs_tick(tg_trs_out *t, unsigned done)
{
    if (t->waiting) {
        if (!done) {
            if (++t->wait_ticks < TG_TRS_GIVE_UP_TICKS) return -1;
            if (++t->failures >= TG_TRS_QUIET_AFTER) { t->quiet = 1; t->r = t->w; }
        } else t->failures = 0;
        t->waiting = 0;
    }
    if (t->r == t->w || t->quiet) return -1;
    int byte = t->ring[t->r % TG_TRS_RING];
    t->r = (uint16_t)(t->r + 1u);
    t->waiting = 1;
    t->wait_ticks = 0;
    return byte;
}
