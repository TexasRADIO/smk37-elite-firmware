/* SPDX-License-Identifier: GPL-3.0-only */
/* Foreground is the sole producer of Elite performance output. The retained
 * endpoint drains its note ring after updater SysEx. */
static uint8_t midi_sent[MIDI_CHANNELS][16];
static uint16_t midi_sent_channels;
static uint32_t midi_tx_reset_seen;

static void midi_send_panel(const struct panel *panel)
{
    if (!usb.config || midi_tx_reset_seen != usb.resets) {
        memset(midi_sent, 0, sizeof midi_sent);
        midi_sent_channels = 0;
        midi_tx_reset_seen = usb.resets;
    }
    if (!usb.config || usb.suspended || updating || usb.ota_req || usb.uboot_req) return;
    uint64_t keys = panel->midi_tx && !panel->record && panel->page != PAGE_ASSIGN ? keyboard.down : 0;
    uint16_t pads_down = panel->midi_tx ? panel->pads_down : 0;
    if (!(keys || pads_down || midi_sent_channels)) return;

    uint8_t key_velocity[TG_ARP_NOTES] = {0}, pad_velocity[TG_ARP_NOTES] = {0};
    unsigned key_channel = panel->midi_channel, pad_channel = panel->pad_midi_channel;
    uint8_t *pads = key_channel == pad_channel ? key_velocity : pad_velocity;
    uint16_t channels = midi_sent_channels;
    if (keys) {
        channels |= (uint16_t)(1u << key_channel);
        for (unsigned key = 0; key < TG_KEYS; key++) {
            if (!(keys & (UINT64_C(1) << key))) continue;
            unsigned note = key_note(panel, key), velocity = fm1_in.key_vel[key];
            if (!velocity) velocity = 64;
            if (velocity > key_velocity[note]) key_velocity[note] = (uint8_t)velocity;
        }
    }
    if (pads_down) {
        channels |= (uint16_t)(1u << pad_channel);
        for (unsigned pad = 0; pad < TG_PADS; pad++) {
            if (!(pads_down & (1u << pad))) continue;
            unsigned note = panel->pad_note[pad], velocity = midi_pad_velocity[pad];
            if (!velocity) velocity = 64;
            if (velocity > pads[note]) pads[note] = (uint8_t)velocity;
        }
    }
    /* Release old routes first. A full ring leaves differences pending for
     * the next poll, so a discarded packet cannot leave a stuck note. */
    for (unsigned on = 0; on < 2; on++) {
        for (unsigned ch = 0; ch < MIDI_CHANNELS; ch++) {
            if (!(channels & (1u << ch))) continue;
            const uint8_t *wanted = ch == key_channel ? key_velocity : ch == pad_channel ? pad_velocity : 0;
            uint8_t *sent = midi_sent[ch];
            unsigned any = 0;
            for (unsigned note = 0; note < TG_ARP_NOTES; note++) {
                unsigned velocity = wanted ? wanted[note] : 0;
                unsigned byte = note / 8u, bit = 1u << (note % 8u);
                unsigned was_on = !!(sent[byte] & bit);
                if (!!velocity == on && was_on != on) {
                    if (mo_w - mo_r >= MQ) return;
                    unsigned cin = 8u + on;
                    midi_out_event(cin | ((cin << 4) | ch) << 8 | note << 16 | velocity << 24);
                    sent[byte] ^= (uint8_t)bit;
                    if (on) midi_sent_channels |= (uint16_t)(1u << ch);
                }
                any |= sent[byte];
            }
            if (!any) midi_sent_channels &= (uint16_t)~(1u << ch);
        }
    }
}
