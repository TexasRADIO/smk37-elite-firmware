/* SPDX-License-Identifier: GPL-3.0-only */
#include "clock101.h"

static void locate(tg_clock *c, unsigned position)
{
    c->out.epoch++;
    c->out.base = c->out.steps;
    c->out.seek = (uint16_t)position;
    c->out.phase = 0;
    c->out.stopped_pulses = 0;
}
static void forget_tempo(tg_clock *c)
{
    c->seen_tick = 0;
    c->out.period_us = 0;
    c->window_ticks = 0;
    c->tempo_candidate_ticks = 0;
}
void tg_clock_select(tg_clock *c, unsigned source, uint32_t now_us)
{
    if (source > TG_CLOCK_USB || source == c->out.source) return;
    c->out.source = (uint8_t)source;
    c->out.state = TG_CLOCK_STOPPED;
    c->wait_us = now_us;
    locate(c, 0);
    forget_tempo(c);
}
void tg_clock_transport(tg_clock *c, unsigned status, uint32_t now_us)
{
    if (c->out.source != TG_CLOCK_USB) return;
    if (status == 0xfa) {
        locate(c, 0);
        c->out.state = TG_CLOCK_WAITING;
        c->wait_us = now_us;
    } else if (status == 0xfb) {
        /* Continue can arrive before foreground polling sees a silent gap. */
        tg_clock_poll(c, now_us);
        if (c->out.state != TG_CLOCK_RUNNING && c->out.state != TG_CLOCK_WAITING) {
            c->out.state = TG_CLOCK_WAITING;
            c->wait_us = now_us;
        }
    } else if (status == 0xfc) {
        if (tg_clock_active(&c->out)) c->out.gate_breaks++;
        if (c->out.state != TG_CLOCK_STOPPED) c->out.stopped_pulses = 0;
        c->out.state = TG_CLOCK_STOPPED;
    }
}
void tg_clock_disconnect(tg_clock *c)
{
    if (c->out.source != TG_CLOCK_USB) return;
    if (c->out.state == TG_CLOCK_RUNNING || c->out.state == TG_CLOCK_WAITING) {
        c->out.state = TG_CLOCK_LOST;
        c->out.gate_breaks++;
        c->out.losses++;
    }
    forget_tempo(c);
}
void tg_clock_poll(tg_clock *c, uint32_t now_us)
{
    if (c->out.source != TG_CLOCK_USB) return;
    uint32_t age = now_us - (c->out.state == TG_CLOCK_WAITING ? c->wait_us : c->last_tick_us);
    if ((c->out.state == TG_CLOCK_WAITING || c->out.state == TG_CLOCK_RUNNING) &&
        age >= TG_CLOCK_TIMEOUT_US) tg_clock_disconnect(c);
}
static int tempo_changed(tg_clock *c, uint32_t interval)
{
    uint32_t period = c->out.period_us;
    if (!period || interval < 2000 || interval > 250000 ||
        (interval >= period - period / 4u && interval <= period + period / 4u)) {
        c->tempo_candidate_ticks = 0;
        return 0;
    }

    /* Keep the averaging window for normal jitter. Three consistent
     * intervals outside it can indicate a deliberate tempo jump. */
    uint32_t candidate = c->tempo_candidate_us;
    uint32_t tolerance = candidate / 8u;
    if (!c->tempo_candidate_ticks || interval < candidate - tolerance ||
        interval > candidate + tolerance) {
        c->tempo_candidate_us = interval;
        c->tempo_candidate_ticks = 1;
        return 0;
    }

    c->tempo_candidate_us = (candidate + interval) / 2u;
    if (++c->tempo_candidate_ticks < 3) return 0;
    c->out.period_us = c->tempo_candidate_us;
    c->tempo_candidate_ticks = 0;
    return 1;
}
static void tick(tg_clock *c, uint32_t now_us)
{
    /* Check loss before a late pulse can erase evidence of the missing clock. */
    tg_clock_poll(c, now_us);
    if (c->seen_tick) {
        uint32_t interval = now_us - c->last_tick_us;
        if (!interval) c->coalesced++;
        if (interval >= TG_CLOCK_TIMEOUT_US) forget_tempo(c);
        else if (tempo_changed(c, interval)) {
            c->window_us = now_us;
            c->window_ticks = 0;
        } else {
            /* One interval acquires an initial estimate. A 24-interval window
             * then averages host/poll jitter. Neither estimate advances music:
             * only counted incoming pulses can produce step events. */
            if (!c->out.period_us && interval >= 2000 && interval <= 250000)
                c->out.period_us = interval;
            c->window_ticks++;
            uint32_t elapsed = now_us - c->window_us;
            if (c->window_ticks >= 24 && elapsed >= 10000) {
                uint32_t period = elapsed / c->window_ticks;
                if (period >= 2000 && period <= 250000) c->out.period_us = period;
                c->window_us = now_us;
                c->window_ticks = 0;
            }
        }
    }
    if (!c->seen_tick) { c->window_us = now_us; c->window_ticks = 0; }
    c->seen_tick = 1;
    c->last_tick_us = now_us;
    c->clocks++;
    if (c->out.state == TG_CLOCK_WAITING) c->out.state = TG_CLOCK_RUNNING;
    if (c->out.state == TG_CLOCK_STOPPED && ++c->out.stopped_pulses == TG_STOPPED_PULSE_WRAP)
        c->out.stopped_pulses = 0;
    if (c->out.state != TG_CLOCK_RUNNING) return;
    if (!c->out.phase) c->out.steps++;
    c->out.phase = (uint8_t)((c->out.phase + 1u) % TG_CLOCKS_PER_STEP);
}
int tg_clock_packet(tg_clock *c, uint32_t packet, uint32_t now_us)
{
    unsigned header = packet & 255u, status = (packet >> 8) & 255u;
    unsigned lo = (packet >> 16) & 255u, hi = packet >> 24;
    unsigned realtime = header == 15 && !lo && !hi &&
                        (status == 0xf8 || status == 0xfa || status == 0xfb || status == 0xfc);
    unsigned spp = header == 3 && status == 0xf2 && lo < 128 && hi < 128;
    if (!realtime && !spp) return 0;
    c->received++;
    if (c->out.source != TG_CLOCK_USB) { c->ignored++; return 1; }
    if (spp) locate(c, lo | (hi << 7));
    else if (status == 0xf8) tick(c, now_us);
    else tg_clock_transport(c, status, now_us);
    return 1;
}
