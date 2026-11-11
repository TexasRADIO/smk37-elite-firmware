/* SPDX-License-Identifier: GPL-3.0-only */
#include "layer303.h"
#include "fastmath.h"
#include <string.h>
#include "../conductor/dry_bus.h"

/* Original Elite recipes. Keep the order stable for setup and startup recall.
 * Pads 1–4 are bass, 5–8 are acid, and 9–12 are leads. */
const dillo_named_sound dillo_expansion[DILLO_EXPANSION_SOUNDS] = {
    {"Round Square", {{380, 430, 0, 28, 46, 30,  0, 68,  4, 14}, {26, 32, DILLO_SWEEP_STOCK}}},
    {"Low Current",  {{270, 620, 1, 33, 56, 37,  8, 48,  7, 20}, {31, 44, DILLO_SWEEP_STOCK}}},
    {"Dry Bottom",   {{520, 230, 0, 26, 14, 32, 13, 43,  2,  8}, {10, 12, DILLO_SWEEP_STOCK}}},
    {"Pulse Bass",   {{660, 650, 0, 43, 22, 52,  9, 58,  3, 33}, {17, 22, DILLO_SWEEP_STOCK}}},
    {"Short Acid",   {{760, 1400,1, 77, 12, 82,  0, 50,  3,  0}, {12, 16, DILLO_SWEEP_STOCK}}},
    {"Low Sweep",    {{310, 1720,0, 88, 49, 80, 10, 83,  4, 18}, {31, 35, DILLO_SWEEP_STOCK}}},
    {"Accent Arc",   {{630, 1830,1, 69, 37, 95,  4, 64,  6, 12}, {42, 26, DILLO_SWEEP_STOCK}}},
    {"Rough Acid",   {{950, 1100,1, 72, 26, 68, 45, 56,  3, 20}, {20, 22, DILLO_SWEEP_STOCK}}},
    {"Open Lead",    {{2150,850, 1, 42, 64, 45,  4,105, 10, 63}, {39, 66, DILLO_SWEEP_STOCK}}},
    {"Square Ribbon",{{1090,1000,0, 34, 70, 41,  0,149, 15, 53}, {47, 68, DILLO_SWEEP_STOCK}}},
    {"Glass Glide",  {{1530,1450,1, 57, 64, 48,  5,179,  9, 47}, {36, 53, DILLO_SWEEP_STOCK}}},
    {"Soft Siren",   {{790, 1110,0, 49, 92, 57, 12,123, 24, 74}, {62, 80, DILLO_SWEEP_STOCK}}},
};

/* Classic 303 characters: original Big Beat settings aimed at the well-known uses of the
 * box, not copies of anyone's knob positions. {cutoff Hz, resonance x100, wave (0 square,
 * 1 saw), env mod, decay, accent, drive, slide ms, attack ms, tracking}, then accent and amp
 * decay (0 keeps the voice default) and the accent sweep. */
const dillo_named_sound dillo_classic[DILLO_CLASSIC_SOUNDS] = {
    {"Scream",       {{ 900, 1850, 1, 88, 55, 92, 18, 65, 3,  0}, { 0,  0, DILLO_SWEEP_LONG}}},
    {"Dub Thump",    {{ 180,  380, 0, 18, 72, 38,  0, 70, 4, 10}, {30, 45, DILLO_SWEEP_STOCK}}},
    {"Squelch",      {{ 340, 1680, 1, 92, 42,100, 10, 65, 3,  0}, {40,  0, DILLO_SWEEP_LONG}}},
    {"Acid Grind",   {{ 640, 1420, 1, 76, 36, 86, 85, 60, 3,  0}, { 0,  0, DILLO_SWEEP_LAG}}},
    {"Wet Rubber",   {{ 300, 1520, 0, 82, 50, 80, 14, 75, 3,  0}, { 0,  0, DILLO_SWEEP_LAG}}},
    {"Chirp",        {{1250, 1300, 1, 62, 10, 70,  0, 55, 2,  0}, {12, 14, DILLO_SWEEP_STOCK}}},
    {"Low Growl",    {{ 220, 1120, 1, 56, 46, 62, 60, 70, 3,  8}, { 0,  0, DILLO_SWEEP_STOCK}}},
    {"Long Wah",     {{ 420, 1720, 1, 96, 95, 76,  6, 80, 3,  0}, {60,  0, DILLO_SWEEP_LONG}}},
    {"Tight Pluck",  {{ 820,  620, 0, 46,  8, 60,  0, 50, 2,  0}, {10, 12, DILLO_SWEEP_STOCK}}},
    {"Machine Pulse",{{ 560,  920, 0, 52, 26, 66, 10, 60, 3,  0}, { 0,  0, DILLO_SWEEP_STOCK}}},
    {"High Scream",  {{1500, 1950, 1, 72, 30, 95, 40, 60, 3,  0}, { 0,  0, DILLO_SWEEP_LONG}}},
    {"Deep Square",  {{ 140,  300, 0, 16, 62, 30,  0, 70, 4, 12}, { 0, 50, DILLO_SWEEP_STOCK}}},
};

