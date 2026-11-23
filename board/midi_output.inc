/* SPDX-License-Identifier: GPL-3.0-only */
/* Foreground is the sole producer of Elite performance output. The retained
 * endpoint drains its note ring after updater SysEx. */
static uint8_t midi_sent[MIDI_CHANNELS][16];
static uint16_t midi_sent_channels;
static uint32_t midi_tx_reset_seen;
static uint8_t midi_pc_seen;

static void midi_send_panel(const struct panel *panel)
{
    /* MIDI OUT: 1 USB, 2 TRS, 3 both. The TRS jack needs no computer, so only a USB-only route waits for one. */
    unsigned trs = panel->midi_tx & 2u, usb_up = usb.config && !usb.suspended;
    if ((!usb.config && !trs) || midi_tx_reset_seen != usb.resets) {
        memset(midi_sent, 0, sizeof midi_sent);
        midi_sent_channels = 0;
        midi_tx_reset_seen = usb.resets;
    }
    if (!(usb_up || trs) || updating || usb.ota_req || usb.uboot_req) return;
    if (panel->band_pc_seq != midi_pc_seen) {
        midi_pc_seen = panel->band_pc_seq;
        if (panel->midi_tx)
            elite_midi_out(0x0cu | (0xc0u | panel->midi_channel) << 8 | (uint32_t)(panel->band_pc & 0x7fu) << 16);
    }
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
                    elite_midi_out(cin | ((cin << 4) | ch) << 8 | note << 16 | velocity << 24);
                    sent[byte] ^= (uint8_t)bit;
                    if (on) midi_sent_channels |= (uint16_t)(1u << ch);
                }
                any |= sent[byte];
            }
            if (!any) midi_sent_channels &= (uint16_t)~(1u << ch);
        }
    }
}

 


static uint8_t sideman_sent[TG_SIDEMAN_BYTES];
static uint32_t sideman_reset_seen = UINT32_MAX, sideman_ms;

static void sideman_fill(const struct panel *panel, tg_sideman_state *s)
{
    *s = (tg_sideman_state){.page = panel->page, .bank = panel->knob_bank, .knob = TG_SIDEMAN_NONE};
    s->mode = panel->perform_view ? 3 : panel->save_mode ? 2 : 0;
#ifdef ELITE_DRUMS808
    if (panel->page == PAGE_DILLO_SOUND) s->bank = panel->dillo_sound_bank;
    else if (panel->page == PAGE_DRUM_PRESETS) s->bank = panel->drum_preset_bank;
#ifdef ELITE_SYNTH_PRESETS
    else if (panel->page == PAGE_SYNTH_PRESETS) s->bank = panel->synth_preset_page;
#endif
    if (panel->library_open && !s->mode) s->mode = 1;
    if (panel->strip_knob) s->knob = (uint8_t)(panel->strip_knob - 1u);
#endif
    s->bpm = panel->sound.tempo;
#ifdef ELITE_DRUMS808
    s->instrument = (uint8_t)instrument_of_page(panel->page, panel);
#endif
    s->running = panel->sound.running != 0;
#ifdef ELITE_BAND_PRESETS
    if (panel->band_preset)
        for (unsigned g = 0; g < conductor_band_groups(); g++)
            if (panel->band_preset - 1u >= factory_band_groups[g].start &&
                panel->band_preset - 1u < factory_band_groups[g].start + factory_band_groups[g].count)
                s->section = (uint8_t)(g + 1u);
#endif
    s->fx_tab = panel->perform_fx;
}

static void midi_send_sideman(const struct panel *panel)
{
    if (!usb.config || usb.suspended || updating || usb.ota_req || usb.uboot_req) return;
    tg_sideman_state state;
    uint8_t message[TG_SIDEMAN_BYTES];
    sideman_fill(panel, &state);
    tg_sideman_encode(&state, TG_SIDEMAN_FW_BB37, message);
    unsigned fresh = sideman_reset_seen != usb.resets;
    if (!fresh && (!memcmp(message, sideman_sent, sizeof message) || (uint32_t)(fm1_ms - sideman_ms) < 40u)) return;
    uint32_t packets[TG_SIDEMAN_PACKETS];
    unsigned n = tg_sideman_packets(message, sizeof message, packets, TG_SIDEMAN_PACKETS);
    if (!elite_usb_burst(packets, n)) return; /* the ring is busy: next loop */
    memcpy(sideman_sent, message, sizeof message);
    sideman_reset_seen = usb.resets;
    sideman_ms = fm1_ms;
}
