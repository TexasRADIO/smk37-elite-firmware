/* SPDX-License-Identifier: GPL-3.0-only */
#include "poly101.h"

enum { STEAL_FADE_FRAMES = 32 };

static tg_poly_slot *release_slot(tg_poly *poly)
{
    tg_poly_slot *quietest = 0;
    float level = 0;
    for (unsigned i = 0; i < TG_POLY_VOICES; i++) {
        tg_poly_slot *slot = &poly->slots[i];
        if (!slot->note) return slot;
        if (slot->held) continue;
        float candidate = slot->voice.envelope + slot->voice.gate_level;
        if (!quietest || candidate < level) {
            quietest = slot;
            level = candidate;
        }
    }
    return quietest;
}

void tg_poly_apply(tg_poly *poly, const tg_controls *performance,
                   const uint8_t *notes, unsigned count)
{
    uint8_t selected[TG_POLY_VOICES] = {0};
    unsigned selected_count = 0;
    while (count && selected_count < TG_POLY_VOICES) {
        uint8_t note = notes[--count];
        if (!note || note > TG_NOTE_ZERO) continue;
        unsigned i = 0;
        while (i < selected_count && selected[i] != note) i++;
        if (i == selected_count) selected[selected_count++] = note;
    }

    tg_controls controls = *performance;
    controls.clock.source = TG_CLOCK_INTERNAL;
    controls.running = controls.arp = controls.arp_count = 0;
    unsigned silence = controls.silence != poly->silence;
    for (unsigned i = 0; i < TG_POLY_VOICES; i++) {
        tg_poly_slot *slot = &poly->slots[i];
        slot->held = 0;
        for (unsigned n = 0; slot->note && n < selected_count; n++) {
            if (slot->note != selected[n]) continue;
            slot->held = 1;
            selected[n] = 0;
            break;
        }
        if (silence) slot->last = slot->stolen_sample = slot->fade_left = 0;
    }
    for (unsigned n = 0; n < selected_count; n++) {
        if (!selected[n]) continue;
        tg_poly_slot *slot = release_slot(poly);
        slot->stolen_sample = slot->last;
        slot->fade_left = slot->note && slot->last ? STEAL_FADE_FRAMES : 0;
        slot->note = selected[n];
        slot->held = 1;
        controls.live_note = controls.live_trigger = 0;
        controls.silence = poly->silence;
        tg_init(&slot->voice, &controls);
    }
    controls.silence = poly->silence = performance->silence;
    for (unsigned i = 0; i < TG_POLY_VOICES; i++) {
        tg_poly_slot *slot = &poly->slots[i];
        if (!slot->note) continue;
        controls.live_note = slot->held ? slot->note : 0;
        controls.live_trigger = slot->voice.controls.live_trigger +
                                (controls.live_note && !slot->voice.controls.live_note);
        tg_apply(&slot->voice, &controls);
    }
}

int tg_poly_render(tg_poly *poly, int32_t *output, int32_t *scratch, unsigned frames)
{
    for (unsigned f = 0; f < frames; f++) output[2 * f] = 0;
    for (unsigned i = 0; i < TG_POLY_VOICES; i++) {
        tg_poly_slot *slot = &poly->slots[i];
        if (!slot->note) continue;
        if (tg_render(&slot->voice, scratch, frames)) return -1;
        for (unsigned f = 0; f < frames; f++) {
            int32_t sample = scratch[2 * f];
            if (slot->fade_left) {
                sample = (slot->stolen_sample * slot->fade_left +
                          sample * (STEAL_FADE_FRAMES - slot->fade_left)) / STEAL_FADE_FRAMES;
                slot->fade_left--;
            }
            slot->last = sample;
            output[2 * f] += sample;
        }
        if (!slot->held && !slot->fade_left && slot->voice.envelope == 0 &&
            slot->voice.gate_level < 0.00000001f &&
            slot->voice.output_dc > -0.00000001f && slot->voice.output_dc < 0.00000001f)
            slot->note = 0;
    }
    for (unsigned f = 0; f < frames; f++) {
        /* Fixed headroom keeps the level stable as fingers are lifted. */
        output[2 * f] /= TG_POLY_VOICES;
        output[2 * f + 1] = output[2 * f];
    }
    return 0;
}
