/* SPDX-License-Identifier: GPL-3.0-only */

/* A struck pad flares toward white at full strength, then settles over four steps. */
static void pad_flare(unsigned glow, unsigned *r, unsigned *g, unsigned *b, unsigned *strength)
{
    if (!glow) return;
    *r += (100u - (*r > 100u ? 100u : *r)) * glow / 6u;
    *g += (100u - (*g > 100u ? 100u : *g)) * glow / 6u;
    *b += (100u - (*b > 100u ? 100u : *b)) * glow / 6u;
    *strength = 127;
}

static uint32_t pad_color(const struct panel *panel, unsigned pad, unsigned head)
{
    static const uint8_t palette[4][3] = {
        {96, 7, 3}, {96, 31, 2}, {89, 72, 2}, {68, 68, 68},
    };
    unsigned strength, r, g, b;
#ifdef ELITE_DILLO
    if (panel->page == PAGE_DILLO && panel->dillo_record == DILLO_REC_PITCH) {
        if (pad >= panel->dillo_pitch_count) return 0;
        strength = panel->dillo_pitch_replace == pad + 1u ? 127 : 48;
        r = 84; g = 96; b = 14;
    } else if (panel->page == PAGE_DILLO && panel->dillo_record == DILLO_REC_TIME) {
        if (panel->dillo_time_pick) {
            if (pad >= panel->dillo_time_count) return 0;
            unsigned selected = panel->dillo_time_edit == pad + 1u;
            strength = selected ? 127 : 42;
            r = selected ? 84 : 8; g = 96; b = selected ? 14 : 96;
        } else {
            if (pad > 4) return 0;
            unsigned flags = panel->dillo_time_edit ? panel->dillo_time[panel->dillo_time_edit - 1u].flags :
                                                     panel->dillo_take_flags;
            strength = pad < 3 || flags & (1u << (pad - 3u)) ? 100 : 24;
            r = pad == 1 || pad == 3 ? 96 : 8;
            g = pad == 0 || pad == 1 ? 94 : 20;
            b = pad == 2 || pad == 4 ? 96 : 8;
        }
    } else if (panel->page == PAGE_DILLO) {
        const dillo_step *step = &panel->dillo_pattern.steps[pad];
        strength = pad == head || pad == panel->dillo_cursor ? 127 : step->event == DILLO_REST ? 12 : 76;
        r = pad == head ? 3 : 96;
        g = pad == head ? 100 : step->flags & DILLO_ACCENT ? 20 : 55;
        b = pad == head || step->event == DILLO_TIE || step->flags & DILLO_SLIDE ? 100 : 8;
    } else
#endif
#ifdef ELITE_DRUMS808
    if (panel->page == PAGE_DILLO_SOUND) {
        if (pad >= DILLO_SOUND_PAGE_SLOTS) return 0;
        unsigned slot = pad + panel->dillo_sound_bank * DILLO_SOUND_PAGE_SLOTS;
        if (slot >= dillo_sound_total()) return 0;
        unsigned active = panel->dillo_sound_slot == slot;
        strength = active ? 127 : conductor_dillo_sound_exists(panel->dillo_sound_slots, slot) ? 58 : 12;
        r = active ? 10 : 90; g = active ? 94 : 45;
        b = slot >= BB_DILLO_PATCHES && slot < DILLO_SOUND_PAGE_SLOTS ? 75 : 8;
#ifdef ELITE_SYNTH_PRESETS
    } else if (panel->page == PAGE_SYNTH_PRESETS && synth_preset_duets(panel)) {
#ifdef ELITE_ACID_DUETS
        /* Duet row in cream, this synth's own row in its colour; the last pick at full. */
        unsigned index = duet_at(panel, pad);
        if (!index) return 0;
        strength = index == panel->synth_duet_selected ? 127 : 44;
        if (pad < DUET_PADS) { r = 95; g = 92; b = 84; }
        else if (panel->synth_preset_dillo) { r = 84; g = 81; b = 77; }
        else { r = 100; g = 50; b = 15; }
#endif
    } else if (panel->page == PAGE_SYNTH_PRESETS) {
        unsigned index = synth_preset_at(panel, pad);
        if (!index) return 0;
        strength = index == panel->synth_preset_selected ? 127 : 48;
        r = panel->synth_preset_dillo ? 84 : 100;
        g = panel->synth_preset_dillo ? 81 : 50;
        b = panel->synth_preset_dillo ? 77 : 15;
#endif
    } else if (panel->page == PAGE_INSTRUMENTS) {
        if (pad >= PICK_COUNT) return 0;
        /* Lane colours: Telegram orange, Dillo silver, 808 A red, 808 B yellow, Band cream. */
        static const uint8_t rgb[PICK_COUNT][3] = {
            [PICK_TELEGRAM] = {100, 50, 15}, [PICK_DILLO] = {84, 81, 77}, [PICK_808_A] = {100, 25, 18},
#if ELITE_DRUM_PARTS > 1
            [PICK_808_B] = {95, 76, 19},
#endif
            [PICK_BAND] = {95, 92, 84}};
        strength = instrument_of_page(panel->instruments_return, panel) == pad ? 127 : 48;
        r = rgb[pad][0]; g = rgb[pad][1]; b = rgb[pad][2];
    } else if (panel->page == PAGE_DRUM_PRESETS) {
        unsigned index = rhythm_at(panel, pad);
        if (!index) return 0;
        /* TR-808 step colours by group of four (Band in cream); the playing entry at full strength. */
        static const uint8_t rgb[5][3] = {{100, 14, 8}, {100, 45, 6}, {100, 82, 10}, {96, 92, 80}, {98, 90, 74}};
        unsigned row = panel->rhythm_source == RHYTHM_BAND ? 4u : pad / 4u;
        strength = rhythm_playing(panel) == index ? 127 : 40;
        if (rhythm_pending(panel) == index) strength = 90;
        r = rgb[row][0]; g = rgb[row][1]; b = rgb[row][2];
    } else if (panel->page == PAGE_KITS) {
        if (pad > DRUM_USER_KITS) return 0;
        unsigned active = (drum_part(panel) ? panel->drums_b_kit_slot : panel->kit_slot) == pad;
        strength = active ? 127 : conductor_kit_exists(panel->kit_slots, pad) ? 58 : 12;
        r = active ? 10 : 95; g = active ? 94 : 47; b = 9;
    } else if (panel->page == PAGE_SCENES) {
        if (panel->scene_cleanup) {
            if (pad >= CONDUCTOR_SCENES || !(panel->scene_slots & (1u << pad))) return 0;
            strength = panel->scene_cleanup == SCENE_CLEANUP_CONFIRM + pad ? 127 : 44;
            r = 96; g = 44; b = 5;
        } else if (pad >= CONDUCTOR_SCENES) {
            if (panel->scene_slots & (1u << (pad - CONDUCTOR_SCENES))) return 0;
            strength = panel->conductor_rec_pending ? 90 : 12;
            r = 8; g = 64; b = 96;
        } else {
            unsigned occupied = panel->scene_slots & (1u << pad);
            unsigned active = panel->scene_active == pad + 1u;
            if (conductor_queued_scene(panel) == pad + 1u) {
                strength = 127; r = 95; g = 64; b = 4;
            } else {
                strength = active ? 127 : panel->scene_cursor == pad ? 72 : occupied ? 44 : 8;
                r = active ? 13 : 72;
                g = active ? 95 : 48;
                b = 64;
            }
        }
    } else if (panel->page == PAGE_MIX) {
        if (pad >= BAND_LANES) return 0;
        unsigned lane = conductor_lane_order[pad];
        unsigned selected = panel->mixer_lane == lane;
        strength = selected ? 127 : conductor_pattern_enabled(panel, lane) ? 54 : 22;
        r = selected ? 84 : 12; g = 96; b = 14;
    } else if (panel->drums_repeat) {
        if (pad >= D8_NUM) return 0;
        const drum_roll *roll = drum_part(panel) ? &panel->drums_b.roll : &panel->drums_roll;
        strength = roll->velocity[pad] ? 127 : 30;
        r = 16; g = 88; b = 48;
    } else if (panel->page == PAGE_CONDUCTOR && panel->draft_view) {
        if (pad >= BAND_LANES || !panel->draft_slot[conductor_lane_order[pad]]) return 0;
        strength = 100; r = 8; g = 66; b = 96;
    } else if (panel->page == PAGE_CONDUCTOR && panel->conductor_seq_pending) {
        unsigned lane = band_group_lane(pad);
        strength = panel->conductor_seq_pending == band_lane_editor(lane) ? 127 : 40;
        r = band_lane_rgb[lane][0]; g = band_lane_rgb[lane][1]; b = band_lane_rgb[lane][2];
    } else if (panel->page == PAGE_CONDUCTOR) {
        /* Pad groups in the lane colours: playing full, next bar white, stored dim. */
        unsigned lane = band_pad_lane(panel, pad);
        unsigned slot = band_pad_slot(panel, pad);
        struct conductor_row_state state = conductor_lane_state(panel, lane);
        unsigned telegram = lane == MIX_TELEGRAM;
        r = band_lane_rgb[lane][0]; g = band_lane_rgb[lane][1]; b = band_lane_rgb[lane][2];
        if (state.pending && !(telegram && panel->telegram_library) && slot == state.queued) {
            strength = 127; r = 96; g = 96; b = 96;
        } else if (state.enabled && !(telegram && panel->telegram_library_active) && slot == state.active) {
            strength = 127;
        } else {
            strength = state.occupied & (1u << slot) ? 40 : 6;
        }
    } else if (panel->page == PAGE_DRUMS && panel->drums_voice_pick) {
        /* Sixteen sounds: the chosen one full, the kit's other picks lit, switched-out sounds dim. */
        unsigned track = drum_part(panel) ? panel->drums_b_track : panel->drums_track;
        const drum_kit *kit = drum_part(panel) ? &panel->drums_b.kit : &panel->drums_kit;
        unsigned plays = drum_kit_sound(kit, drum_sound_track(pad)) == pad;
        strength = plays && drum_sound_track(pad) == track ? 127 : plays ? 36 : 8;
        r = 86; g = 92; b = 13;
    } else if (panel->page == PAGE_DRUMS) {
        unsigned mask = 1u << pad;
        unsigned track = drum_part(panel) ? panel->drums_b_track : panel->drums_track;
        const drum_pattern *pattern = drum_part(panel) ? &panel->drums_b.pattern : &panel->drums_pattern;
        unsigned hit = pattern->hits[track] & mask;
        unsigned accent = pattern->accents[track] & mask;
        strength = pad == head ? 127 : hit ? 86 : 12;
        r = pad == head ? 3 : accent ? 96 : palette[pad / 4][0];
        g = pad == head ? 100 : accent ? 4 : palette[pad / 4][1];
        b = pad == head ? 100 : palette[pad / 4][2];
    } else
#endif
    if (panel->page == PAGE_PATCH) {
        unsigned slot = panel->preset_bank * TG_PADS + pad;
        unsigned selected = library_selected(panel, slot);
        unsigned bank = panel->preset_bank % 4u;
        strength = selected ? 127 : library_occupied(panel, slot) ? 40 : 7;
        r = palette[bank][0];
        g = palette[bank][1];
        b = palette[bank][2];
    } else if (panel->page == PAGE_ASSIGN ||
               ((panel->pad_keys || panel->repeat_on) && performance_page(panel->page))) {
        unsigned selected = panel->page == PAGE_ASSIGN ?
            panel->assign_source == MAP_PAD && panel->assign_slot == pad :
            (panel->pads_down & (1u << pad)) != 0 || panel->repeat_latched == pad + 1;
        strength = selected ? 127 : 25;
        r = panel->repeat_on ? 68 : 35; g = panel->repeat_on ? 23 : 66; b = 100;
    } else if (panel->page == PAGE_ARP) {
        return 0;
    } else if (panel->page == PAGE_TONE) {
        unsigned selected = !panel->user_sound && !panel->imported_sound &&
                            panel->preset % TG_PADS == pad;
        strength = selected ? 127 : 40;
        unsigned bank = panel->preset / TG_PADS;
        r = palette[bank][0];
        g = palette[bank][1];
        b = palette[bank][2];
    } else {
        unsigned step = step_page_base(panel, head) + pad;
        if (step >= TG_STEPS)
            return 0;
        if (step == head) {
            strength = 127;
            r = 3; g = 100; b = 103;
        } else {
            unsigned group = pad / 4;
            if (panel->page == PAGE_STEPS && panel->cursor == step)
                strength = 127;
            else if (step >= tg_pattern_length(&panel->sound))
                strength = 7;
            else
                strength = panel->sound.steps[step] & 127u ? 92 : 20;
            r = palette[group][0];
            g = palette[group][1];
            b = palette[group][2];
        }
    }
#ifndef ELITE_PANEL_EXACT_LIGHTS /* the panel tests check settled colours */
    pad_flare(pad_hit_glow(panel, pad), &r, &g, &b, &strength);
#endif
    unsigned light = strength * panel->pad_light;
    return ((r * light / (127u * 127u)) << 16) |
           ((g * light / (127u * 127u)) << 8) |
           (b * light / (127u * 127u));
}
