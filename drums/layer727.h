/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>

enum {
    P727_BONGO_HIGH, P727_BONGO_LOW, P727_CONGA_MUTED, P727_CONGA_OPEN, P727_CONGA_LOW,
    P727_TIMBALE_HIGH, P727_TIMBALE_LOW, P727_AGOGO_HIGH, P727_AGOGO_LOW, P727_CABASA, P727_MARACAS,
    P727_WHISTLE_SHORT, P727_WHISTLE_LONG, P727_QUIJADA, P727_CHIME,
    P727_BOMBO, P727_GUIRO, P727_CAMPANA, P727_HANAN_CONGA, P727_SOUNDS,
    P727_TRACKS = 11, P727_SILENT = 255
};

enum { P727_STOCK, P727_LATIN, P727_KITS };

/* Compás streams run at 44.1 kHz. Hanan bytes are unsigned PCM at 9804 Hz. */
typedef struct { uint32_t offset, frames; } percussion_clip;
typedef struct {
    const struct percussion_attack_cache *attacks;
    const uint8_t *packed727;
    const uint8_t *hanan;
    const percussion_clip *clips;
    const uint32_t *rice_sizes; /* Optional byte bounds for the 15 predictive streams. */
} percussion_bank;

typedef struct {
    const uint8_t *next, *rice_end;
    uint32_t remaining, rice_cache;
    int16_t rice_history[3];
    uint8_t rice_available, rice_left, rice_header;
    int16_t sample;
    uint16_t phase;
    uint8_t repeats, sound, velocity, source_phase;
} percussion_voice;

enum { P727_ATTACK_FRAMES = 448 };
typedef struct percussion_attack_cache {
    int16_t pcm[P727_BOMBO][P727_ATTACK_FRAMES];
    percussion_voice after[P727_BOMBO];
    uint16_t length[P727_BOMBO];
} percussion_attack_cache;

/* Build once in the foreground after verifying this bank. The renderer
 * can resume the original lossless stream at any callback boundary. */
void percussion_prepare_attacks(percussion_bank *bank, percussion_attack_cache *cache);

typedef struct {
    percussion_voice voice[P727_TRACKS];
    const percussion_bank *bank;
    uint16_t active;
    uint8_t track_gain[P727_TRACKS]; /* 128 unity: the 727 page's per-instrument knobs */
} percussion_layer;

extern const uint8_t percussion_factory_kits[P727_KITS][P727_TRACKS];
extern const uint8_t percussion_727_alternates[P727_TRACKS];
void percussion_init(percussion_layer *layer, const percussion_bank *bank);
void percussion_trigger(percussion_layer *layer, unsigned track, unsigned sound, unsigned velocity);
/* Writes a mono Q23 dry bus at the Elite sample rate. */
void percussion_render(percussion_layer *layer, int32_t *pcm, unsigned count);
