/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include "../drums/layer727.h"
#include "library_region.h"
#include "fm1_flash_map.h"
#include "vendor/firmware/stub/stub_core.h"

/* The same three read windows that the offline planner uses. They do not
 * extend the board's flash write allowlist. */
static int percussion_flash_range(uint32_t offset, uint32_t size)
{
    return FL_IN(offset, size, FL_SMP_LO, FL_SMP_HI) ||
           FL_IN(offset, size, ELITE_LIBRARY_HI, FL_OTA_LO) ||
           FL_IN(offset, size, FL_DATA_LO, ELITE_LIBRARY_LO);
}

/* The application supplies metadata compiled from the qualified bank.
 * Validate the complete pool before giving the renderer any flash pointer. */
static int percussion_flash_open(percussion_bank *bank, const uint8_t *flash,
                                 const percussion_clip clips[P727_SOUNDS],
                                 const uint32_t sizes[P727_SOUNDS],
                                 const uint32_t checksums[P727_SOUNDS],
                                 void (*checkpoint)(void))
{
    *bank = (percussion_bank){0};
    if (!flash) return 0;
    for (unsigned i = 0; i < P727_SOUNDS; ++i) {
        uint32_t off = clips[i].offset, size = sizes[i];
        if (!clips[i].frames || !percussion_flash_range(off, size) ||
            (i < P727_BOMBO ? size < 3 : size != clips[i].frames))
            return 0;
        for (unsigned j = 0; j < i; ++j) {
            uint32_t prior = clips[j].offset;
            if (off < prior + sizes[j] && prior < off + size) return 0;
        }
    }

    stub_crc_t crc;
    stub_crc32_init(&crc);
    for (unsigned i = 0; i < P727_SOUNDS; ++i) {
        if (stub_crc32_bytes(&crc, flash + clips[i].offset, sizes[i]) != checksums[i])
            return 0;
        if (checkpoint) checkpoint();
    }
    *bank = (percussion_bank){.packed727 = flash, .hanan = flash, .clips = clips,
                             .rice_sizes = sizes};
    return 1;
}
