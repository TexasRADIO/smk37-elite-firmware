/* SPDX-License-Identifier: GPL-3.0-only */
enum { DRUM_STAGE = VIEW_RGB(14, 14, 14), DRUM_STRIP = VIEW_RGB(30, 29, 26),
       DRUM_PLOT = VIEW_RGB(34, 32, 27) };

static const char *const drum_short[D8_NUM] = {"BD", "SD", "LT", "MT", "HT", "RS", "CP", "CB", "CY", "OH", "CH"};
/* The sixteen sounds in 8W8 lane order, which is also the voice picker's pad order. */
static const char *const drum_sound_short[DRUM_SOUNDS] = {
    "BD", "SD", "LT", "MT", "HT", "LC", "MC", "HC", "RS", "CL", "MA", "CP", "CB", "CH", "OH", "CY"};
static const char *const drum_sound_name[DRUM_SOUNDS] = {
    "BASS DRUM", "SNARE", "LOW TOM", "MID TOM", "HIGH TOM", "LOW CONGA", "MID CONGA", "HIGH CONGA",
    "RIM SHOT", "CLAVES", "MARACAS", "CLAP", "COWBELL", "CLOSED HAT", "OPEN HAT", "CYMBAL"};

/* The sixteen-sound picker: each pad shows its sound, the kit's current pick lit. */
static void view_drum_sound_pads(uint16_t *pixels, const drum_kit *kit, unsigned track)
{
    for (unsigned pad = 0; pad < DRUM_SOUNDS; pad++) {
        unsigned x = 10 + pad % 8 * 28, y = 112 + pad / 8 * 31;
        unsigned plays = drum_kit_sound(kit, drum_sound_track(pad)) == pad;
        unsigned active = plays && drum_sound_track(pad) == track;
        view_rect(pixels, x, y, 24, 24, active ? LIME : plays ? CARD : DRUM_PLOT);
        view_text(pixels, x + 4, y + 8, drum_sound_short[pad], 1, active ? PAPER : plays ? INK : DIM);
    }
}

/* Each voice drawn as its own sound: kick and toms drop in pitch, the snare adds a noise
 * band, the clap bursts three times before its tail, the cowbell beats two square tones,
 * the rimshot clicks and the metals shimmer. Tune, decay and level reshape it live. */
static void view_drum_contour(uint16_t *pixels, const drum_kit *kit, unsigned track)
{
    static const uint8_t period[D8_NUM] = {30, 11, 24, 19, 15, 6, 0, 7, 0, 0, 0};
    const int mid = 89, height = 29;
    int tune = kit->value[track][DRUM_TUNE], decay = kit->value[track][DRUM_DECAY];
    int level = kit->value[track][DRUM_LEVEL];
    int span = track == D8_RS ? 10 : track == D8_CH ? 8 + decay / 8 : track == D8_CB ? 30 + decay / 3 :
               track == D8_CY ? 90 + decay : track == D8_OH ? 40 + decay / 2 : 30 + decay * 3 / 2;
    int previous = mid;
    unsigned noise = 29u, phase = 0u, phase2 = 0u;
    int amp = level << 8;
    view_rect(pixels, 8, 57, 224, 63, DRUM_PLOT);
    view_rect(pixels, 8, mid, 224, 1, EDGE);
    for (int i = 0; i < 224; i++) {
        noise = noise * 1103515245u + 12345u;
        int hiss = (int)((noise >> 16) & 255u) - 128;
        int env = amp >> 8;
        if (track == D8_CP) { /* three claps 6 px apart, then the reverb tail */
            int burst = i < 18 ? i % 6 : i - 18;
            env = i < 18 ? level * (6 - burst) / 6 : level * 3 / 5 * span / (span + (i - 18) * 4);
        }
        amp -= amp * 3 / (span * 2);
        if (period[track]) { /* pitched */
            int p = period[track] * (192 - tune) / 128;
            if (p < 2) p = 2;
            int drop = track <= D8_HT && track != D8_SD ? (i < 24 ? (24 - i) * 64 / 24 : 0) : 0; /* pitch falls */
            phase += (unsigned)(256 * (64 + drop) / (p * 64));
            int s = view_sin64[(phase >> 2) & 63u];
            if (track == D8_CB) { /* 808 cowbell: two square tones */
                phase2 += (unsigned)(256 * 3 / (p * 2));
                s = ((phase & 255u) < 128u ? 64 : -64) + ((phase2 & 255u) < 128u ? 63 : -63);
            }
            if (track == D8_RS) s = (s + view_sin64[(phase * 3u >> 2) & 63u]) / 2;
            if (track == D8_SD) s = (s * 2 + hiss * (32 + decay / 2) / 96) / 3; /* tone + snappy */
            int y = mid - s * env / 127 * height / 127;
            view_tel_plot_segment(pixels, 8 + i, previous, y, CHROME_808_ORANGE);
            previous = y;
        } else { /* metals and clap: a noise band, bright and dim columns shimmering */
            int h = (hiss < 0 ? -hiss : hiss) * env / 127 * height / 128;
            view_rect(pixels, 8 + i, mid - h, 1, h * 2 + 1, i & 1 ? CHROME_808_ORANGE : VIEW_RGB(110, 60, 20));
        }
    }
}

