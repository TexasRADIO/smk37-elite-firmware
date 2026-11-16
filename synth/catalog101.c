/* SPDX-License-Identifier: GPL-3.0-only */
#include "catalog101.h"
/* Optional private data is generated in the build directory, never vendored. */
#ifdef TG_IMPORT_BANK
#include "imported_bank.inc"
_Static_assert(sizeof tg_imported_presets / sizeof *tg_imported_presets <= TG_IMPORT_MAX,
               "imported bank exceeds slot bounds");
#endif
unsigned tg_imported_count(void)
{
#ifdef TG_IMPORT_BANK
    return sizeof tg_imported_presets / sizeof *tg_imported_presets;
#else
    return 0;
#endif
}
const tg_imported_preset *tg_imported_at(unsigned index)
{
#ifdef TG_IMPORT_BANK
    return index < tg_imported_count() ? &tg_imported_presets[index] : 0;
#else
    (void)index;
    return 0;
#endif
}
unsigned tg_imported_match_count(unsigned category)
{
    if (category >= TG_IMPORT_CATEGORY_COUNT) return 0;
    if (category == TG_IMPORT_ALL) return tg_imported_count();
#ifdef TG_IMPORT_BANK
    return tg_imported_offsets[category+1] - tg_imported_offsets[category];
#else
    return 0;
#endif
}
unsigned tg_imported_match_index(unsigned category, unsigned position)
{
    if (position >= tg_imported_match_count(category)) return TG_IMPORT_MAX;
    if (category == TG_IMPORT_ALL) return position;
#ifdef TG_IMPORT_BANK
    return tg_imported_order[tg_imported_offsets[category]+position];
#else
    return TG_IMPORT_MAX;
#endif
}
const char *tg_imported_category_name(unsigned category)
{
    static const char *const names[] = {"ALL", "BASS", "LEAD", "KEYS", "PADS", "PERC", "FX", "SEQ ARP", "BRASS", "WINDS", "OTHER", "PLAY FIRST", "FM1 RECIPES"};
    return category < TG_IMPORT_CATEGORY_COUNT ? names[category] : "OTHER";
}
