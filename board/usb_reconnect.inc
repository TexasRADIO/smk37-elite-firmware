/* SPDX-License-Identifier: GPL-3.0-only */
/* Run in the USB foreground. Acknowledge EP0 before leaving the bus. */
static uint8_t usb_reconnect_pending;
static uint32_t usb_reconnect_at;

static void elite_usb_retry(void)
{
    if (usb.detached) return;
    if (usb.up && !fm1_usb_sie_on())
        usb.up = 0;
    if (!usb.up && usb.config) {
        /* Release USB-owned performance input and let the host see a detach. */
        usb.config = 0;
        fm1_usb_off();
        usb.retry_ms = fm1_ms;
    }
    usb_retry(fm1_ms);
}

static int elite_usb_reconnect_allowed(void)
{
    if (usb.detached || usb.ota_req || usb.uboot_req || updating || requested.running ||
        requested.live_note || requested.arp_count) return 0;
    uint32_t csr = elite_usb_tx_csr();
    return (csr & 1u) && elite_usb_unacknowledged_sysex(csr);
}

static int elite_usb_queue_reconnect(uint16_t build)
{
    if (build != ELITE_BUILD || usb_reconnect_pending || !elite_usb_reconnect_allowed()) return 0;
    usb_reconnect_at = fm1_ms;
    usb_reconnect_pending = 1;
    return 1;
}

static void elite_usb_service_reconnect(void)
{
    if (!usb_reconnect_pending) return;
    if (!elite_usb_reconnect_allowed()) {
        usb_reconnect_pending = 0;
        return;
    }
    if (fm1_ms - usb_reconnect_at < 100u) return;
    usb_reconnect_pending = 0;
    usb_bus_reset();
    sx_ready = 0;
    elite_tx_sysex_events = 0;
    usb.up = 0;
    fm1_usb_off();
    usb.retry_ms = fm1_ms; /* usb_retry attaches after its usual one-second delay. */
}
