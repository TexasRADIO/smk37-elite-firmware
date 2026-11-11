/* SPDX-License-Identifier: GPL-3.0-only */
/* Redesign T1: the K1-K8 strip (y 184-219) and key hints (y 220-239) for every page
 * except Conductor Home, which draws its own. Built on view_knob_cell and view_keycap.
 * Each cell shows what that encoder does on the current page, read from the same state
 * the encoder handlers in panel101.inc write. While a modifier is held, the hint row
 * shows what that modifier unlocks here (hold-layer hints). */

struct strip_cell {
    const char *label; /* NULL: the knob does nothing on this page */
    unsigned value;    /* 0..127 */
};

static const char *const strip_control_short[CTRL_COUNT] = {
    "SAW", "PLS", "SUB", "NSE", "CUT", "RES", "ENV", "TRK", "ATK", "DEC",
    "SUS", "REL", "PW", "PWM", "LFO", "VLF", "GLD", "VCO", "DLY", "REV",
    "PBD", "FBD", "PWH", "FWH", "LVL", "BPM", "OCT", "KOC", "EX2", "VLO",
    "LKY", "IDL", "DRV", "FAT", "FDC", "FSU", "FRL", "ACT", "ACL", "PSR"
};

static struct strip_cell strip_set(const char *label, int value, int low, int high)
{
    return (struct strip_cell){label, strip_scale(value, low, high)};
}

static struct strip_cell strip_control(const tg_controls *sound, unsigned control)
{
    int value = (int)control_value(sound, control);
    if (control == CTRL_TEMPO) return strip_set("BPM", value, 30, 300);
    int high = control_switch(control) ? 1 : control_five_positions(control) ? 4 : 127;
    return strip_set(strip_control_short[control], value, 0, high);
}

