/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TG_MIDITRS101_H
#define TG_MIDITRS101_H
#include <stdint.h>

/* MIDI OUT on the TRS jack: USB-MIDI packets turned into serial bytes and kept as whole messages in a ring
 * (a full ring drops the message, never half of it), drained one byte at a time from the timer tick. After
 * FMini-1's smk_midi_out.c (GPL-3.0). Hardware-free here, so the host tests run it. */
enum { TG_TRS_RING = 256, TG_TRS_GIVE_UP_TICKS = 12, TG_TRS_QUIET_AFTER = 4 };
enum { TG_MIDI_ROUTE_USB = 1, TG_MIDI_ROUTE_TRS = 2 };

typedef struct {
    uint8_t ring[TG_TRS_RING];
    volatile uint16_t w, r;
    uint8_t waiting, wait_ticks, failures, quiet;
} tg_trs_out;

/* Bytes of one USB-MIDI packet (cable 0) by its CIN; 0 for SysEx or a malformed packet. */
unsigned tg_trs_packet_bytes(uint32_t packet, uint8_t out[3]);
/* Queue a packet's bytes; 0 if dropped (full or not a channel / realtime / common message). */
unsigned tg_trs_queue(tg_trs_out *t, uint32_t packet);
/* One tick: done = the UART has sent the last byte. Returns the next byte to send, or -1. */
int tg_trs_tick(tg_trs_out *t, unsigned done);
#endif