/* Both drum overlays follow the same physical 11-voice pad order. */
static void view_drum_voice_pads(uint16_t *pixels, const drum_roll *roll, unsigned selected)
{
    for (unsigned pad = 0; pad < D8_NUM; pad++) {
        unsigned x = 10 + pad % 8 * 28, y = 112 + pad / 8 * 31;
        unsigned active = roll ? roll->velocity[pad] != 0 : pad == selected;
        view_rect(pixels, x, y, 24, 24, active ? LIME : CARD);
        view_text(pixels, x + 4, y + 8, drum_short[pad], 1, active ? PAPER : INK);
    }
}

static void view_drum_steps(uint16_t *pixels, const struct panel *panel, unsigned head, const char *rate)
{
    unsigned track = drum_part(panel) ? panel->drums_b_track : panel->drums_track;
    const drum_kit *kit = drum_part(panel) ? &panel->drums_b.kit : &panel->drums_kit;
    const char *name = drum_sound_name[drum_kit_sound(kit, track)];
    view_rect(pixels, 0, 18, VIEW_W, 166, DRUM_STAGE);
    if (panel->drums_voice_pick) {
        view_picnic(pixels, 8, 22, "PICK A SOUND", &picnic_M, DIM);
        jukebox_big(pixels, 8, 44, VIEW_W - 16, name, CHROME_808_ORANGE);
        view_drum_sound_pads(pixels, kit, track);
        return;
    }
    const drum_roll *roll = drum_part(panel) ? &panel->drums_b.roll : &panel->drums_roll;
    if (panel->drums_repeat) {
        unsigned latch = panel->drum_latch & (drum_part(panel) ? DRUM_LATCH_B : DRUM_LATCH_A);
        view_picnic(pixels, 8, 20, "REPEAT", &picnic_L, CHROME_808_WHITE);
        view_small(pixels, 10, 60, "K2 RATE", DIM);
        view_picnic(pixels, 10, 66, rate, &picnic_L, CHROME_808_ORANGE);
        view_small(pixels, 124, 60, "K3 SWING", DIM);
        view_label_number(pixels, 124, 70, NULL, 50u + roll->swing, DIM, CHROME_808_ORANGE);
        view_drum_voice_pads(pixels, roll, 0);
        const char *hint = panel->sound.clock.source == TG_CLOCK_USB && panel->sound.clock.state != TG_CLOCK_RUNNING ?
                           "WAITING FOR USB CLOCK" : latch ? "LATCH ON  TAP PADS" : "LATCH OFF  HOLD PADS";
        view_small(pixels, 8, 172, hint, DIM);
        const char *accent = "CHORD+PAD ACCENT";
        view_small(pixels, VIEW_W - 8 - view_picnic_width(accent, &picnic_S), 152, accent,
                   panel->held_buttons & (1u << BTN_TIE) ? CHROME_808_ORANGE : DIM);
        return;
    }
    const drum_pattern *pattern = drum_part(panel) ? &panel->drums_b.pattern : &panel->drums_pattern;
    /* The voice being edited, big; its sound as a picture; the pattern named inside it. */
    jukebox_big(pixels, 8, 20, VIEW_W - 16, name, CHROME_808_WHITE);
    view_drum_contour(pixels, kit, track);
    {
        unsigned lane = drum_part(panel) ? MIX_808_B : MIX_808;
        struct conductor_row_state state = conductor_lane_state(panel, lane);
        unsigned slot = (state.pending ? state.queued : state.active) + 1u;
        char label[12] = "PATTERN 00";
        label[8] = (char)('0' + slot / 10u); label[9] = (char)('0' + slot % 10u);
        if (state.pending) { label[0] = 'N'; label[1] = 'E'; label[2] = 'X'; label[3] = 'T'; label[4] = ' ';
                             label[5] = ' '; label[6] = ' '; label[7] = ' '; }
        view_picnic(pixels, 12, 60, label, &picnic_S, state.pending ? CHROME_808_ORANGE : DIM);
        if (!state.enabled) {
            int w = view_picnic_width("MUTE", &picnic_M) + 10;
            view_rect(pixels, VIEW_W - 12 - w, 61, w, 22, CHROME_808_ORANGE);
            view_picnic(pixels, VIEW_W - 7 - w, 63, "MUTE", &picnic_M, CHROME_808);
        }
    }
    static const uint16_t step_on[] = {
        CHROME_808_RED, CHROME_808_ORANGE, CHROME_808_YELLOW, CHROME_808_WHITE
    };
    static const uint16_t step_off[] = {
        VIEW_RGB(86, 28, 24), VIEW_RGB(94, 49, 22), VIEW_RGB(89, 72, 21), VIEW_RGB(73, 70, 63)
    };
    /* Sixteen steps as two rows of eight, laid out like the pads; a struck pad flashes. */
    for (unsigned i = 0; i < DRUM_STEPS; i++) {
        unsigned mask = 1u << i;
        unsigned hit = pattern->hits[track] & mask;
        unsigned accent = pattern->accents[track] & mask;
        int x = 8 + (int)(i % 8u) * 28, y = 124 + (int)(i / 8u) * 21;
        unsigned group = i / 4u;
        uint16_t color = hit ? step_on[group] : step_off[group];
        unsigned glow = pad_hit_glow(panel, i);
        if (glow) color = picnic_mix(color, CHROME_CREAM, glow * 3u);
        view_rect(pixels, x, y, 26, 17, color);
        if (accent && hit) view_rect(pixels, x + 4, y + 2, 18, 3, CHROME_808);
        if (i == head) view_rect(pixels, x, y + 17, 26, 2, SKY);
    }
    for (unsigned i = 0; i < D8_NUM; i++) {
        int x = 8 + i * 20;
        view_rect(pixels, x, 168, 18, 14, i == track ? CHROME_808_ORANGE : DRUM_PLOT);
        view_small(pixels, x + 3, 171, drum_sound_short[drum_kit_sound(kit, i)], i == track ? CHROME_808 : CHROME_808_WHITE);
    }
    unsigned bit = 1u << drum_part(panel);
    unsigned restore = panel->drums_restore & bit, undo = panel->drums_undo & bit;
    /* Only the live undo states stay on the stage; the combinations are in the hint row. */
    if (restore || undo) view_status_line(pixels, 98, restore ? "CHORD+< RESTORE" : "CHORD+< UNDO", LIME);

}

