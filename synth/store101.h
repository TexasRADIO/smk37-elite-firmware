/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef STORE101_H
#define STORE101_H
#include "telegram101.h"

enum { TG_USER_SLOTS = 64, TG_NAME_BYTES = 16, TG_STORE_SECTOR = 4096,
       TG_STORE_BANK = 4 * TG_STORE_SECTOR, TG_STORE_BYTES = 2 * TG_STORE_BANK };
typedef struct { char name[TG_NAME_BYTES]; tg_patch patch; } tg_saved_patch;
typedef struct { char name[TG_NAME_BYTES]; uint8_t length, steps[TG_STEPS], gate; } tg_saved_phrase;
enum { TG_CONTROL_LEVEL = TG_PARAMETERS, TG_CONTROL_TEMPO, TG_CONTROL_OSC_OCTAVE, TG_CONTROL_KEY_OCTAVE, TG_CONTROL_ENV_WIDE, TG_CONTROL_FILTER_LOW, TG_CONTROL_LFO_RETRIGGER, TG_CONTROL_FILTER_IDLE_HOLD, TG_CONTROL_DRIVE, TG_CONTROL_FILTER_ATTACK, TG_CONTROL_FILTER_DECAY, TG_CONTROL_FILTER_SUSTAIN, TG_CONTROL_FILTER_RELEASE, TG_CONTROL_ACCENT_TONE, TG_CONTROL_ACCENT_LEVEL, TG_CONTROL_PWM_SOURCE, TG_CONTROL_COUNT,
       TG_LAYOUT_BYTES = 49, TG_LAYOUT_PROFILES = 8 };
typedef struct {
    uint8_t knobs[2][8], faders[4][4], pads[TG_PADS], brightness;
} tg_layout;
typedef struct {
    uint8_t patches_present[8], phrases_present[8];
    tg_saved_patch patches[TG_USER_SLOTS];
    tg_saved_phrase phrases[TG_USER_SLOTS];
    tg_layout layouts[TG_LAYOUT_PROFILES];
    uint8_t profile; /* Last explicitly saved controller profile. */
} tg_library;

/* Relative offsets in a dedicated, exclusively owned 32 KiB region.
 * The board must verify ownership/ranges before exposing these callbacks.
 * No physical addresses or device writes are supplied by this module.
 * Callbacks return zero on success. program receives <=256 bytes within
 * one page. erase receives an aligned sector offset. Read may span a bank.
 * All calls are synchronous; do not call save from an audio interrupt or
 * while playback, held notes, effects tails or an update are active. */
typedef struct {
    void *context;
    int (*read)(void *, uint32_t, void *, uint32_t);
    int (*erase)(void *, uint32_t);
    int (*program)(void *, uint32_t, const void *, uint32_t);
} tg_store_io;
enum { TG_STORE_OK = 0, TG_STORE_EMPTY = 1, TG_STORE_IO = -1,
       TG_STORE_INVALID = -2, TG_STORE_CONFLICT = -3 };

void tg_library_init(tg_library *);
enum { TG_PATCH_WIRE_BYTES = TG_PARAMETERS + 21,
       TG_PATCH_SCENE_BYTES = TG_PATCH_WIRE_BYTES - 10,
       TG_PACKED_PARAMS = (7 * TG_PARAMETERS + 7) / 8,
       TG_PATCH_STORE_BYTES = TG_PATCH_SCENE_BYTES - TG_PARAMETERS + TG_PACKED_PARAMS };
void tg_patch_pack_scene(uint8_t out[TG_PATCH_SCENE_BYTES], const tg_patch *);
int tg_patch_unpack_scene(tg_patch *, const uint8_t in[TG_PATCH_SCENE_BYTES]);
void tg_patch_encode(uint8_t out[TG_PATCH_WIRE_BYTES], const tg_patch *);
int tg_patch_decode(tg_patch *, const uint8_t in[TG_PATCH_WIRE_BYTES]);
extern const uint8_t tg_default_knob_map[2][8];
void tg_layout_default(tg_layout *);
int tg_library_put_patch(tg_library *, unsigned, const char *, const tg_controls *);
int tg_library_put_phrase(tg_library *, unsigned, const char *, const tg_controls *);
int tg_library_get_patch(const tg_library *, unsigned, tg_controls *);
int tg_library_find_phrase(const tg_library *, unsigned, const tg_saved_phrase **);
int tg_library_get_phrase(const tg_library *, unsigned, tg_controls *);

/* Caller owns a TG_STORE_BANK-byte scratch buffer, outside the audio stack.
 * load leaves the destination unchanged on failure. save retains the prior
 * committed bank until a fully verified new bank is committed. On IO error
 * during the commit marker, re-load to determine whether commit completed. */
int tg_store_load(const tg_store_io *, tg_library *, uint8_t *scratch);
int tg_store_save(const tg_store_io *, const tg_library *, uint8_t *scratch);
/* Require each bank to be erased or carry our complete, CRC-valid header.
 * A torn payload can be reclaimed only when another valid bank survives.
 * Unidentified contents, including a torn header, are never auto-erased. */
int tg_store_can_save(const tg_store_io *, uint8_t *scratch);

/* Extended tails use the lossless v15 patch layout in the same bank.
 * Smaller tails continue to use v14 when creating a library. */
enum { TG_STORE_V14_TAIL_MAX = 3448,
       TG_STORE_TAIL_MAX = TG_STORE_V14_TAIL_MAX + TG_USER_SLOTS * (TG_PATCH_WIRE_BYTES - TG_PATCH_STORE_BYTES) };
/* Use only after a successful load or tail read left the selected bank in scratch.
 * When actual is supplied, size is capacity. Otherwise the tail must match size. */
void tg_store_loaded_identity(const uint8_t *scratch, uint32_t *generation, uint32_t *library_crc);
int tg_store_loaded_tail(const uint8_t *scratch, uint8_t *tail, unsigned size, unsigned *actual);
int tg_store_load_tail(const tg_store_io *, uint8_t *tail, unsigned size, uint8_t *scratch);
int tg_store_save_tail(const tg_store_io *, const tg_library *, const uint8_t *tail,
                       unsigned size, uint8_t *scratch);
#endif