/* Devilfish-style extremes: the voice pushed to the ends of its range the way the
 * Devilfish modification pushed the 303. Full drive, resonance at the limit, keyboard
 * tracking on the filter, long slides, soft or snapped attacks and long amp decay (the
 * Devilfish "VEG") so notes can sustain. Original Big Beat settings. */
const dillo_named_sound dillo_devilfish[DILLO_DEVIL_SOUNDS] = {
    {"Fish Scream",  {{1200, 2000, 1,100, 70,100,100,120, 1, 70}, { 80,   0, DILLO_SWEEP_LONG}}},
    {"Overdrive Sub",{{ 160,  900, 0, 40, 80, 70,100, 90, 3,  0}, {  0,  90, DILLO_SWEEP_STOCK}}},
    {"Tracking Lead",{{2000, 1600, 1, 60, 50, 80, 50,150, 2,100}, {  0,  70, DILLO_SWEEP_LAG}}},
    {"Muffled Grit", {{ 400, 1400, 0, 80, 30, 90,100, 60, 6, 20}, {  0,   0, DILLO_SWEEP_STOCK}}},
    {"Long Glide",   {{ 600, 1700, 1, 85, 60, 85, 40,250, 3, 40}, { 50,   0, DILLO_SWEEP_LONG}}},
    {"Snap Attack",  {{ 900, 1500, 1, 90,  5,100, 70, 50, 1, 30}, {  6,   8, DILLO_SWEEP_STOCK}}},
    {"Soft Bloom",   {{ 300, 1300, 0, 70, 90, 60, 20,100,30, 20}, {  0, 100, DILLO_SWEEP_LONG}}},
    {"Acid Fuzz",    {{ 500, 1900, 1, 95, 45,100,100, 80, 2, 50}, { 70,   0, DILLO_SWEEP_LONG}}},
    {"Rubber Howl",  {{ 250, 2000, 0,100, 75, 95, 60,140, 3, 60}, { 90,   0, DILLO_SWEEP_LONG}}},
    {"Laser Zap",    {{3000, 1800, 1,100,  3,100, 30, 30, 1,100}, {  3,   5, DILLO_SWEEP_STOCK}}},
    {"Drone Bass",   {{ 200, 1100, 1, 30,100, 40, 80,200,10, 10}, {  0, 100, DILLO_SWEEP_STOCK}}},
    {"Full Fish",    {{ 800, 2000, 1,100,100,100,100,180, 2, 80}, {100, 100, DILLO_SWEEP_LONG}}},
};

const dillo_phrase dillo_demo = {.length = 16, .steps = {
    {DILLO_NOTE, 36, 65, DILLO_ACCENT}, {0},
    {DILLO_NOTE, 36, 65, DILLO_SLIDE}, {DILLO_NOTE, 43, 65, DILLO_ACCENT},
    {DILLO_TIE, 0, 45, 0}, {0}, {DILLO_NOTE, 39, 40, 0}, {0},
    {DILLO_NOTE, 36, 65, 0}, {DILLO_NOTE, 48, 60, DILLO_ACCENT},
    {0}, {DILLO_NOTE, 43, 50, 0},
    {DILLO_NOTE, 39, 65, DILLO_ACCENT}, {0}, {0}, {DILLO_NOTE, 43, 65, DILLO_SLIDE}
}};

void dillo_layer_init(dillo_layer *layer)
{
    memset(layer, 0, sizeof *layer);
    layer->head = DILLO_STEPS;
    bb_dillo_init(&layer->voice);
    bb_dillo_sound(&layer->voice, &bb_dillo_bank[DILLO_DEFAULT_SOUND].sound);
}

int dillo_patch_valid(const bb_dillo_patch *p)
{
    return p->cutoff_hz >= 20 && p->cutoff_hz <= 16000 && p->resonance_x100 <= 2000 &&
        p->wave <= 1 && p->envelope <= 100 && p->decay <= 100 && p->accent <= 100 &&
        p->drive <= 100 && p->slide_ms && p->attack_ms && p->tracking <= 100;
}