/* Sound kits: the kit in use named big, four big kit numbers where pads 1-4 are, and one line
 * of state. Saved kits are cream; empty ones stay dark. */
static void view_drum_kits(uint16_t *pixels, const struct panel *panel)
{
    static const char *const names[] = {"FACTORY 808", "KIT 1", "KIT 2", "KIT 3"};
    view_rect(pixels, 0, 18, VIEW_W, 166, DRUM_STAGE);
    unsigned current = drum_part(panel) ? panel->drums_b_kit_slot : panel->kit_slot;
    jukebox_big(pixels, 8, 20, VIEW_W - 16, names[current & 3u], CHROME_808_WHITE);
    for (unsigned slot = 0; slot <= DRUM_USER_KITS; slot++) {
        int x = 8 + (int)slot * 57;
        unsigned occupied = conductor_kit_exists(panel->kit_slots, slot);
        unsigned active = current == slot;
        uint16_t card = active ? CHROME_808_ORANGE : occupied ? CHROME_808_WHITE : DRUM_PLOT;
        if (pad_hit_glow(panel, slot)) card = picnic_mix(card, CHROME_CREAM, pad_hit_glow(panel, slot) * 3u);
        view_rect(pixels, x, 64, 53, 60, card);
        char digit[2] = {(char)('1' + slot), 0};
        view_picnic(pixels, x + 8, 70, digit, &picnic_L, active || occupied ? CHROME_808 : VIEW_RGB(90, 84, 76));
    }
    drum_kit saved = conductor_get_kit(current);
    const drum_kit *working = drum_part(panel) ? &panel->drums_b.kit : &panel->drums_kit;
    const char *status = panel->kit_status == KIT_EMPTY ? "EMPTY KIT SLOT" :
        panel->kit_status == KIT_CAPTURED ? "KIT CAPTURED" :
        panel->kit_status == KIT_RECALLED ? "KIT RECALLED" :
        memcmp(working, &saved, sizeof saved) ? "KIT EDITED" : NULL;
    const char *save = view_conductor_save_status(panel);
    if (!save && panel->conductor_dirty) save = "REC SAVES KITS";
    if (status) view_status_line(pixels, 136, status, CHROME_808_ORANGE);
    if (save) view_status_line(pixels, 160, save, DIM);
}