static void strip_cells(const struct panel *panel, struct strip_cell cell[8])
{
    const tg_controls *sound = &panel->sound;
    for (unsigned k = 0; k < 8; k++) cell[k] = (struct strip_cell){0, 0};
    unsigned page = panel->page;

    if (page == PAGE_DILLO_SOUND) {
        const bb_dillo_patch *patch = &panel->dillo_sound.tone;
        const dillo_decay *decay = &panel->dillo_sound.decay;
        if (panel->dillo_sound_page) {
            cell[0] = strip_set("ATK", patch->attack_ms, 1, 255);
            cell[1] = strip_set("TRK", patch->tracking, 0, 100);
            cell[2] = strip_set("ADC", decay->accent, 0, DILLO_DECAY_MAX);
            cell[3] = strip_set("AMP", decay->amp, 0, DILLO_DECAY_MAX);
            cell[4] = strip_set("SWP", decay->sweep, 0, DILLO_SWEEP_COUNT - 1);
            return;
        }
        cell[0] = (struct strip_cell){"CUT", strip_hz(patch->cutoff_hz)};
        cell[1] = strip_set("RES", patch->resonance_x100, 0, 2000);
        cell[2] = strip_set("ENV", patch->envelope, 0, 100);
        cell[3] = strip_set("DEC", patch->decay, 0, 100);
        cell[4] = strip_set("ACC", patch->accent, 0, 100);
        cell[5] = strip_set("SLD", patch->slide_ms, 1, 255);
        cell[6] = strip_set("WAV", patch->wave, 0, 1);
        cell[7] = strip_set("DRV", patch->drive, 0, 100);
        return;
    }
    if (page == PAGE_DILLO) {
        dillo_step step = panel->dillo_pattern.steps[panel->dillo_cursor];
        cell[0] = strip_set("LVL", sound->master, 0, 127);
        if (panel->dillo_record >= DILLO_REC_PITCH) {
            if (panel->dillo_record == DILLO_REC_TIME) {
                unsigned gate = panel->dillo_time_edit ?
                    panel->dillo_time[panel->dillo_time_edit - 1u].gate_percent : panel->dillo_take_gate;
                cell[5] = strip_set("GATE", gate, 5, 100);
            }
            return;
        }
        unsigned note = step.event == DILLO_NOTE;
        cell[4] = strip_set("ACC", note && (step.flags & DILLO_ACCENT), 0, 1);
        if (step.event != DILLO_REST) cell[5] = strip_set("GATE", step.gate_percent, 5, 100);
        cell[6] = strip_set("SLD", note && (step.flags & DILLO_SLIDE), 0, 1);
        cell[7] = strip_set("PIT", note ? step.pitch : 36, 0, 127);
        return;
    }
    if (page == PAGE_DRUMS || (page == PAGE_CONDUCTOR && panel->drums_repeat)) {
        const drum_roll *roll = drum_part(panel) ? &panel->drums_b.roll : &panel->drums_roll;
        unsigned focused = drum_part(panel) ? 2u : 1u;
        if (panel->drums_repeat) {
            cell[1] = strip_set("RATE", roll->rate ? roll->rate : 5, 1, TG_ARP_RATE_COUNT);
            cell[2] = strip_set("SWG", roll->swing, 0, DRUM_ROLL_SWING_MAX);
            cell[3] = strip_set("LTCH", (panel->drum_latch & focused) != 0, 0, 1);
            return;
        }
        const drum_kit *kit = drum_part(panel) ? &panel->drums_b.kit : &panel->drums_kit;
        unsigned track = drum_part(panel) ? panel->drums_b_track : panel->drums_track;
        static const char *const kit_label[] = {"TUNE", "DCY", "LVL"};
        cell[0] = strip_set("VOX", (int)track, 0, D8_NUM - 1); /* K1 picks the part's voice (T3) */
        if (!panel->drums_voice_pick)
            for (unsigned k = 1; k <= DRUM_KIT_PARAMS && k < 4; k++)
                cell[k] = strip_set(kit_label[k - 1u], kit->value[track][k - 1u], 0, 127);
        return;
    }
    if (page == PAGE_MIX) {
        unsigned lane = panel->mixer_lane;
        static const char *const mix_label[MIX_CONTROLS] = {"LVL", "PAN", "DLY", "REV", "DRV"};
        for (unsigned k = 0; k < MIX_CONTROLS; k++) {
            unsigned value = lane == MIX_808_B ? panel->drums_b.mix[k] : panel->mixer.value[lane][k];
            if (lane == MIX_TELEGRAM && k >= MIX_DELAY)
                value = k == MIX_DRIVE ? sound->patch.drive :
                        sound->patch.value[k == MIX_DELAY ? TG_DELAY : TG_REVERB];
            cell[k] = strip_set(mix_label[k], (int)value, 0, 127);
        }
        cell[7] = strip_set("MSTR", sound->master, 0, 127);
        return;
    }
#ifdef ELITE_SYNTH_PRESETS
    if (page == PAGE_SYNTH_PRESETS) {
        cell[0] = strip_set("SPIN", panel->synth_preset_page, 0, synth_preset_pages(panel) - 1u); /* K1 spins */
        return;
    }
#endif
    if (page == PAGE_DRUM_PRESETS) {
        cell[0] = strip_set("SPIN", (int)rhythm_selected(panel), 1, (int)rhythm_total(panel)); /* K1 spins the jukebox */
        return;
    }
    if (page == PAGE_SCENES || page == PAGE_KITS || page == PAGE_INSTRUMENTS) return;

    /* Telegram pages: same precedence as the encoder handler. */
    if (panel->repeat_edit) {
        cell[0] = strip_set("RATE", sound->repeat_rate ? sound->repeat_rate : 5, 1, TG_ARP_RATE_COUNT);
        cell[1] = strip_set("GATE", sound->repeat_gate ? sound->repeat_gate : 75, 5, 100);
        cell[2] = strip_set("SWG", sound->repeat_swing, 0, 25);
        cell[3] = strip_set("LTCH", panel->repeat_latch, 0, 1);
        return;
    }
    if (page == PAGE_ARP) {
        cell[0] = strip_set("MODE", sound->arp, TG_ARP_OFF, TG_ARP_REPEAT);
        cell[1] = strip_set("RATE", sound->arp_rate ? sound->arp_rate : 5, 1, TG_ARP_RATE_COUNT);
        cell[2] = strip_set("GATE", sound->arp_gate ? sound->arp_gate : 75, 5, 100);
        cell[3] = strip_set("OCT", sound->arp_octaves, 0, 3);
        cell[4] = strip_set("SWG", sound->arp_swing, 0, 25);
        cell[5] = strip_set("LTCH", panel->arp_latch, 0, 1);
        if (sound->clock.source == TG_CLOCK_INTERNAL) cell[6] = strip_set("BPM", (int)tg_arp_tempo(sound), 30, 300);
        cell[7] = strip_set("CLK", sound->clock.source == TG_CLOCK_USB, 0, 1);
        return;
    }
    if (panel->save_mode) {
        cell[0] = strip_set("CHR", panel->save_name[panel->name_cursor], 32, 126);
        cell[1] = strip_set("POS", panel->name_cursor, 0, TG_NAME_BYTES - 2);
        return;
    }
    if (page == PAGE_FILTER_ENV) {
        cell[0] = strip_set("MODE", sound->patch.filter_env_mode, TG_FILTER_ENV_SHARED, TG_FILTER_ENV_CLOSE);
        cell[1] = strip_set("AMT", sound->patch.value[TG_ENV], 0, 127);
        cell[2] = strip_control(sound, CTRL_ACCENT_TONE); cell[2].label = "ACT";
        cell[3] = strip_control(sound, CTRL_ACCENT_TONE + 1u); cell[3].label = "ACL";
        return;
    }
    if (page == PAGE_GLOBAL) {
        cell[0] = strip_set("ROOT", panel->scale_root, 0, SCALE_ROOTS - 1);
        cell[1] = strip_set("SCL", panel->scale_type, 0, sizeof scale_intervals / sizeof scale_intervals[0] - 1);
        cell[2] = strip_set("KCH", panel->midi_channel, 0, MIDI_CHANNELS - 1);
        cell[3] = strip_set("PCH", panel->pad_midi_channel, 0, MIDI_CHANNELS - 1);
        cell[4] = strip_set("USB", panel->midi_tx, 0, 1);
        cell[5] = strip_set("PRE", panel->profile, 0, TG_LAYOUT_PROFILES - 1);
        cell[6] = strip_set("PED", panel->pedal_mode, PEDAL_OFF, PEDAL_HIGH);
        return;
    }
    if (page == PAGE_PATCH) {
        if (panel->library_kind == LIB_IMPORTED) {
            int count = (int)tg_imported_match_count(panel->import_category);
            cell[6] = strip_set("NAME", panel->library_slot, 0, count > 1 ? count - 1 : 1);
            cell[7] = strip_set("FAM", panel->import_category, 0, TG_IMPORT_CATEGORY_COUNT - 1);
        } else if (panel->library_kind == LIB_FACTORY && tg_imported_count()) {
            cell[7] = strip_set("BANK", 0, 0, 1);
        }
        return;
    }
    if (page == PAGE_FX) {
        for (unsigned k = 0; k < sizeof fx_map; k++) cell[k] = strip_control(sound, fx_map[k]);
        cell[3] = strip_set("GMOD", sound->patch.glide_mode, TG_GLIDE_OFF, TG_GLIDE_ON);
        cell[7] = strip_control(sound, TG_GLIDE);
        return;
    }
    for (unsigned k = 0; k < 8; k++) cell[k] = strip_control(sound, panel->knob_map[panel->knob_bank][k]);
    if (page == PAGE_STEPS) {
        cell[5] = strip_set("GATE", (int)tg_phrase_gate(sound), 5, 100);
        cell[6] = strip_set("LEN", (int)tg_pattern_length(sound), 1, TG_STEPS);
        cell[7] = panel->record ? strip_set("CUR", panel->cursor, 0, TG_STEPS - 1) :
                                  strip_set("PIT", sound->steps[panel->cursor] & 127u, 1, 127);
    } else if (page == PAGE_MANUAL && panel->fader_bank == 0) {
        cell[7] = strip_set("SUBW", sound->patch.sub_mode, 0, 2);
    } else if (page == PAGE_MANUAL && panel->fader_bank == 2) {
        cell[6] = strip_set("TRIG", sound->patch.trigger, TG_GATE_TRIG, TG_LFO_TRIG);
        cell[7] = strip_set("VCA", sound->patch.amp_gate, 0, 1);
    } else if (page == PAGE_MANUAL && panel->fader_bank == 3) {
        cell[3] = strip_set("FINE", sound->tune_cents, -TG_TUNE_LIMIT, TG_TUNE_LIMIT);
        cell[4] = strip_control(sound, TG_VIBRATO);
        cell[5] = strip_control(sound, CTRL_PWM_SOURCE);
        cell[6] = strip_set("WAVE", sound->patch.lfo_wave, TG_LFO_TRI, TG_LFO_NOISE);
        cell[7] = strip_set("RNG", sound->patch.range, TG_RANGE_16, TG_RANGE_2);
    }
}

