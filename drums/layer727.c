/* SPDX-License-Identifier: GPL-3.0-only */
#include "layer727.h"

const uint8_t percussion_factory_kits[P727_KITS][P727_TRACKS] = {
    [P727_STOCK] = {P727_BONGO_HIGH, P727_CONGA_MUTED, P727_CONGA_LOW, P727_TIMBALE_HIGH,
        P727_TIMBALE_LOW, P727_AGOGO_HIGH, P727_CABASA, P727_WHISTLE_SHORT, P727_QUIJADA, P727_CHIME, P727_SILENT},
    [P727_LATIN] = {P727_BOMBO, P727_GUIRO, P727_CAMPANA, P727_HANAN_CONGA, P727_BONGO_HIGH,
        P727_BONGO_LOW, P727_CONGA_MUTED, P727_CONGA_OPEN, P727_CONGA_LOW, P727_TIMBALE_HIGH, P727_TIMBALE_LOW},
};
const uint8_t percussion_727_alternates[P727_TRACKS] = {
    P727_BONGO_LOW, P727_CONGA_OPEN, P727_SILENT, P727_SILENT, P727_SILENT, P727_AGOGO_LOW,
    P727_MARACAS, P727_WHISTLE_LONG, P727_SILENT, P727_SILENT, P727_SILENT,
};

/* The five exclusive Compás pairs share generators. Hanan voices are independent. */
static const uint8_t group[P727_SOUNDS] = {0, 0, 1, 1, 2, 3, 4, 5, 5, 6, 6, 7, 7, 8, 9, 10, 11, 12, 13};

/* The factory Hanan pitch knob advances (step + 1) / 4 source frames
 * per 9,804 Hz tick. Preserve the fractional phase between ticks. */
static const uint8_t hanan_increment[] = {
    [P727_BOMBO - P727_BOMBO] = 4,
    [P727_GUIRO - P727_BOMBO] = 4,
    [P727_CAMPANA - P727_BOMBO] = 11 + 1,
    [P727_HANAN_CONGA - P727_BOMBO] = 9 + 1,
};

void percussion_init(percussion_layer *layer, const percussion_bank *bank)
{
    layer->bank = bank;
    layer->active = 0;
    for (unsigned t = 0; t < P727_TRACKS; t++) layer->track_gain[t] = 128;
}

void percussion_trigger(percussion_layer *layer, unsigned track, unsigned sound, unsigned velocity)
{
    if (track >= P727_TRACKS || sound >= P727_SOUNDS || !velocity || velocity > 127) return;
    for (unsigned i = 0; i < P727_TRACKS; ++i)
        if ((layer->active & (1u << i)) && group[layer->voice[i].sound] == group[sound])
            layer->active &= (uint16_t)~(1u << i);
    const percussion_clip *clip = &layer->bank->clips[sound];
    const uint8_t *source = sound < P727_BOMBO ? layer->bank->packed727 : layer->bank->hanan;
    layer->voice[track] = (percussion_voice){.next = source + clip->offset, .remaining = clip->frames,
        .sound = (uint8_t)sound, .velocity = (uint8_t)velocity};
    if (sound < P727_BOMBO && layer->bank->rice_sizes)
        layer->voice[track].rice_end = source + clip->offset + layer->bank->rice_sizes[sound];
    layer->active |= (uint16_t)(1u << track);
}

static int32_t next_sample(percussion_voice *voice)
{
    if (voice->sound >= P727_BOMBO) {
        /* Keep the source's sample-and-hold character and exact average rate. */
        int32_t value = ((int32_t)*voice->next - 127) * 128;
        voice->phase += 9804;
        if (voice->phase >= 44100) {
            voice->phase -= 44100;
            unsigned position = voice->source_phase + hanan_increment[voice->sound - P727_BOMBO];
            unsigned advance = position >> 2;
            voice->source_phase = (uint8_t)(position & 3u);
            if (advance > voice->remaining) advance = voice->remaining;
            voice->next += advance;
            voice->remaining -= advance;
        }
        return value;
    }

    int32_t sample = voice->sample;
    if (voice->repeats) {
        --voice->repeats;
    } else {
        unsigned code = *voice->next++;
        if (code >= 128) sample = (int32_t)((code - 128u) * 256u + *voice->next++) - 16384;
        else if (code) sample += (int32_t)code - 64;
        else voice->repeats = (uint8_t)(*voice->next++ - 1u);
        voice->sample = (int16_t)sample;
    }
    --voice->remaining;
    return sample;
}