/* The preset jukebox: Studio rhythms, the 727 layer and the Band presets on one screen.
 * K1 spins title strips past a lit centre strip; INFO (PARA) turns the selected entry to what
 * it is, then to how to play it; a pad cues it and the screen counts it in to the bar. */
static void jukebox_lanes(uint16_t *pixels, const struct panel *panel, unsigned index, int y, unsigned band)
{
    static const char *const band_label[3] = {"808", "303", "101"};
    static const char *const drum_label[3] = {"LOW", "MID", "HI"};
    static const uint16_t on[3] = {CHROME_RED, CHROME_CREAM, CHROME_808_ORANGE};
    static const uint16_t hold[3] = {VIEW_RGB(122, 52, 36), VIEW_RGB(107, 98, 91), VIEW_RGB(122, 69, 33)};
    uint8_t lane[3][DRUM_STEPS];
    rhythm_lanes(panel, index, lane);
    for (unsigned r = 0; r < 3; r++) {
        int ry = y + (int)r * 11;
        view_small(pixels, 8, ry, band ? band_label[r] : drum_label[r], DIM);
        for (unsigned step = 0; step < DRUM_STEPS; step++) {
            int x = 36 + (int)step * 12 + (int)(step / 4u) * 1;
            uint16_t c = lane[r][step] == LANE_OFF ? VIEW_RGB(47, 40, 35) :
                         lane[r][step] == LANE_HOLD ? hold[r] : on[r];
            view_rect(pixels, x, ry, 10, lane[r][step] == LANE_ACCENT ? 9 : 7, c);
        }
    }
}

