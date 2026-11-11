/* SPDX-License-Identifier: GPL-3.0-only */
#include "elite.h"

#include <string.h>

_Static_assert(FM1_SAMPLE_RATE == 44100 && FM1_BLOCK_FRAMES == 128 && FM1_CHANNELS == 2,
               "Check DSP rate and audio contract before changing board profile");
_Static_assert(D8_NUM == BB_DRUM_TRACKS, "Drum trigger mapping must agree with BB");

#ifdef ELITE_DRUM_CACHE
#include "drum_cache.inc"

static void strike(elite *state, unsigned drum, unsigned lane, float velocity)
{
    drum_playback *voice = &state->drums[drum][lane];
    *voice = (drum_playback){
        .decoder = {0, drum_samples[drum][lane].initial_index},
        .gain = velocity * (1.0f / 16384.0f), .active = 1
    };
    if (lane == D8_CH && state->drums[drum][D8_OH].active &&
        !state->drums[drum][D8_OH].choke)
        state->drums[drum][D8_OH].choke = 88;
}

static void render_drums(elite *state, unsigned drum, unsigned frames)
{
    memset(state->mono, 0, frames * sizeof state->mono[0]);
    for (unsigned lane = 0; lane < D8_NUM; ++lane) {
        drum_playback *voice = &state->drums[drum][lane];
        const drum_sample *sample = &drum_samples[drum][lane];
        if (!voice->active)
            continue;
        const uint8_t *bytes = drum_data + sample->offset;
        unsigned count = sample->length - voice->frame;
        if (count > frames)
            count = frames;
        unsigned choking = voice->choke;
        if (choking && count > voice->choke)
            count = voice->choke;
        unsigned frame = voice->frame;
        for (unsigned i = 0; i < count; ++i, ++frame) {
            unsigned code = (bytes[frame >> 1] >> ((frame & 1u) * 4u)) & 15u;
            float gain = voice->gain;
            if (voice->choke)
                gain *= (float)(--voice->choke) * (1.0f / 88.0f);
            state->mono[i] += (float)drum_decode(&voice->decoder, code) * gain;
        }
        if (frame == sample->length || (choking && !voice->choke))
            voice->active = 0;
        voice->frame = frame;
    }
}
#else
#include "drum_sound_adapter.inc"

static void strike(elite *state, unsigned drum, unsigned lane, float velocity)
{
    drum808_trigger(&state->drums[drum], lane, velocity);
}

static void render_drums(elite *state, unsigned drum, unsigned frames)
{
    for (unsigned i = 0; i < frames; ++i) {
        state->mono[i] = 0.0f;
        state->delay[i] = 0.0f;
        state->reverb[i] = 0.0f;
    }
    bb_drum808_render_span(&state->drums[drum], state->mono, state->reverb, state->delay, (int)frames);
}
#endif

void elite_init(elite *state)
{
    memset(state, 0, sizeof *state);
    bb_init(&state->clock, FM1_SAMPLE_RATE);
#ifndef ELITE_DRUM_CACHE
    for (unsigned drum = 0; drum < ELITE_DRUMS; ++drum) {
        drum808_init(&state->drums[drum]);
        audio_drum_sound(&state->drums[drum], &state->clock.drums[drum].sound);
    }
#endif
    bb_dillo_init(&state->dillo);
    bb_dillo_sound(&state->dillo, &state->clock.dillo);
    bb_telegram_init(&state->telegram);
    bb_output_init(&state->output, FM1_SAMPLE_RATE, 1.0f);
    elite_set_mix(state, ELITE_DRUM_A, 100, -25, 0);
    elite_set_mix(state, ELITE_DRUM_B, 80, 25, 0);
    elite_set_mix(state, ELITE_DILLO, 64, 0, 0);
    elite_set_mix(state, ELITE_TELEGRAM, 82, 0, 0);
}

int elite_set_drum_pattern(elite *state, unsigned drum, const bb_drum_step *steps, unsigned length)
{
    const unsigned mask = (1u << D8_NUM) - 1u;
    if (!state || drum >= ELITE_DRUMS || !steps || !length || length > BB_STEPS)
        return 0;
    for (unsigned i = 0; i < length; ++i)
        if ((steps[i].hits & ~mask) || (steps[i].accents & ~steps[i].hits))
            return 0;
    bb_set_length(&state->clock, drum, length);
    memmove(state->clock.drums[drum].steps, steps, length * sizeof *steps);
    return 1;
}