/* key, action pairs; at most three fit the row */
struct strip_hint { const char *key, *action; };

static unsigned strip_hints(const struct panel *panel, unsigned held, struct strip_hint hint[3])
{
    unsigned page = panel->page, n = 0;
#define HINT(k, a) do { if (n < 3) hint[n++] = (struct strip_hint){k, a}; } while (0)
#ifdef ELITE_SYNTH_PRESETS
    if (page == PAGE_SYNTH_PRESETS) {
#ifdef ELITE_ACID_DUETS
        if (synth_preset_duets(panel)) {
            if (held == 9) { HINT("PAD 1-8", "KEEP SOUND"); return n; } /* CHORD */
            HINT("PAD 1-8", "DUET");
            HINT("PAD 9-16", panel->synth_preset_dillo ? "BASS" : "LEAD");
            HINT("PARA", "SOURCE");
            return n;
        }
#endif
        HINT("PAD", "LOAD"); HINT("PARA", "SOURCE"); HINT("PATCH", "BACK");
        return n;
    }
#endif
    if (held == 9) { /* CHORD */
        if (page == PAGE_STEPS) { HINT("<", "UNDO"); HINT(">", "SHIFT"); HINT("PAD", "TIE"); }
        else if (page == PAGE_DILLO) { HINT("REC", "TAKE"); HINT("PAD", "FIX"); HINT("<", "UNDO"); }
        else if (page == PAGE_DRUMS) { HINT("PAD", "ACCENT"); HINT("<", "UNDO"); HINT("REC", "CLEAR"); }
        else if (page == PAGE_SCENES) { HINT("PAD", "MIX ONLY"); }
        else if (page == PAGE_PATCH) { HINT("<", "COMPARE"); }
        else if (page == PAGE_ARP) { HINT("", "LATCH"); }
        else if (page == PAGE_DILLO_SOUND) { HINT("", "SLIDE NEW NOTES"); }
        else HINT("KEYS", "HOLD");
    } else if (held == 17) { /* REC */
        if (page == PAGE_SCENES) { HINT("PAD", "CAPTURE"); HINT("<", "REMOVE"); }
        else if (page == PAGE_KITS) { HINT("PAD", "CAPTURE"); }
        else if (page == PAGE_STEPS || page == PAGE_DILLO) { HINT("KEY", "ENTER"); }
        else HINT("PAD", "STORE");
    } else if (held == 10) { /* GLOBE */
        if (page == PAGE_DRUMS) { HINT("PARA", "KITS"); if (ELITE_DRUM_PARTS > 1) HINT("FX", "A/B"); }
        else { HINT("KNOB BANK", "KEYS"); HINT("PAD", "PAD CH"); HINT("PRE", "PROFILE"); }
    } else if (held == 15) { /* SEQ */
        HINT("PAD", "EDITOR"); HINT("KEY", "TRANSP"); HINT("PLAY", "CLOCK");
    } else if (held == 7) { /* NOTE REPEAT */
        if (page == PAGE_DILLO_SOUND || page == PAGE_DILLO) HINT("", "ACCENT NEW NOTES");
        else { HINT("PAD", "ROLL"); HINT("K4", "LATCH"); }
    } else if (held == 0) { /* KNOB BANK */
        if (page == PAGE_DRUMS) HINT("PAD 1-16", "SOUND");
        else HINT("PARA", "PWM SRC");
    } else switch (page) {
    case PAGE_TONE:
        if (panel->repeat_edit) { HINT("PAD", "ROLL"); HINT("RATE", "DIV"); break; }
        HINT("BT", ELITE_DRUM_PARTS == 1 ? "BAND" : "INSTRUMENTS");
        HINT("PARA", "MUTE"); HINT("KNOB BANK", panel->knob_bank ? "TONE" : "MOD"); break;
    case PAGE_MANUAL:
        HINT("< >", "SHAPE");
        if (panel->fader_bank == 1) HINT("PARA", "FILT ENV");
        else if (panel->fader_bank == 2) HINT("PARA", "TRIG");
        HINT("BT", "HOME"); break;
    case PAGE_FILTER_ENV: HINT("PARA", "VCF"); HINT("BT", "HOME"); break;
    case PAGE_STEPS: HINT("REC", panel->record ? "STOP" : "ENTER"); HINT("PLAY", "RUN"); HINT("< >", "PAGE"); break;
    case PAGE_ARP: HINT("CHORD", "LATCH"); HINT("ARP", "BACK"); break;
    case PAGE_FX: HINT("FX", "BACK"); HINT("BT", "HOME"); break;
    case PAGE_GLOBAL: HINT("GLOBE", "BACK"); HINT("BT", "HOME"); break;
    case PAGE_ASSIGN: HINT("< >", "DEST"); HINT("REC", "SAVE"); HINT("BT", "DONE"); break;
    case PAGE_PATCH: HINT("PAD", "LOAD"); HINT("< >", "PAGE"); HINT("REC", "COPY"); break;
    case PAGE_DILLO_SOUND: HINT(panel->dillo_sound_page ? "<" : ">", panel->dillo_sound_page ? "TONE" : "MOD");
        HINT("PARA", "BANK"); HINT("PATCH", panel->dillo_sound_home ? "HOME" : "STEPS"); break;
    case PAGE_DILLO:
        if (panel->dillo_record == DILLO_REC_PITCH) { HINT("REC", "TIME"); HINT("PARA", "CANCEL"); HINT("<", "UNDO"); }
        else if (panel->dillo_record == DILLO_REC_TIME) {
            HINT("REC", panel->dillo_take_error ? "CHECK TAKE" : "APPLY"); HINT("PARA", "CANCEL"); HINT("PAD 1-3", "N R T");
        } else if (panel->dillo_record == DILLO_REC_DIRECT) { HINT("KEY", "PITCH"); HINT("REC", "DONE"); HINT("< >", "STEP"); }
        else { HINT("REC", "ENTER"); HINT("PATCH", "SOUND"); HINT("PARA", "MUTE"); }
        break;
    case PAGE_DRUMS:
        if (panel->drums_repeat) { HINT("PAD", "ROLL"); HINT("K4", "LATCH"); HINT("RPT", "STEPS"); }
        else { HINT("< >", "VOICE"); HINT("PARA", "MUTE"); HINT("FX", "MIX"); }
        break;
    case PAGE_CONDUCTOR: HINT("PAD", "ROLL"); HINT("K4", "LATCH"); break;
    case PAGE_MIX: HINT("PAD", "LANE"); HINT("PARA", "MUTE"); HINT("FX", "BACK"); break;
    case PAGE_SCENES: HINT("PAD", "CUE"); HINT("REC", "SAVE"); HINT("BT", "BACK"); break;
    case PAGE_KITS: HINT("PAD", "RECALL"); HINT("REC", "SAVE"); HINT("BT", "BACK"); break;
    case PAGE_INSTRUMENTS: HINT(ELITE_DRUM_PARTS > 1 ? "PAD 1-5" : "PAD 1-4", "PLAY"); HINT("BT", "BACK"); break;
    case PAGE_DRUM_PRESETS: HINT("PAD", "LOAD"); HINT("< >", "BANK"); HINT("PATCH", "STEPS"); break;
    }
#undef HINT
    return n;
}