static void view_drum_presets(uint16_t *pixels, const struct panel *panel, unsigned beat)
{
    unsigned band = panel->rhythm_source == RHYTHM_BAND;
    uint16_t stage = band ? CHROME_BAND : DRUM_STAGE, ink = band ? CHROME_CREAM : CHROME_808_WHITE;
    uint16_t accent = band ? CHROME_RED : CHROME_808_ORANGE;
    view_rect(pixels, 0, 18, VIEW_W, 166, stage);
    unsigned selected = rhythm_selected(panel), total = rhythm_total(panel);
    if (!selected) return;
    unsigned index = selected - 1u;

    if (rhythm_cue_waiting(panel)) {
        /* Counted in: the name, four big beats, nothing else. */
        unsigned waiting = rhythm_pending(panel);
        char caps[32];
        const char *name = jukebox_caps(rhythm_name(panel, waiting ? waiting - 1u : index), caps);
        const picnic_face *face = jukebox_face(name, 232);
        view_picnic(pixels, (VIEW_W - view_picnic_width(name, face)) / 2, 26, name, face, ink);
        for (unsigned b = 0; b < 4; b++) {
            int x = 10 + (int)b * 56;
            char digit[2] = {(char)('4' - b), 0};
            unsigned past = beat < 4u && b < beat, now = beat < 4u && b == beat;
            view_rect(pixels, x, 74, 52, 52, now ? accent : past ? VIEW_RGB(58, 49, 43) : VIEW_RGB(42, 36, 32));
            view_picnic(pixels, x + 16, 82, digit, &picnic_L, now ? stage : past ? VIEW_RGB(107, 90, 78) : ink);
        }
        view_picnic(pixels, (VIEW_W - view_picnic_width("AT THE BAR", &picnic_M)) / 2, 142, "AT THE BAR", &picnic_M, accent);
        return;
    }

    if (panel->rhythm_info == JUKEBOX_LIST) {
        /* Three titles: the one before, the lit one, the one after. */
        for (int d = -1; d <= 1; d++) {
            int at = (int)selected + d;
            if (at < 1 || at > (int)total) continue;
            const char *name = rhythm_name(panel, (unsigned)at - 1u);
            if (!d) {
                uint16_t lit = CHROME_CREAM;
                for (unsigned pad = 0; pad < TG_PADS; pad++)
                    if (pad_hit_glow(panel, pad)) lit = picnic_mix(lit, CHROME_808_ORANGE, pad_hit_glow(panel, pad) * 2u);
                view_rect(pixels, 0, 46, VIEW_W, 46, lit);
                jukebox_big(pixels, 8, 51, VIEW_W - 16, name, band ? CHROME_BAND : CHROME_808);
            } else {
                int y = d < 0 ? 22 : 96;
                const picnic_face *face = view_picnic_width(name, &picnic_M) <= VIEW_W - 16 ? &picnic_M : &picnic_S;
                view_picnic(pixels, 8, y + (face == &picnic_M ? 0 : 4), name, face, DIM);
            }
        }
        jukebox_lanes(pixels, panel, index, 126, band);
        return;
    }

    /* A card leads with the big name. */
    const char *name = rhythm_name(panel, index);
    jukebox_big(pixels, 8, 22, VIEW_W - 16, name, ink);

    if (panel->rhythm_info == JUKEBOX_ABOUT) {
        const char *line[2] = {0, 0};
#ifdef ELITE_BAND_PRESETS
        char tempo[12] = "";
        if (band) {
            const struct band_preset *row = &factory_band_presets[index];
            const struct band_preset_sound *sound = &factory_band_sounds[row->sound];
            char digits[6];
            unsigned at = 0;
            jukebox_number(digits, sound->tempo);
            for (const char *c = digits; *c; c++) tempo[at++] = *c;
            for (const char *c = " BPM"; *c; c++) tempo[at++] = *c;
            tempo[at] = 0;
            line[0] = tempo;
            const char *part = sound->name, *group = rhythm_group(panel);
            while (*part && *part == *group) {
                part++;
                group++;
            }
            line[1] = *part == *group ? "ALL THREE PARTS" : sound->name;
        } else
#endif
        if (panel->rhythm_source == RHYTHM_LAYER) {
            line[0] = "727 LAYER";
#ifdef ELITE_PERCUSSION_727
            line[1] = layer_rhythm_sources[index];
#endif
        } else {
            line[0] = "STUDIO 808";
            line[1] = "INTO THE 808";
        }
        for (unsigned i = 0; i < 2; i++)
            if (line[i]) {
                const picnic_face *face = view_picnic_width(line[i], &picnic_M) <= VIEW_W - 16 ? &picnic_M : &picnic_S;
                view_picnic(pixels, 8, 74 + (int)i * 28, line[i], face, i ? DIM : accent);
            }
        jukebox_lanes(pixels, panel, index, 140, band);
        return;
    }

    /* How to play. */
#ifdef ELITE_BAND_PRESETS
    if (band) {
        struct band_notes notes = band_preset_notes(index);
        jukebox_keys(pixels, 64, 58, notes.used, notes.root, stage);
        const char *tip = !notes.used ? "DRUMS ONLY" : !notes.lead_notes ? "THE TOP IS YOURS" :
                          !notes.bass_on_one ? "THE ONE IS OPEN" : notes.lead_notes <= 4u ? "PLAY THE GAPS" : "HOLD LONG NOTES";
        jukebox_root_tip(pixels, 126, notes.used, notes.root, tip, ink, CHROME_RED);
        return;
    }
#endif
    /* A rhythm: its busiest drums, big enough to play along with. */
    const drum_pattern *pattern = rhythm_pattern(panel, index);
    static const char *const studio_name[D8_NUM] = {"BD", "SD", "LT", "MT", "HT", "RS", "CP", "CB", "CY", "OH", "CH"};
    static const char *const latin_name[D8_NUM] = {"BOM", "GUI", "CAM", "CGA", "BGH", "BGL", "MUT", "OPN", "LOW", "TBH", "TBL"};
    unsigned order[D8_NUM], count = 0;
    for (unsigned t = 0; t < D8_NUM; t++) if (pattern->hits[t]) order[count++] = t;
    for (unsigned i = 1; i < count; i++) /* busiest first */
        for (unsigned j = i; j && __builtin_popcount(pattern->hits[order[j]]) > __builtin_popcount(pattern->hits[order[j - 1u]]); j--) {
            unsigned t = order[j]; order[j] = order[j - 1u]; order[j - 1u] = t;
        }
    if (count > 5u) count = 5u;
    for (unsigned r = 0; r < count; r++) {
        unsigned t = order[r];
        int y = 64 + (int)r * 23;
        const char *label = panel->rhythm_source == RHYTHM_LAYER ? latin_name[t] : studio_name[t];
        if (panel->rhythm_source != RHYTHM_LAYER) {
            unsigned alt = drum_part(panel) ? panel->drums_b.kit.alternate : panel->drums_kit.alternate;
            static const char *const alt_name[] = {"LC", "MC", "HC", "CL", "MA"};
            unsigned bit = drum_track_alternate_bit(t);
            for (unsigned i = 0; i < 5u; i++)
                if (bit == 1u << i && (alt & bit)) label = alt_name[i];
        }
        view_picnic(pixels, 4, y, label, &picnic_M, DIM);
        for (unsigned step = 0; step < DRUM_STEPS; step++) {
            int x = 42 + (int)step * 12 + (int)(step / 4u);
            unsigned hit = pattern->hits[t] >> step & 1u, acc = pattern->accents[t] >> step & 1u;
            view_rect(pixels, x, y + 2, 10, 16, acc ? accent : hit ? ink : VIEW_RGB(47, 40, 35));
        }
    }
}