int elite_set_notes(elite *state, unsigned synth, const bb_note *notes, unsigned length)
{
    if (!state || synth >= BB_SYNTHS || !notes || !length || length > BB_STEPS)
        return 0;
    for (unsigned i = 0; i < length; ++i)
        if (notes[i].note > 127 || notes[i].gate > 1 || notes[i].accent > 1 || notes[i].slide > 1)
            return 0;
    bb_set_synth_length(&state->clock, synth, length);
    memmove(state->clock.synths[synth].notes, notes, length * sizeof *notes);
    return 1;
}

int elite_set_drum_kit(elite *state, unsigned drum, unsigned kit)
{
#ifdef ELITE_DRUM_CACHE
    /* The board cache contains only the two fixture kits. */
    if ((drum == ELITE_DRUM_A && kit != 0) || (drum == ELITE_DRUM_B && kit != 3))
        return 0;
#endif
    if (!state || drum >= ELITE_DRUMS || !bb_recall_drum(&state->clock, drum, kit))
        return 0;
#ifndef ELITE_DRUM_CACHE
    audio_drum_sound(&state->drums[drum], &state->clock.drums[drum].sound);
#endif
    return 1;
}

int elite_set_synth_patch(elite *state, unsigned synth, unsigned patch)
{
    if (!state)
        return 0;
    if (synth == 0 && patch < BB_DILLO_PATCHES) {
        bb_recall_dillo(&state->clock, patch);
        return bb_dillo_sound(&state->dillo, &state->clock.dillo);
    }
    if (synth == 1 && patch < BB_TELEGRAM_PATCHES) {
        bb_recall_telegram(&state->clock, &bb_telegram_bank[patch].sound);
        return bb_telegram_sound(&state->telegram, &state->clock.telegram);
    }
    return 0;
}

int elite_set_mix(elite *state, unsigned channel, unsigned level, int pan, int mute)
{
    if (!state || channel >= ELITE_CHANNELS || level > 127 || pan < -100 || pan > 100)
        return 0;
    elite_fader *fader = &state->mix[channel];
    float gain = mute ? 0.0f : (float)level / 127.0f;
    fader->target_left = gain * (pan > 0 ? (100 - pan) * 0.01f : 1.0f);
    fader->target_right = gain * (pan < 0 ? (100 + pan) * 0.01f : 1.0f);
    if (state->clock.running) {
        fader->remaining = ELITE_GAIN_RAMP;
        fader->delta_left = (fader->target_left - fader->left) / ELITE_GAIN_RAMP;
        fader->delta_right = (fader->target_right - fader->right) / ELITE_GAIN_RAMP;
    } else {
        fader->remaining = 0;
        fader->left = fader->target_left;
        fader->right = fader->target_right;
    }
    return 1;
}

void elite_start(elite *state)
{
    if (!state->clock.running) {
        memset(state->hits, 0, sizeof state->hits);
        memset(state->notes, 0, sizeof state->notes);
        bb_start(&state->clock);
    }
}

void elite_stop(elite *state)
{
    bb_stop(&state->clock);
    memset(state->live_velocity, 0, sizeof state->live_velocity);
    bb_dillo_stop(&state->dillo);
    bb_telegram_stop(&state->telegram);
}

int elite_strike(elite *state, unsigned drum, unsigned lane, unsigned velocity)
{
    if (!state || drum >= ELITE_DRUMS || lane >= D8_NUM || !velocity || velocity > 127)
        return 0;
    strike(state, drum, lane, (float)velocity / 127.0f);
    ++state->hits[drum];
    return 1;
}

