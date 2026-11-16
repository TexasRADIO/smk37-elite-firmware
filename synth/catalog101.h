/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CATALOG101_H
#define CATALOG101_H
#include "telegram101.h"
enum { TG_IMPORT_QUIET = 1, TG_IMPORT_ADAPTED = 2, TG_IMPORT_GAPS = 4, TG_IMPORT_STUDIO = 8, TG_IMPORT_FM1 = 16, TG_IMPORT_MAX = 640 };
enum { TG_IMPORT_ALL, TG_IMPORT_BASS, TG_IMPORT_LEAD, TG_IMPORT_KEYS,
       TG_IMPORT_PADS, TG_IMPORT_PERC, TG_IMPORT_FX, TG_IMPORT_SEQUENCE,
       TG_IMPORT_BRASS, TG_IMPORT_WINDS, TG_IMPORT_OTHER, TG_IMPORT_FIRST, TG_IMPORT_FM1_RECIPES, TG_IMPORT_CATEGORY_COUNT };
typedef struct { tg_preset sound; uint8_t flags, category; } tg_imported_preset;
unsigned tg_imported_count(void);
const tg_imported_preset *tg_imported_at(unsigned index);
unsigned tg_imported_match_count(unsigned category);
/* Position within a category -> immutable original index, or TG_IMPORT_MAX. */
unsigned tg_imported_match_index(unsigned category, unsigned position);
const char *tg_imported_category_name(unsigned category);
#endif
