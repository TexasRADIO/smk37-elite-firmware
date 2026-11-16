/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TG_CLOCK101_H
#define TG_CLOCK101_H
#include <stdint.h>

enum { TG_CLOCK_INTERNAL, TG_CLOCK_USB };
enum { TG_CLOCK_STOPPED, TG_CLOCK_WAITING, TG_CLOCK_RUNNING, TG_CLOCK_LOST };
enum { TG_CLOCK_TIMEOUT_US = 500000, TG_CLOCKS_PER_STEP = 6,
       TG_CLOCKS_PER_BAR = 16 * TG_CLOCKS_PER_STEP,
       TG_STOPPED_PULSE_WRAP = UINT16_MAX / TG_CLOCKS_PER_BAR * TG_CLOCKS_PER_BAR };

/* Foreground-owned transport. The audio side consumes complete snapshots.
 * steps is a monotonically wrapping event generation, not a pattern index.
 * On an epoch change, reset to seek then consume (steps - base) events.
 * Otherwise consume the unsigned difference from the previous steps value.
 * Song position advances only on running pulses. Stopped pulses give live
 * percussion its own grid without moving the saved song position. */
typedef struct {
    uint32_t epoch, base, steps, period_us;
    uint16_t seek;                 /* Song Position Pointer in six-clock beats. */
    uint8_t source, state, phase;  /* phase is the next pulse's index, 0..5. */
    uint8_t gate_breaks;          /* Stop or loss, even when Continue follows in the same batch. */
    uint16_t stopped_pulses;      /* Free-running audition grid, wrapping on a bar. */
    uint32_t losses;              /* Clock detach survives a coalesced Continue. */
} tg_clock_snapshot;
static inline unsigned tg_clock_active(const tg_clock_snapshot *c)
{
    return c->source == TG_CLOCK_USB && (c->state == TG_CLOCK_WAITING || c->state == TG_CLOCK_RUNNING);
}

typedef struct {
    tg_clock_snapshot out;
    uint32_t last_tick_us, wait_us, window_us, window_ticks;
    uint32_t received, clocks, ignored, coalesced;
    uint32_t tempo_candidate_us;
    uint8_t seen_tick, tempo_candidate_ticks;
} tg_clock;

/* Zero initialization selects internal clock. Switching source stops transport
 * and resets position; selecting the already-selected source is a no-op. */
void tg_clock_select(tg_clock *, unsigned source, uint32_t now_us);
/* Shared entry for a future local transport button and received FA/FB/FC. */
void tg_clock_transport(tg_clock *, unsigned status, uint32_t now_us);
/* Parsed USB MIDI cable-0 F8/FA/FB/FC (CIN F), or SPP (CIN 3).
 * Return 1 if recognized, including when deliberately ignored in internal mode.
 * Return 0 for unrelated/malformed packets, with no state mutation. */
int tg_clock_packet(tg_clock *, uint32_t packet, uint32_t now_us);
void tg_clock_poll(tg_clock *, uint32_t now_us);
/* Reset/suspend/overflow/update detach: never resume from clocks alone. */
void tg_clock_disconnect(tg_clock *);
#endif
