/* SPDX-License-Identifier: GPL-3.0-only */
/* App-only hooks in the retained USB driver. None accesses flash. */
#include "board/usb_diag.h"

static elite_usb_diag usb_diag;
static uint32_t elite_tx_sysex_events, elite_ota_flags;
static void elite_usb_audio_send(uint16_t length);
static void elite_usb_midi_send(uint16_t length);
static void elite_usb_clock_send(uint16_t length);
static void elite_usb_boot_send(uint16_t length);
static void elite_usb_activity_send(uint16_t length);
#ifdef ELITE_DRUMS808
static void elite_usb_conductor_send(uint16_t length);
static void elite_usb_conductor_live_send(uint16_t length);
static void elite_usb_conductor_kits_send(uint16_t length);
static void elite_usb_conductor_cues_send(uint16_t length);
#ifdef ELITE_RENDER_PROFILE
static void elite_usb_render_profile_send(uint16_t length);
#endif
static void elite_usb_recall_send(uint16_t length);
#endif
static int elite_usb_queue_reconnect(uint16_t build);

static int elite_usb_select_tx(uint32_t *csr)
{
    if (!usb.up || !usb.config || !fm1_usb_sie_on()) return 0;
    sie_wr(S_INDEX, 1);
    if (usb.timeouts) return 0;
    *csr = sie_rd(S_TXCSR1);
    /* A timed-out read returns zero, which must never mean the DMA buffer is free. */
    return !usb.timeouts && fm1_usb_sie_on();
}

static uint32_t elite_usb_tx_csr(void)
{
    if (!usb.up || !usb.config) return 0;
    uint32_t csr;
    int valid = elite_usb_select_tx(&csr);
    sie_wr(S_INDEX, 0);
    return valid ? csr : 0;
}

static uint32_t elite_usb_unacknowledged_sysex(uint32_t csr)
{
    return so_w - so_r + ((csr & 1u) ? elite_tx_sysex_events : 0u);
}

static void elite_usb_diag_rx(const uint8_t *bytes, uint32_t length)
{
    usb_diag.last_rx_bytes = length;
    for (unsigned i = 0; i < sizeof usb_diag.rx_header; ++i)
        usb_diag.rx_header[i] = i < length ? bytes[i] : 0;
}

static void elite_usb_diag_send(uint16_t length)
{
    usb_diag.magic = ELITE_USB_DIAG_MAGIC;
    usb_diag.build = ELITE_BUILD;
    usb_diag.rx = usb.rx_pkts;
    usb_diag.tx = usb.tx_pkts;
    usb_diag.acknowledged = so_gone;
    usb_diag.resets = usb.resets;
    usb_diag.setups = usb.setups;
    usb_diag.flags = usb.config | ((uint32_t)usb.sx_on << 8) |
                     ((uint32_t)sx_collect << 9) | ((uint32_t)sx_ready << 10) | elite_ota_flags;
    usb_diag.frame_bytes = sx_frame_len;
    usb_diag.sysex_pos = sx_pos;
    usb_diag.midi_queue_depth = mi_w - mi_r;
    e0_send((const uint8_t *)&usb_diag, sizeof usb_diag, length);
}
