/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Elite application EP1 IN, adapted from the pinned Felucca USB driver. */
static USB_OUTLINE void ep1_tx(void)
{
    uint32_t csr;
    if (!elite_usb_select_tx(&csr) || (csr & 1u))
        return;
    SX_TAKEN();

    if (csr & 0x80u) {
        sie_wr(S_TXCSR1, csr & ~0x80u);
        if (usb.timeouts || !fm1_usb_sie_on())
            return;
    }

    uint32_t sysex = so_r, midi = mo_r, n = 0;
    while (sysex != so_w && n < sizeof ep1tx) {
        uint32_t event = sx_out_q[sysex++ % SXQ];
        ep1tx[n++] = (uint8_t)event;
        ep1tx[n++] = (uint8_t)(event >> 8);
        ep1tx[n++] = (uint8_t)(event >> 16);
        ep1tx[n++] = (uint8_t)(event >> 24);
    }
    uint32_t sysex_events = n / 4u;
    while (!sx_busy && sysex == so_w && midi != mo_w && n < sizeof ep1tx) {
        RING_PUBLISH();
        uint32_t event = midi_out_q[midi++ % MQ];
        ep1tx[n++] = (uint8_t)event;
        ep1tx[n++] = (uint8_t)(event >> 8);
        ep1tx[n++] = (uint8_t)(event >> 16);
        ep1tx[n++] = (uint8_t)(event >> 24);
    }
    if (!n)
        return;

    fm1_usb_ep_send(1, ep1tx, n);
    fm1_usb_rx_sync(); /* Complete the DMA address and count writes before arming IN. */
    sie_wr(S_TXCSR1, 1u);
    if (usb.timeouts || !fm1_usb_sie_on())
        return;

    /* A failed controller write leaves both queues intact for the next poll. */
    RING_PUBLISH();
    so_r = sysex;
    mo_r = midi;
    elite_tx_sysex_events = sysex_events;
    usb.tx_pkts++;
}
