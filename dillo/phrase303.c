/* SPDX-License-Identifier: GPL-3.0-only */
#include "phrase303.h"

int dillo_phrase_set_step(dillo_phrase *phrase, unsigned step, dillo_step value)
{
    if (step >= DILLO_STEPS)
        return 0;

    switch (value.event) {
    case DILLO_REST:
        if (value.pitch || value.flags || value.gate_percent)
            return 0;
        break;
    case DILLO_TIE:
        if (value.pitch || value.flags || !value.gate_percent || value.gate_percent > 100)
            return 0;
        break;
    case DILLO_NOTE:
        if (value.pitch > 127 || (value.flags & ~(DILLO_ACCENT | DILLO_SLIDE)) ||
            !value.gate_percent || value.gate_percent > 100)
            return 0;
        break;
    default:
        return 0;
    }

    phrase->steps[step] = value;
    return 1;
}

int dillo_phrase_from_passes(dillo_phrase *phrase, const uint8_t *pitches,
                             unsigned pitch_count, const dillo_step *timing,
                             unsigned length)
{
    if (!length || length > DILLO_STEPS || pitch_count > length)
        return 0;

    dillo_phrase draft = {.length = (uint8_t)length};
    unsigned pitch = 0;
    for (unsigned step = 0; step < length; ++step) {
        dillo_step value = timing[step];
        if (value.event == DILLO_NOTE) {
            if (pitch == pitch_count)
                return 0;
            value.pitch = pitches[pitch++];
        }
        if (!dillo_phrase_set_step(&draft, step, value))
            return 0;
    }

    if (pitch != pitch_count)
        return 0;
    *phrase = draft;
    return 1;
}

void dillo_phrase_release(dillo_phrase_playback *playback, bb_dillo *voice)
{
    *playback = (dillo_phrase_playback){0};
    bb_dillo_stop(voice);
}

void dillo_phrase_tick(dillo_phrase_playback *playback, bb_dillo *voice,
                       const dillo_phrase *phrase, unsigned index, unsigned grid, uint32_t frames,
                       uint32_t elapsed, unsigned articulation)
{
    if (index >= phrase->length || phrase->length > DILLO_STEPS || !frames) {
        dillo_phrase_release(playback, voice);
        return;
    }

    dillo_step step = phrase->steps[index];
    unsigned next_step = index + 1u == grid ? 0 : index + 1u;
    unsigned next = next_step < phrase->length ? phrase->steps[next_step].event : DILLO_REST;
    unsigned onset = step.event == DILLO_NOTE;
    if (!onset && (step.event != DILLO_TIE || !playback->tie_ready || !voice->voice.gate)) {
        dillo_phrase_release(playback, voice);
        return;
    }

    /* Ties retain the original pitch and envelopes. A slide carries across
     * consecutive ties and ends at the next note, or is cancelled by a rest. */
    unsigned flags = step.flags | articulation;
    unsigned outgoing_slide = onset ? !!(flags & DILLO_SLIDE) : playback->outgoing_slide;
    unsigned tie_ready = next == DILLO_TIE;
    unsigned slide_out = next == DILLO_NOTE && outgoing_slide;
    uint32_t release_in = UINT32_MAX;
    if (!tie_ready && !slide_out) {
        uint32_t gate = (frames / 100u) * step.gate_percent +
                        (frames % 100u) * step.gate_percent / 100u;
        release_in = gate ? gate : 1u;
        /* A late USB batch can arrive after a short gate has ended. Do
         * not arm a new attack just to release it before the first sample. */
        if (elapsed >= release_in) {
            dillo_phrase_release(playback, voice);
            return;
        }
        release_in -= elapsed;
    }
    if (onset) {
        bb_note note = {
            .note = step.pitch, .gate = 1, .accent = (flags & DILLO_ACCENT) != 0
        };
        bb_dillo_step(voice, note, frames, 0);
    }
    playback->outgoing_slide = (uint8_t)outgoing_slide;
    playback->tie_ready = (uint8_t)tie_ready;
    voice->slide_out = (uint8_t)slide_out;
    voice->release_in = release_in;
}