static float decay_coefficient(unsigned ms)
{
    return fm_expf(-1000.0f / ((float)ms * BASS303_SR));
}

int dillo_layer_sound(dillo_layer *layer, const dillo_sound *sound)
{
    if (!sound->tone.cutoff_hz) return 1;
    dillo_decay decay = sound->decay;
    if (decay.accent > DILLO_DECAY_MAX || decay.amp > DILLO_DECAY_MAX ||
        decay.sweep >= DILLO_SWEEP_COUNT) return 0;
    unsigned resonance = sound->tone.resonance_x100;
    unsigned update_sweep = decay.sweep != layer->decay.sweep ||
        (decay.sweep && resonance != layer->voice.sound.resonance_x100);
    if (memcmp(&sound->tone, &layer->voice.sound, sizeof sound->tone) &&
        !bb_dillo_sound(&layer->voice, &sound->tone)) return 0;

    bass303_t *voice = &layer->voice.voice;
    if (decay.accent != layer->decay.accent) {
        if (decay.accent)
            voice->accdec_c = decay_coefficient(DILLO_DECAY_STEP_MS * decay.accent);
        else
            bass303_set(voice, BASS303_ACCDEC, bass303_param(BASS303_ACCDEC)->def);
    }
    /* The adapter fixes BASS303_CTRL at 1, so the only decay power is c^1. */
    if (decay.amp != layer->decay.amp)
        voice->ampd_pw[1] = decay_coefficient(decay.amp ? DILLO_DECAY_STEP_MS * decay.amp : 1230u);
    if (update_sweep) {
        /* The retained 15 ms follower is stock. Extend its memory as
         * resonance rises, retaining the current charge between notes. */
        static const unsigned extra_ms[DILLO_SWEEP_COUNT] = {0, 75, 165};
        unsigned ms = 15u + extra_ms[decay.sweep] * resonance / 2000u;
        voice->rc2_c = voice->rc2_pw[1] = decay_coefficient(ms);
        voice->g2[1] = (1.0f - voice->rc2_c) * voice->env_pw[1];
    }
    layer->decay = decay;
    return 1;
}

static int32_t mix_q23(int32_t existing, int32_t bass)
{
    int32_t sum = existing + bass;
    if (sum > 8388607) return 8388607;
    if (sum < -8388607) return -8388607;
    return sum;
}

static unsigned apply_keys(dillo_layer *layer, unsigned panic)
{
    const dillo_keys next = layer->requested_keys, previous = layer->keys;
    unsigned fresh = next.note != previous.note || next.strike != previous.strike || next.gap != previous.gap;
    if (fresh) layer->blocked_key = 0;
    if (panic) {
        dillo_phrase_release(&layer->phrase, &layer->voice);
        layer->blocked_key = next.keep_after_panic ? 0 : next.note;
        layer->started = layer->live_tail = 0;
    }
    unsigned live = next.note && !layer->blocked_key;
    if (live) bb_dillo_expression(&layer->voice, next.bend, next.mod);
    if (live && (fresh || panic)) {
        unsigned legato = previous.note && previous.note != next.note && next.gap == previous.gap &&
                          layer->live_tail && layer->voice.voice.gate;
        if (!legato) dillo_phrase_release(&layer->phrase, &layer->voice);
        bb_dillo_key(&layer->voice, next.note == TG_NOTE_ZERO ? 0 : next.note, next.velocity, legato);
        layer->started = layer->live_tail = 1;
    } else if (!next.note && previous.note && layer->live_tail) {
        dillo_phrase_release(&layer->phrase, &layer->voice);
    }
    layer->keys = next;
    return live;
}

static int render_span(dillo_layer *layer, int32_t *stereo, unsigned count, float gain)
{
    if (!layer->started) return 0;
    float mono[64];
    bb_dillo_render(&layer->voice, mono, count);
    for (unsigned i = 0; i < count; ++i) {
        float sample = mono[i];
        if (!(sample >= -4.0f && sample <= 4.0f)) return -1;
        int32_t bass = (int32_t)(sample * gain * 8388607.0f);
        unsigned at = CONDUCTOR_DRY_STRIDE * i;
        stereo[at] = mix_q23(stereo[at], bass);
        if (CONDUCTOR_DRY_STRIDE == 2u)
            stereo[at + 1u] = mix_q23(stereo[at + 1u], bass);
    }
    if (!layer->running && layer->voice.voice.idle) layer->started = layer->live_tail = 0;
    return 0;
}

