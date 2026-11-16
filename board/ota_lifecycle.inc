/* SPDX-License-Identifier: GPL-3.0-only */
static void ota_attempt(const struct panel *panel)
{
    usb.ota_req = 0;
    updating = 1;
    elite_ota_flags = ELITE_USB_OTA_ACTIVE;
    audio_stop();
    refresh_pads(panel);
    midi_drain();
    if (flash_chip_ok())
        ota_session();
    else
        ota_show(0, -1);

    /* A successful commit resets the core. A return leaves the running
     * instrument intact and requires a fresh request before another try. */
    midi_drain();
    usb.ota_req = 0;
    elite_ota_flags &= ~ELITE_USB_OTA_ACTIVE;
    updating = 0;
}