/* Keep each voice's decoder state local through a render span. The byte
 * boundary still applies to every refill, including the final block. */
static __attribute__((noinline)) void render_rice(percussion_voice *voice, int32_t *pcm, unsigned count, int32_t gain)
{
    const uint8_t *next = voice->next, *end = voice->rice_end;
    uint32_t cache = voice->rice_cache;
    unsigned available = voice->rice_available, left = voice->rice_left;
    unsigned header = voice->rice_header;
    int32_t last = voice->rice_history[0], previous = voice->rice_history[1], oldest = voice->rice_history[2];
    unsigned frames = count < voice->remaining ? count : voice->remaining;
    int32_t *end_pcm = pcm + frames;
    while (pcm != end_pcm) {
        if (!left) {
            if (available < 6) {
                if (next == end) goto truncated;
                cache |= (uint32_t)*next++ << available;
                available += 8;
            }
            header = cache & 63u;
            cache >>= 6;
            available -= 6;
            left = 128;
        }
        unsigned block = (unsigned)(end_pcm - pcm);
        if (block > left) block = left;
        left -= block;
        unsigned width = header >> 2, order = header & 3u, mask = (1u << width) - 1u;
        for (int32_t *limit = pcm + block; pcm != limit; ++pcm) {
            while (available < 16) {
                if (next == end) goto truncated;
                cache |= (uint32_t)*next++ << available;
                available += 8;
            }
            unsigned quotient = (unsigned)__builtin_ctz(cache | 0x10000u);
            if (quotient > 15u) goto truncated;
            cache >>= quotient + 1u;
            available -= quotient + 1u;
            while (available < width) {
                if (next == end) goto truncated;
                cache |= (uint32_t)*next++ << available;
                available += 8;
            }
            unsigned residual = (quotient << width) | (cache & mask);
            cache >>= width;
            available -= width;

            int32_t sample = residual & 1u ? -(int32_t)(residual >> 1) - 1 : (int32_t)(residual >> 1);
            switch (order) {
            case 1: sample += last; break;
            case 2: sample += 2 * last - previous; break;
            case 3: sample += 3 * (last - previous) + oldest; break;
            }
            if (sample < INT16_MIN || sample > INT16_MAX) goto truncated;
            oldest = previous;
            previous = last;
            last = sample;
            *pcm += sample * gain;
        }
    }
    voice->next = next;
    voice->remaining -= frames;
    voice->rice_cache = cache;
    voice->rice_available = (uint8_t)available;
    voice->rice_left = (uint8_t)left;
    voice->rice_header = (uint8_t)header;
    voice->rice_history[0] = (int16_t)last;
    voice->rice_history[1] = (int16_t)previous;
    voice->rice_history[2] = (int16_t)oldest;
    return;

truncated:
    voice->remaining = 0;
}

void percussion_render(percussion_layer *layer, int32_t *pcm, unsigned count)
{
    for (unsigned frame = 0; frame < count; ++frame) pcm[frame] = 0;
    for (unsigned track = 0; track < P727_TRACKS && layer->active; ++track) {
        unsigned bit = 1u << track;
        if (!(layer->active & bit)) continue;
        percussion_voice *voice = &layer->voice[track];
        /* x5 (was x2, +8 dB): measured 14 November, the layer sat 6-17 dB under the 808 at equal level. */
        int32_t gain = (voice->velocity * 5 * (int32_t)layer->track_gain[track]) >> 7;
        if (voice->rice_end) {
            render_rice(voice, pcm, count, gain);
        } else {
            for (unsigned frame = 0; frame < count && voice->remaining; ++frame)
                pcm[frame] += next_sample(voice) * gain;
        }
        if (!voice->remaining) layer->active &= (uint16_t)~bit;
    }
    for (unsigned frame = 0; frame < count; ++frame) {
        int32_t sum = pcm[frame];
        pcm[frame] = sum > 8388607 ? 8388607 : sum < -8388607 ? -8388607 : sum;
    }
}
