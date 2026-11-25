/* SPDX-License-Identifier: GPL-3.0-only */
/* Application-only USB identity.  board/main.c calls this once with IRQs off,
 * after the flash driver is available and before usb_start attaches the SIE.
 * The retained update loader is built before this file is staged and never
 * includes it. */
static uint8_t elite_usb_device_descriptor[18];
static uint8_t elite_usb_serial_descriptor_cache[ELITE_USB_SERIAL_BYTES];
static uint8_t elite_usb_identity_initialized;

static void elite_usb_identity_init(void)
{
    uint8_t uid[16] = {0};
    if (elite_usb_identity_initialized)
        return;
    memcpy(elite_usb_device_descriptor, DEV_DESC, sizeof elite_usb_device_descriptor);
    elite_usb_device_descriptor[16] = 0; /* safe failure: no shared serial */
    elite_usb_identity_initialized = 1;
    if (!FL_JEDEC_OK(FL_FAR(fl_jedec_ram)()) || FL_FAR(fl_uid_ram)(uid) ||
        !elite_usb_serial_descriptor(elite_usb_serial_descriptor_cache, uid)) {
        memset(uid, 0, sizeof uid);
        return;
    }
    elite_usb_device_descriptor[16] = ELITE_USB_SERIAL_INDEX;
    memset(uid, 0, sizeof uid);
}
