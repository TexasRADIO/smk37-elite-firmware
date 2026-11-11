/* SPDX-License-Identifier: GPL-3.0-only */
/* USB and this consumer run only in foreground; the audio ISR sees snapshots. */
#include "synth/midi101.h"
static tg_midi performance_midi;
static tg_clock performance_clock;
static uint32_t midi_clock_at_silence;
static uint8_t midi_clock_phase_at_silence;
static uint32_t midi_reset_seen, midi_stream_resets;
static uint8_t midi_owned, midi_frame_trigger, midi_gate_broke;
static uint32_t midi_silence_seen;
static uint32_t midi_strikes[4];
/* At most one full phrase can be entered before the cursor disarms. */
static uint8_t midi_step_notes[TG_STEPS], midi_step_count, midi_step_armed, midi_step_key;

static void midi_drain(void)
{
    if (updating || !usb.config || usb.suspended || usb.resets != midi_reset_seen || midi_in_overflow) {
        if (performance_midi.count || midi_in_overflow || usb.resets != midi_reset_seen) midi_stream_resets++;
        tg_midi_release(&performance_midi);
        tg_clock_disconnect(&performance_clock);
        midi_reset_seen = usb.resets;
        mi_r = mi_w;
        midi_in_overflow = 0;
        midi_step_count = midi_step_key = midi_gate_broke = 0;
        memset(midi_strikes, 0, sizeof midi_strikes);
        return;
    }
    /* A bounded batch leaves all endpoint/SysEx ownership with usb_poll. */
    unsigned budget = MQ;
    while (mi_r != mi_w && budget--) {
        unsigned index = mi_r % MQ;
        if (midi_in_source[index] == 1 &&
            !tg_clock_packet(&performance_clock, midi_in_q[index], midi_in_ms[index] * 1000u)) {
            uint32_t before = performance_midi.silence;
            uint8_t trigger = performance_midi.trigger, held = performance_midi.count;
            unsigned overlap = midi_step_key &&
                (performance_midi.down[midi_step_key / 8u] & (1u << (midi_step_key % 8u)));
            tg_midi_packet(&performance_midi, midi_in_q[index]);
            if (held && !performance_midi.count) midi_gate_broke = 1;
            /* Only accepted Note On changes trigger. Keep each pitch across the batch. */
            if (performance_midi.trigger != trigger) {
                unsigned pitch = performance_midi.selected;
                midi_strikes[pitch / 32u] |= UINT32_C(1) << (pitch % 32u);
                if (midi_step_armed && pitch && midi_step_count < sizeof midi_step_notes) {
                    /* MIDI zero is playable, but the phrase uses zero as a rest. */
                    midi_step_notes[midi_step_count++] = (uint8_t)(pitch | (overlap ? TG_TIE : 0));
                    midi_step_key = (uint8_t)pitch;
                }
            }
            if (before != performance_midi.silence) {
                memset(midi_strikes, 0, sizeof midi_strikes);
                midi_clock_at_silence = performance_clock.out.steps;
                midi_clock_phase_at_silence = performance_clock.out.phase;
            }
        }
        mi_r++;
    }
    tg_clock_poll(&performance_clock, fm1_ms * 1000u);
}
static void read_performance(struct panel *panel, unsigned head)
{
    tg_controls *c = &panel->sound;
    unsigned previous_note = c->live_note;
    unsigned previous_clock_state = c->clock.state;
#ifdef ELITE_DRUMS808
    unsigned previous_dillo_strike = panel->dillo_live.strike;
    unsigned previous_dillo_gap = panel->dillo_live.gap;
    /* Decide between live cues and stopped recalls using MIDI already drained
     * for this scan. Publish the full clock snapshot after local controls. */
    if (c->clock.source == TG_CLOCK_USB && performance_clock.out.source == TG_CLOCK_USB) {
        c->clock.state = performance_clock.out.state;
        c->running = (uint8_t)tg_clock_active(&c->clock);
    }
    if (performance_midi.silence != midi_silence_seen) {
        /* Keep All Sound Off with the physical keys through focus, octave
         * and scale changes. The panel clears this mask on each key-up.
         * Keys first seen in this scan can still start a new note. */
        transpose_keys |= transpose_down;
        held_chord = (tg_latch){0};
        arp_chord = (tg_latch){0};
        panel->dillo_entry_key = 0;
    }
#endif
    read_controls(panel, head);
    if (panel->midi_step_entry & MIDI_STEP_ENTRY) {
        unsigned tie = panel->midi_step_entry & MIDI_STEP_TIE ? TG_TIE : 0;
        for (unsigned i = 0; i < midi_step_count && panel->record; i++) {
            unsigned entry = midi_step_notes[i], pitch = entry & 127u;
            /* A panel rest, cursor jump or local note may have broken the
             * chain after this burst was queued. Later MIDI steps can tie. */
            unsigned overlap = panel->record_key > TG_KEYS ? entry & TG_TIE : 0;
            enter_step(panel, pitch, tie | overlap);
            if (panel->record) panel->record_key = (uint8_t)(TG_KEYS + pitch);
        }
    }
    midi_step_count = 0;
    midi_step_armed = panel->page == PAGE_STEPS && panel->record;
    midi_step_key = midi_step_armed && panel->record_key > TG_KEYS ?
        (uint8_t)(panel->record_key - TG_KEYS) : 0;
    tg_clock_select(&performance_clock, c->clock.source, fm1_ms * 1000u);
    if (panel->clock_toggle) {
        unsigned active = performance_clock.out.state == TG_CLOCK_WAITING || performance_clock.out.state == TG_CLOCK_RUNNING;
        tg_clock_transport(&performance_clock, active ? 0xfc : 0xfa, fm1_ms * 1000u);
        panel->clock_toggle = 0;
    }
    if (c->clock.epoch != performance_clock.out.epoch || c->clock.steps != performance_clock.out.steps ||
        c->clock.phase != performance_clock.out.phase || previous_clock_state != performance_clock.out.state)
        c->clock_age_us = performance_clock.seen_tick ? fm1_ms * 1000u - performance_clock.last_tick_us : 0;
#ifdef ELITE_DRUMS808
    if (c->clock.epoch != performance_clock.out.epoch || c->clock.gate_breaks != performance_clock.out.gate_breaks)
        clear_drum_rolls(panel, panel->drum_latch);
#endif
    c->clock = performance_clock.out;
    c->silence_clock_steps = midi_clock_at_silence;
    c->silence_clock_phase = midi_clock_phase_at_silence;
    if (c->clock.source == TG_CLOCK_USB) {
        c->running = (uint8_t)tg_clock_active(&c->clock);
#ifndef ELITE_DRUMS808
        if (c->arp) c->running = 0;
#endif
    }
    tg_midi_set_channel(&performance_midi, panel->midi_channel);
    tg_midi_set_arp_latch(&performance_midi,
                         panel->arp_latch && c->arp && arp_latch_allowed(c) && !panel->record);
    unsigned note = tg_midi_note(&performance_midi, c->patch.trigger);
    unsigned own = !c->live_note && !c->arp_count && note;
    unsigned fresh_local = c->live_note && c->live_trigger != midi_frame_trigger;
    /* Preserve an ordered all-up edge across foreground and ISR coalescing.
     * MIDI releases hidden behind a local note do not break the live gate. */
    unsigned midi_break = midi_owned && midi_gate_broke && !c->live_note && !c->arp_count;
    midi_gate_broke = 0;
#ifdef ELITE_DRUMS808
    if (panel->dillo_audition && (conductor_running(panel) || performance_midi.silence != midi_silence_seen)) {
        panel->dillo_audition = 0;
        panel->dillo_live.note = panel->dillo_live.velocity = 0;
        panel->dillo_live.gap++;
    }
#endif
    if (performance_midi.silence != midi_silence_seen) {
        panel->audition_step = 0;
#ifdef ELITE_DRUMS808
        clear_drum_rolls(panel, panel->drum_latch);
        panel->dillo_live.keep_after_panic = 0;
#endif
        c->silence++;
        c->silence_keep_live = 0;
        midi_silence_seen = performance_midi.silence;
    }
    /* A new note may arrive in a later foreground pass before the ISR copies
     * this generation. Preserve its ordering across snapshot coalescing. */
    if (fresh_local || (own && performance_midi.after_silence)) c->silence_keep_live = 1;
#ifdef ELITE_DRUMS808
    if (!panel->dillo_live.note) panel->dillo_live.keep_after_panic = 0;
    else if (panel->dillo_live.strike != previous_dillo_strike ||
             (panel->dillo_entry_key && panel->dillo_live.gap != previous_dillo_gap))
        panel->dillo_live.keep_after_panic = 1;
#endif
    if (own) {
        c->live_note = (uint8_t)note;
        c->live_velocity = (uint8_t)tg_midi_velocity(&performance_midi, note);
        if (c->arp) {
            c->arp_count = (uint8_t)tg_midi_arp_notes(&performance_midi, c->arp_notes, c->arp == TG_ARP_ORDER);
            for (unsigned i = 0; i < c->arp_count; i++)
                c->arp_velocity[i] = (uint8_t)tg_midi_velocity(&performance_midi, c->arp_notes[i]);
        }
        unsigned pitch = note == TG_NOTE_ZERO ? 0 : note;
        if (midi_strikes[pitch / 32u] & (UINT32_C(1) << (pitch % 32u))) c->live_trigger++;
    }
    /* Audition is the lowest-priority local voice and sends no MIDI. */
    if (panel->audition_step) {
        unsigned step = panel->audition_step - 1u;
        unsigned pitch = c->steps[step] & 127u;
        if (panel->page != PAGE_STEPS || panel->record || c->running || tg_clock_active(&c->clock) ||
            c->live_note || c->arp_count || panel->cursor != step || step >= tg_pattern_length(c) || !pitch) {
            panel->audition_step = 0;
        } else {
            c->live_note = (uint8_t)bounded((int)pitch + c->transpose, 1, 127);
            c->live_velocity = panel->audition_velocity;
            if (panel->audition_strike) c->live_trigger++;
        }
    }
    panel->audition_strike = 0;
    /* Keep a released foreground frame even if audio sees only the next note. */
    if (midi_break || (previous_note && !c->live_note))
        c->live_break++;
    memset(midi_strikes, 0, sizeof midi_strikes);
    midi_owned = (uint8_t)!!own;
    midi_frame_trigger = c->live_trigger;
    /* Both controllers remain useful: bend sums with saturation, mod takes max. */
    int bend = c->bend + performance_midi.bend;
    c->bend = (int16_t)(bend < -512 ? -512 : bend > 511 ? 511 : bend);
    if (performance_midi.mod > c->mod) c->mod = performance_midi.mod;
}