/* BT: every instrument on one screen. Each row is a pad in its lane colour; the instrument you
 * came from is marked, so a second BT goes back there. */
static void view_instruments(uint16_t *pixels, const struct panel *panel)
{
#if ELITE_DRUM_PARTS > 1
    static const char *const name[PICK_COUNT] = {"TELEGRAM", "DILLO", "808 A", "808 B", "BAND"};
    static const char *const role[PICK_COUNT] = {
        "101 LEAD ON THE KEYS", "303 BASS LINES", "808 DRUMS", "SECOND 808", "ALL FOUR TOGETHER"};
    const uint16_t lane[PICK_COUNT] = {CHROME_ELITE, CHROME_303, CHROME_808_RED, CHROME_808_YELLOW, CHROME_CREAM};
#else
    static const char *const name[PICK_COUNT] = {"TELEGRAM", "DILLO", "808", "BAND"};
    static const char *const role[PICK_COUNT] = {
        "101 LEAD ON THE KEYS", "303 BASS LINES", "808 DRUMS: 16 SOUNDS", "ALL THREE TOGETHER"};
    const uint16_t lane[PICK_COUNT] = {CHROME_ELITE, CHROME_303, CHROME_808_RED, CHROME_CREAM};
#endif
    (void)role;
    view_rect(pixels, 0, 18, VIEW_W, 166, CHROME_BAND);
    unsigned now = instrument_of_page(panel->instruments_return, panel);
    const int row = PICK_COUNT <= 4 ? 40 : 32;
    const picnic_face *face = PICK_COUNT <= 4 ? &picnic_L : &picnic_M;
    for (unsigned i = 0; i < PICK_COUNT; i++) {
        int y = 22 + (int)i * row;
        uint16_t box = lane[i];
        if (pad_hit_glow(panel, i)) box = picnic_mix(box, CHROME_CREAM, pad_hit_glow(panel, i) * 3u);
        view_rect(pixels, 8, y + 2, 32, row - 6, box);
        char digit[2] = {(char)('1' + i), 0};
        view_picnic(pixels, 24 - view_picnic_width(digit, &picnic_M) / 2, y + (row - 6) / 2 - 7, digit, &picnic_M, CHROME_BAND);
        view_picnic(pixels, 48, y + (face == &picnic_L ? -1 : 6), name[i], face, i == now ? lane[i] : CHROME_CREAM);
        if (i == now) view_rect(pixels, VIEW_W - 8, y + 2, 4, row - 6, lane[i]); /* where you came from */
    }
}

