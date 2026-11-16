/* SPDX-License-Identifier: GPL-3.0-only */
#include "elite_ota_loader.inc"

static uint32_t ota_staged(void);
_Static_assert(sizeof elite_loader_body <= OTA_AREA_LEN - 0x200u, "Elite loader exceeds staging");

static __attribute__((noinline)) int elite_stage_loader(uint32_t offset, uint32_t size, uint32_t expected_crc, uint8_t *scratch)
{
    if (size != sizeof elite_loader_body || expected_crc != ELITE_LOADER_CRC)
        return -7;
    uint32_t pos, span;
    for (pos = 0; pos < size; pos += span) {
        span = size - pos > 512u ? 512u : size - pos;
        if (ota_read(offset + 32u + pos, scratch, span))
            return -9;
        if (!ota_memeq(scratch, elite_loader_body + pos, span))
            return -11;
    }

    if (ota_staged() == size + 32u) {
        for (pos = 0; pos < size; pos += span) {
            span = size - pos > 512u ? 512u : size - pos;
            if (ota_fread(OTA_AREA + 32u + pos, scratch, span))
                return -11;
            if (!ota_memeq(scratch, elite_loader_body + pos, span))
                break;
        }
        if (pos == size)
            return 0;
    }

    ota_touched = 1;
    for (pos = 0; pos < OTA_AREA_LEN; pos += 0x1000u)
        if (ota_erase(OTA_AREA + pos))
            return -8;
    for (pos = 0; pos < size; pos += span) {
        span = size - pos > 512u ? 512u : size - pos;
        /* Flash programming disables XIP. Use the existing RAM scratch. */
        memcpy(scratch, elite_loader_body + pos, span);
        if (ota_prog(OTA_AREA + 32u + pos, scratch, span))
            return -10;
        if (ota_fread(OTA_AREA + 32u + pos, scratch, span) ||
            !ota_memeq(scratch, elite_loader_body + pos, span))
            return -11;
    }
    if (ota_stage_head(size) || ota_staged() != size + 32u)
        return -12;
    /* A verified staged loader is safe even if host confirmation is lost. */
    ota_touched = 0;
    return 0;
}