/* The first modifier held, by the panel's button ids; NONE when no modifier is down. */
enum { STRIP_HELD_NONE = 255 };
static unsigned strip_held(uint32_t buttons)
{
    static const uint8_t order[] = {9, 17, 10, 15, 7, 0}; /* CHORD REC GLOBE SEQ NOTE REPEAT KNOB BANK */
    for (unsigned i = 0; i < sizeof order; i++)
        if (buttons & (1u << order[i])) return order[i];
    return STRIP_HELD_NONE;
}

static void view_footer(uint16_t *pixels, const struct panel *panel)
{
    if (panel->page == PAGE_CONDUCTOR && !panel->drums_repeat) return; /* drawn by view_conductor */
    const struct view_livery *liv = view_livery(panel);
    struct strip_cell cell[8];
    strip_cells(panel, cell);
    unsigned hot = panel->strip_knob &&
                   (uint32_t)(panel->view_frames - panel->strip_frame) < CONTROL_FOCUS_FRAMES ?
                   panel->strip_knob : 0;
    view_rect(pixels, 0, 184, VIEW_W, 36, liv->ground);
    view_rect(pixels, 0, 184, VIEW_W, 1, liv->lamp);
    for (unsigned k = 0; k < 8; k++) {
        if (hot == k + 1u && cell[k].label) {
            /* The knob being turned: its cell fills with the accent, ink in the ground colour. */
            struct view_livery inverse = *liv;
            inverse.ink = liv->ground;
            inverse.lamp = liv->ground;
            view_rect(pixels, 30 * (int)k, 185, 30, 35, liv->accent);
            view_knob_cell(pixels, k, cell[k].label, cell[k].value, &inverse);
        } else {
            view_knob_cell(pixels, k, cell[k].label, cell[k].value, liv);
        }
    }

    view_rect(pixels, 0, 220, VIEW_W, 20, liv->ground);
    struct strip_hint hint[3];
    unsigned held = strip_held(panel->held_buttons);
#ifdef ELITE_DRUMS808
    if (library_active(panel) && held == STRIP_HELD_NONE) {
        /* LIBRARY: the shelves are the hint row. PATCH steps right (then home), BT goes back. */
        uint8_t shelf[LIBRARY_SHELVES_MAX];
        unsigned n = library_shelves(panel, shelf);
        int x = view_keycap(pixels, 5, "PATCH", "", liv) - 4, gap = 4, need = x;
        for (unsigned i = 0; i < n; i++) need += view_small_width(library_shelf_name[shelf[i]]) + 8;
        if (need + 8 > VIEW_W) gap = 2; /* four shelves: close up so every name fits */
        for (unsigned i = 0; i < n; i++) {
            const char *name = library_shelf_name[shelf[i]];
            if (shelf[i] == panel->library_shelf) {
                int w = view_small_width(name) + 2 * gap;
                view_rect(pixels, x, 223, w, 14, liv->accent);
                view_small(pixels, x + gap, 227, name, liv->ground);
                x += w + gap;
            } else x = view_small(pixels, x + gap, 227, name, liv->ink) + gap;
        }
        if (x + view_small_width("BT BACK") + 18 <= VIEW_W) view_keycap(pixels, x + 4, "BT", "BACK", liv);
        return;
    }
#endif
    unsigned n = strip_hints(panel, held, hint);
    int x = 5;
    if (held != STRIP_HELD_NONE) {
        static const char *const held_name[18] = {[0] = "KNOB BANK", [7] = "NOTE RPT", [9] = "CHORD",
                                                  [10] = "GLOBE", [15] = "SEQ", [17] = "REC"};
        x = view_keycap(pixels, x, held_name[held], "+", liv);
    }
    for (unsigned i = 0; i < n; i++) {
        int need = view_small_width(hint[i].key) + view_small_width(hint[i].action) + 13;
        if (x + need > VIEW_W) break;
        x = hint[i].key[0] ? view_keycap(pixels, x, hint[i].key, hint[i].action, liv) :
                             view_small(pixels, x, 227, hint[i].action, liv->ink) + 6;
    }
}