int dillo_layer_mix_grid(dillo_layer *layer, int32_t *stereo, unsigned frames,
                         const tg_controls *controls, unsigned next_edge, unsigned next_step,
                         const dillo_phrase *pattern)
{
    unsigned internal = pattern && controls->clock.source == TG_CLOCK_INTERNAL;
    unsigned running = internal && controls->running;
    unsigned live = apply_keys(layer, layer->silence != controls->silence);
    if (!live && !layer->live_tail) {
        if (!internal && (layer->running || layer->started)) {
            dillo_phrase_release(&layer->phrase, &layer->voice);
            layer->started = 0;
        } else if (layer->running && !running) {
            dillo_phrase_release(&layer->phrase, &layer->voice);
        }
    }
    if (!running) {
        layer->step = 0;
        layer->head = DILLO_STEPS;
    }
    layer->silence = controls->silence;
    layer->running = (uint8_t)running;
    if (!running && !layer->started) return 0;

    unsigned tempo = controls->tempo < 30 ? 30 : controls->tempo;
    unsigned period = (TG_RATE * 15u + tempo / 2u) / tempo;
    unsigned delay = tg_step_delay(period, controls->step_swing);
    float gain = controls->master * (0.24f / 127.0f);
    unsigned grid = dillo_phrase_grid(pattern);
    while (frames) {
        if (running && !next_edge) {
            if (layer->step >= grid) layer->step = 0;
            layer->head = layer->step;
            next_edge = tg_step_interval(period, delay, next_step++);
            if (!live) {
                dillo_phrase_tick(&layer->phrase, &layer->voice, pattern, layer->step, grid, next_edge, 0, layer->articulation);
                layer->live_tail = 0;
            }
            if (++layer->step == grid) layer->step = 0;
            layer->started = 1;
        }
        unsigned count = frames < 64 ? frames : 64;
        if (running && count > next_edge) count = next_edge;
        if (render_span(layer, stereo, count, gain)) return -1;
        stereo += CONDUCTOR_DRY_STRIDE * count;
        frames -= count;
        if (running) next_edge -= count;
    }
    return 0;
}


int dillo_layer_mix(dillo_layer *layer, int32_t *stereo, unsigned frames,
                    const tg_controls *controls, unsigned next_edge, const dillo_phrase *pattern)
{
    return dillo_layer_mix_grid(layer, stereo, frames, controls, next_edge, 0, pattern);
}

/* A burst can skip ties without replaying them. A skipped rest or note
 * breaks the chain, so the latest step cannot inherit the old gate. */
static int skip_ties(dillo_layer *layer, const dillo_phrase *pattern, unsigned skipped, unsigned grid)
{
    if (!layer->phrase.tie_ready || !layer->voice.voice.gate || skipped >= grid)
        return 0;
    unsigned step = layer->step;
    while (skipped--) {
        if (step >= pattern->length || pattern->steps[step].event != DILLO_TIE)
            return 0;
        if (++step == grid) step = 0;
    }
    layer->voice.slide_out = step < pattern->length &&
        pattern->steps[step].event == DILLO_NOTE && layer->phrase.outgoing_slide;
    return 1;
}

int dillo_layer_mix_usb(dillo_layer *layer, int32_t *stereo, unsigned frames,
                        const tg_controls *controls, const dillo_phrase *pattern,
                        unsigned next, unsigned events, unsigned detach, unsigned period, unsigned age)
{
    unsigned panic = layer->silence != controls->silence;
    unsigned live = apply_keys(layer, panic);
    unsigned running = pattern && controls->clock.state == TG_CLOCK_RUNNING;
    unsigned grid = dillo_phrase_grid(pattern);
    if (!live && !layer->live_tail &&
        (detach || (layer->running && !running) ||
         (running && events > 1u && !skip_ties(layer, pattern, events - 1u, grid))))
        dillo_phrase_release(&layer->phrase, &layer->voice);
    layer->running = (uint8_t)running;
    layer->silence = controls->silence;
    layer->step = (uint8_t)next;
    if (!running) layer->head = DILLO_STEPS;
    if (running && events) {
        layer->head = (uint8_t)(next ? next - 1u : grid - 1u);
        if (!live) {
            dillo_phrase_tick(&layer->phrase, &layer->voice, pattern,
                              layer->head, grid, period, age, layer->articulation);
            layer->started = 1;
            layer->live_tail = 0;
        }
    }
    float gain = controls->master * (0.24f / 127.0f);
    while (frames && layer->started) {
        unsigned count = frames < 64 ? frames : 64;
        if (render_span(layer, stereo, count, gain)) return -1;
        stereo += CONDUCTOR_DRY_STRIDE * count;
        frames -= count;
    }
    return 0;
}
