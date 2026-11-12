/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ELITE_OTA_TRACE_H
#define ELITE_OTA_TRACE_H

#include <stdint.h>

enum elite_ota_phase {
    ELITE_OTA_ENTER = 1, ELITE_OTA_REQUEST, ELITE_OTA_REPLY, ELITE_OTA_STATUS,
    ELITE_OTA_RESTART_AUDIO, ELITE_OTA_COMMIT
};

#define ELITE_OTA_TRACE_MAGIC 0xea000000u
#define ELITE_OTA_TRACE_FAULT 0x00800000u
#define ELITE_OTA_TRACE_DETAIL 0x000fffffu

/* A single aligned RAM store is the complete checkpoint. Reserve the maximum
 * detail for special protocol addresses outside the 1 MiB flash image. */
static inline uint32_t elite_ota_checkpoint(unsigned phase, uint32_t detail)
{
    if (detail > ELITE_OTA_TRACE_DETAIL)
        detail = ELITE_OTA_TRACE_DETAIL;
    return ELITE_OTA_TRACE_MAGIC | (phase << 20) | detail;
}

static inline uint32_t elite_ota_previous(uint32_t checkpoint, int power_on)
{
    unsigned phase = (checkpoint >> 20) & 7u;
    return !power_on && (checkpoint & 0xff000000u) == ELITE_OTA_TRACE_MAGIC &&
           phase >= ELITE_OTA_ENTER && phase <= ELITE_OTA_COMMIT ? checkpoint : 0;
}
#endif