/* Holding GLOBE on its own for half a second draws every GLOBE combination for this page, so the
 * shift layer explains itself; a quick tap still opens the settings. */
enum { GLOBE_MAP_FRAMES = 312 };

static int view_globe_map_visible(const struct panel *panel)
{
    return panel->held_buttons == (1u << BTN_GLOBE) && panel->page != PAGE_GLOBAL &&
           (uint32_t)(panel->view_frames - panel->globe_frame) >= GLOBE_MAP_FRAMES;
}

static void view_globe_map(uint16_t *pixels, const struct panel *panel)
{
    const char *key[9], *does[9];
    unsigned n = 0;
#define GLOBE_ROW(k, d) do { key[n] = k; does[n] = d; n++; } while (0)
    GLOBE_ROW("KNOB BANK", panel->conductor_dillo_keys ? "KEYS TO TELEGRAM" : "KEYS TO DILLO");
    if (panel->page == PAGE_CONDUCTOR || panel->page == PAGE_SCENES) {
        GLOBE_ROW("PARA", "SETUPS");
        if (ELITE_DRUM_PARTS > 1) GLOBE_ROW("FX", drum_part(panel) ? "EDIT 808 A" : "EDIT 808 B");
    }
    if (panel->page == PAGE_DRUMS || panel->page == PAGE_KITS) GLOBE_ROW("PARA", "DRUM KITS");
    GLOBE_ROW("PAD", "PADS MIDI CHANNEL");
    GLOBE_ROW("KEYS 1-16", "KEYS MIDI CHANNEL");
    GLOBE_ROW("KEYS 17-24", "CONTROL PROFILE");
    GLOBE_ROW("KEYS 25-36", "SCALE ROOT");
    GLOBE_ROW("TOP KEY", "RESET THE SOUND");
#undef GLOBE_ROW
    view_rect(pixels, 0, 18, VIEW_W, 166, CHROME_BAND);
    view_picnic(pixels, 8, 23, "HOLD GLOBE +", &picnic_M, CHROME_CREAM);
    for (unsigned i = 0; i < n; i++) {
        int y = 46 + (int)i * 15;
        view_rect(pixels, 8, y - 2, 84, 13, CHROME_TEL);
        view_small(pixels, 12, y, key[i], CHROME_ELITE);
        view_small(pixels, 98, y, does[i], CHROME_CREAM);
    }
    view_small(pixels, 8, 174, "TAP GLOBE ALONE FOR SETTINGS", DIM);
}