int elite_live_note(elite *state, unsigned synth, unsigned note, unsigned velocity)
{
    if (!state || synth >= BB_SYNTHS || note > 127 || velocity > 127)
        return 0;
    int legato = state->live_velocity[synth] != 0;
    if (legato == (velocity != 0) && (!velocity || note == state->live_note[synth]))
        return 1;
    state->live_note[synth] = (uint8_t)note;
    state->live_velocity[synth] = (uint8_t)velocity;
    if (synth == 0) {
        if (velocity)
            bb_dillo_key(&state->dillo, note, velocity, legato);
        else
            bb_dillo_stop(&state->dillo);
    } else if (velocity) {
        unsigned frames = FM1_SAMPLE_RATE * 60u / (4u * state->clock.bpm);
        bb_telegram_key(&state->telegram, note, velocity, legato, frames);
    } else {
        bb_telegram_stop(&state->telegram);
    }
    return 1;
}

static void trigger_drum(void *context, const bb_hit *hit)
{
    elite *state = context;
    if (hit->machine < ELITE_DRUMS && hit->voice < D8_NUM) {
        float velocity = hit->velocity == 127 ? 1.0f : D8_VEL_NORMAL;
        strike(state, hit->machine, hit->voice, velocity);
        ++state->hits[hit->machine];
    }
}

static void trigger_note(void *context, const bb_note_event *event)
{
    elite *state = context;
    if (state->live_velocity[event->synth])
        return;
    const bb_core *clock = &state->clock;
    unsigned swing = clock->swing_odd ? 100u - clock->active_swing : clock->active_swing;
    uint32_t denominator = clock->sample_rate * 120u * swing;
    uint32_t increment = (uint32_t)clock->bpm * 400u;
    uint32_t duration = (denominator - clock->phase + increment - 1u) / increment;
    if (event->note.gate)
        ++state->notes[event->synth];
    if (event->synth == 0) {
        const bb_sequence *sequence = &clock->synths[0];
        unsigned next = (event->step + 1u) % sequence->length;
        bb_dillo_step(&state->dillo, event->note, duration, sequence->notes[next].gate);
    } else {
        bb_telegram_step(&state->telegram, event->note, duration);
    }
}

static void mix_span(elite *state, unsigned channel, unsigned frames)
{
    elite_fader *fader = &state->mix[channel];
    float *left = state->left + state->offset;
    float *right = state->right + state->offset;
    float left_gain = fader->left, right_gain = fader->right;
    unsigned i = 0;
    while (i < frames && fader->remaining) {
        left_gain += fader->delta_left;
        right_gain += fader->delta_right;
        if (!--fader->remaining) {
            left_gain = fader->target_left;
            right_gain = fader->target_right;
        }
        left[i] += state->mono[i] * left_gain;
        right[i] += state->mono[i] * right_gain;
        ++i;
    }
    fader->left = left_gain;
    fader->right = right_gain;
    for (; i < frames; ++i) {
        left[i] += state->mono[i] * left_gain;
        right[i] += state->mono[i] * right_gain;
    }
}

static void render_span(void *context, uint32_t frames)
{
    elite *state = context;
    for (unsigned drum = 0; drum < ELITE_DRUMS; ++drum) {
#ifndef ELITE_DRUM_CACHE
        if (!state->mix[drum].remaining && !drum808_active(&state->drums[drum]))
            continue;
#endif
        render_drums(state, drum, frames);
        mix_span(state, drum, frames);
    }
    bb_dillo_render(&state->dillo, state->mono, frames);
    mix_span(state, ELITE_DILLO, frames);
    bb_telegram_render(&state->telegram, state->mono, frames);
    mix_span(state, ELITE_TELEGRAM, frames);
    state->offset += frames;
}

int elite_render(elite *state, int32_t *pcm, unsigned frames)
{
    if (!state || !pcm || frames != FM1_BLOCK_FRAMES)
        return -1;
    state->offset = 0;
    for (unsigned i = 0; i < frames; ++i) {
        state->left[i] = 0.0f;
        state->right[i] = 0.0f;
    }
    bb_process_render(&state->clock, frames, trigger_drum, trigger_note, render_span, state);
    bb_output_process(&state->output, state->left, state->right, frames);
    for (unsigned i = 0; i < frames; ++i) {
        pcm[2 * i] = (int32_t)(state->left[i] * 8388608.0f);
        pcm[2 * i + 1] = (int32_t)(state->right[i] * 8388608.0f);
    }
    return 0;
}
