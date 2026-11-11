/* SPDX-License-Identifier: GPL-3.0-only */
static unsigned view_conductor_queue(uint16_t *pixels, const struct panel *panel, unsigned y)
{
    if (!conductor_scene_pending(panel)) {
        if (panel->cancel_cue < CANCEL_CUE_DONE) return 0;
        view_text(pixels, 10, y, panel->cancel_cue == CANCEL_CUE_DONE ? "CUE CANCELED" : "NO QUEUED CUE", 1, SKY);
        return 1;
    }
    unsigned scene = conductor_queued_scene(panel);
    unsigned running = conductor_running(panel);
    unsigned lost = conductor_clock_lost(panel);
    if (scene) {
        view_text(pixels, 10, y, "SETUP", 1, CORAL);
        view_num(pixels, 58, y, scene, 2, CORAL);
        view_text(pixels, 85, y, lost ? "CLOCK LOST" : running ? "NEXT BAR" : "LOADING", 1, CORAL);
    } else {
        view_text(pixels, 10, y, lost ? "PARTS WAIT FOR CLOCK" :
                  running ? "PARTS NEXT BAR" : "PARTS LOADING", 1, CORAL);
    }
    return 1;
}

enum {
    BAND_STAGE = VIEW_RGB(31, 27, 24), BAND_CARD = VIEW_RGB(38, 33, 29),
    BAND_GRID = VIEW_RGB(70, 57, 47), BAND_MUTED = VIEW_RGB(166, 149, 134)
};

static const uint16_t band_lane_color[MIX_AUDIO_LANES] = {
    [MIX_TELEGRAM] = CHROME_BLUE, [MIX_DILLO] = CHROME_303,
    [MIX_808] = CHROME_808_RED, [MIX_808_B] = CHROME_808_YELLOW
};
static const char *const band_lane_name[MIX_AUDIO_LANES] = {
    [MIX_TELEGRAM] = "TEL", [MIX_DILLO] = "DILLO", [MIX_808] = ELITE_DRUM_PARTS > 1 ? "808 A" : "808", [MIX_808_B] = "808 B"
};
/* Each Band row's pads, in row order. */
#if ELITE_DRUM_PARTS > 1
static const char *const band_row_pads[BAND_LANES] = {"1-4", "5-8", "9-12", "13-16"};
#else
static const char *const band_row_pads[BAND_LANES] = {"1-8", "9-12", "13-16"};
#endif

static unsigned view_scene_lane_slot(const conductor_scene *scene, unsigned lane)
{
    switch (lane) {
    case MIX_808: return scene->setup[CONDUCTOR_A_SLOT] + 1u;
    case MIX_808_B: return scene->drums_b[CONDUCTOR_B_SLOT] + 1u;
    case MIX_DILLO: return scene->setup[CONDUCTOR_DILLO_SLOT] + 1u;
    default: return scene->setup[CONDUCTOR_TELEGRAM_SLOT] + 1u;
    }
}

static void view_scene_cleanup(uint16_t *pixels, const struct panel *panel)
{
    view_picnic(pixels, 8, 20, "REMOVE", &picnic_L, CHROME_CREAM);
    if (panel->scene_cleanup == SCENE_CLEANUP_PICK) {
        const char *prompt = panel->scene_status == SCENE_REMOVE_IN_USE ? "CHOOSE AN INACTIVE SETUP" :
                             panel->scene_status == SCENE_EMPTY ? "EMPTY SETUP" : "TAP A TOP PAD TO REVIEW";
        view_small(pixels, 8, 60, prompt, CORAL);
        for (unsigned slot = 0; slot < CONDUCTOR_SCENES; slot++) {
            int x = 8 + 28 * slot;
            uint16_t color = panel->scene_slots & (1u << slot) ? CHROME_CREAM : BAND_MUTED;
            view_rect(pixels, x, 82, 24, 29, BAND_CARD);
            view_small_num(pixels, x + 9, 93, slot + 1u, 1, color);
        }
        view_small(pixels, 8, 171, "BT CANCEL", BAND_MUTED);
        return;
    }
    unsigned slot = panel->scene_cleanup - SCENE_CLEANUP_CONFIRM;
    const conductor_scene *scene = &conductor_scenes[slot];
    const conductor_assets *release = &panel->scene_cleanup_assets;
    int x = view_small(pixels, 8, 46, "FREE PATTERN SLOTS FROM SETUP", CORAL) + 6;
    view_small_num(pixels, x, 46, slot + 1u, 1, CHROME_CREAM);
    for (unsigned i = 0; i < BAND_LANES; i++) {
        unsigned lane = conductor_lane_order[i];
        x = 8 + i * 56;
        view_rect(pixels, x, 60, 52, 44, BAND_CARD);
        view_small(pixels, x + 4, 66, band_lane_name[lane], band_lane_color[lane]);
        if (release->pattern[lane]) view_num(pixels, x + 4, 82, view_scene_lane_slot(scene, lane), 2, INK);
        else view_small(pixels, x + 4, 88, "--", BAND_MUTED);
    }
    view_small(pixels, 8, 118, "FREE KITS", BAND_MUTED);
    x = 8;
    for (unsigned kit = 1; kit <= DRUM_USER_KITS; kit++) {
        if (release->kits & (1u << kit))
            x = view_small_num(pixels, x, 131, kit, 1, CHROME_CREAM) + 8;
    }
    if (x == 8) view_small(pixels, x, 131, "--", BAND_MUTED);
    view_small(pixels, 107, 118, "FREE DILLO SOUND", BAND_MUTED);
    if (release->sounds) view_small_num(pixels, 107, 131, scene->dillo_sound_slot + 1u, 2, CHROME_CREAM);
    else view_small(pixels, 107, 131, "--", BAND_MUTED);
    view_small(pixels, 8, 157, "REC REMOVE SETUP + SLOTS", CORAL);
    view_small(pixels, 8, 171, "BT CANCEL", BAND_MUTED);
}

/* A setup is a big number where its pad is. A saved one lights with three lane bars and their
 * pattern numbers; an empty one stays dark. Cued, active and cursor show as the border. */
static void view_scene_card(uint16_t *pixels, const struct panel *panel, unsigned slot, unsigned queued)
{
    int x = 5 + slot % 4u * 59u, y = 22 + slot / 4u * 62u;
    unsigned occupied = panel->scene_slots & (1u << slot);
    uint16_t border = queued == slot + 1u ? CORAL : panel->scene_active == slot + 1u ? VIEW_PICKED_UP :
                      panel->scene_cursor == slot ? SKY : occupied ? BAND_GRID : BAND_STAGE;
    uint16_t card = occupied ? CHROME_CREAM : VIEW_RGB(38, 32, 25);
    unsigned glow = pad_hit_glow(panel, slot);
    if (glow) card = picnic_mix(card, CHROME_ELITE, glow * 2u);
    view_rect(pixels, x, y, 54, 56, border);
    view_rect(pixels, x + 2, y + 2, 50, 52, card);
    char digit[2] = {(char)('1' + slot), 0};
    view_picnic(pixels, x + 7, y + 4, digit, &picnic_L, occupied ? CHROME_BAND : VIEW_RGB(90, 76, 64));
    if (queued == slot + 1u) view_picnic(pixels, x + 34, y + 8, ">", &picnic_M, CORAL);
    if (!occupied) return;
    const conductor_scene *scene = &conductor_scenes[slot];
    for (unsigned i = 0; i < BAND_LANES; i++) {
        unsigned lane = conductor_lane_order[i];
        int row = y + 36 + 5 * (int)i;
        unsigned enabled = lane != MIX_808_B || (scene->drums_b[0] & B_ENABLED);
        view_rect(pixels, x + 6, row, 42, 4, enabled ? band_lane_color[lane] : BAND_MUTED);
    }
}

static void view_conductor_scenes(uint16_t *pixels, const struct panel *panel)
{
    view_rect(pixels, 0, 18, VIEW_W, 166, BAND_STAGE);
    if (panel->scene_cleanup) {
        view_scene_cleanup(pixels, panel);
        return;
    }
    unsigned cue = conductor_cues_at_bar(panel);
    unsigned queued = conductor_queued_scene(panel);
    for (unsigned slot = 0; slot < CONDUCTOR_SCENES; slot++)
        view_scene_card(pixels, panel, slot, queued);
    const char *status = view_conductor_save_status(panel);
    if (!status) status =
        panel->conductor_copy_needed ? view_copy_prompt(panel) :
        panel->scene_status == SCENE_SAVE_B_KIT ? "CAPTURE EDITED 808 B KIT" :
        panel->scene_status == SCENE_SAVE_DILLO ? "CAPTURE DILLO SOUND FIRST" :
        panel->scene_status == SCENE_SAVE_KIT ? "CAPTURE EDITED 808 KIT FIRST" :
        panel->scene_status == SCENE_OCCUPIED ? "CHOOSE A FREE SETUP" :
        panel->scene_status == SCENE_PATTERN_FULL ? "NO FREE PATTERN SLOT" :
        panel->scene_status == SCENE_KITS_FULL ? "NO FREE 808 KIT SLOT" :
        panel->scene_status == SCENE_SOUNDS_FULL ? "NO FREE DILLO SOUND SLOT" :
        panel->scene_status == SCENE_REMOVE_STOP ? "STOP TO REMOVE SETUP" :
        panel->scene_status == SCENE_REMOVE_BUSY ? "WAIT FOR SAVE TO FINISH" :
        panel->scene_status == SCENE_REMOVE_IN_USE ? "CHOOSE AN INACTIVE SETUP" :
        panel->scene_status == SCENE_REMOVE_CHANGED ? "SLOTS CHANGED  TRY AGAIN" :
        panel->scene_status == SCENE_CUED && !conductor_scene_pending(panel) ?
            conductor_running(panel) ? "PATTERNS LAUNCHED" : "PATTERNS READY" :
        panel->scene_status == SCENE_MIX_RECALLED ? "MIX RECALLED" :
        panel->scene_status == SCENE_RECALLED ? "SETUP RECALLED" :
        panel->scene_status == SCENE_VARIATION && panel->conductor_dirty ? "VARIATION CAPTURED" :
        panel->scene_status == SCENE_REMOVED && panel->conductor_dirty ? "REMOVED  TAP REC TO SAVE" :
        panel->scene_status == SCENE_CAPTURED && panel->conductor_dirty ? "CAPTURED  TAP REC TO SAVE" :
        panel->library_status == LIB_SAVED ? "SETUPS SAVED" :
        panel->scene_active ? "REC+LOWER: NEW VARIATION" : NULL;
    if (status) view_status_line(pixels, 148, status, CHROME_CREAM);
    if (conductor_scene_pending(panel)) {
        char line[24] = "SETUP 0 AT THE BAR";
        if (queued) line[6] = (char)('0' + queued);
        view_status_line(pixels, 166, conductor_clock_lost(panel) ? "WAITS FOR CLOCK" : !queued ? "PARTS AT THE BAR" :
                         cue ? line : "LOADING", CORAL);
    } else if (panel->cancel_cue >= CANCEL_CUE_DONE) {
        view_status_line(pixels, 166, panel->cancel_cue == CANCEL_CUE_DONE ? "CUE CANCELED" : "NO QUEUED CUE", SKY);
    } else if (!cue && (panel->scene_slots & (1u << panel->scene_cursor))) {
        const uint8_t *setup = conductor_scenes[panel->scene_cursor].setup;
        int x = view_picnic(pixels, 8, 166, "BPM", &picnic_M, BAND_MUTED) + 6;
        view_small_num(pixels, x, 170, conductor_get16(&setup), 3, CHROME_CREAM);
    }
}

/* Mixer: one tall card per lane, its level as the big number, the fader under it; pan and
 * sends as thin bars. A muted lane dims; a struck lane pad flashes its card. */
static void view_conductor_mix(uint16_t *pixels, const struct panel *panel)
{
    static const char *const short_name[MIX_AUDIO_LANES] = {
        [MIX_808] = ELITE_DRUM_PARTS > 1 ? "808A" : "808", [MIX_808_B] = "808B", [MIX_DILLO] = "303", [MIX_TELEGRAM] = "101"};
    conductor_mixer mixer = conductor_mixer_controls(panel);
    view_rect(pixels, 0, 18, VIEW_W, 166, BAND_STAGE);
    const int width = (VIEW_W - 8) / BAND_LANES - 4;
    for (unsigned position = 0; position < BAND_LANES; position++) {
        unsigned lane = conductor_lane_order[position];
        const uint8_t *v = lane == MIX_808_B ? panel->drums_b.mix : mixer.value[lane];
        int x = 4 + (width + 4) * (int)position;
        unsigned selected = lane == panel->mixer_lane;
        unsigned caught = fader_caught & (1u << position);
        unsigned enabled = conductor_pattern_enabled(panel, lane);
        uint16_t color = band_lane_color[lane];
        uint16_t card = selected ? BAND_GRID : BAND_CARD;
        if (pad_hit_glow(panel, position)) card = picnic_mix(card, color, pad_hit_glow(panel, position) * 2u);
        view_rect(pixels, x, 22, width, 150, card);
        view_rect(pixels, x, 22, width, 3, enabled ? color : BAND_MUTED);
        view_picnic(pixels, x + 5, 27, short_name[lane], &picnic_M, enabled ? color : BAND_MUTED);
        if (!enabled) view_picnic(pixels, x + width - 4 - view_picnic_width("MUTE", &picnic_S), 31, "MUTE", &picnic_S, CORAL);
        char level[4];
        unsigned n = v[MIX_LEVEL];
        level[0] = (char)('0' + n / 100u); level[1] = (char)('0' + n / 10u % 10u); level[2] = (char)('0' + n % 10u); level[3] = 0;
        const char *shown = n >= 100u ? level : n >= 10u ? level + 1 : level + 2;
        view_picnic(pixels, x + 5, 44, shown, &picnic_L, caught ? VIEW_PICKED_UP : enabled ? INK : BAND_MUTED);
        /* Pan as a pointer on a line; delay, reverb and drive as three thin bars. */
        int bar = width - 10;
        view_rect(pixels, x + 5, 86, bar, 1, BAND_MUTED);
        view_rect(pixels, x + 5 + bar / 2, 83, 1, 7, BAND_MUTED);
        view_rect(pixels, x + 4 + (bar - 3) * v[MIX_PAN] / 127, 84, 3, 5, CHROME_CREAM);
        view_rect(pixels, x + 5, 95, bar, 3, BAND_STAGE);
        view_rect(pixels, x + 5, 95, bar * v[MIX_DELAY] / 127, 3, color);
        view_rect(pixels, x + 5, 101, bar, 3, BAND_STAGE);
        view_rect(pixels, x + 5, 101, bar * v[MIX_ROOM] / 127, 3, color);
        view_rect(pixels, x + 5, 107, bar * v[MIX_DRIVE] / 127, 3, CORAL);
        view_pickup_fader(pixels, x + width / 2, 164, v[MIX_LEVEL], last_fader[position], caught, BAND_STAGE);
    }
    int x = view_picnic(pixels, 8, 176 - 2, "MAIN", &picnic_S, BAND_MUTED) + 6;
    view_small_num(pixels, x, 174, panel->sound.master, 3, CHROME_CREAM);
}

static void view_conductor_drafts(uint16_t *pixels, const struct panel *panel)
{
    view_picnic(pixels, 8, 20, "LAST EDITS", &picnic_L, SKY);
    const int w = (VIEW_W - 16) / BAND_LANES - 6;
    for (unsigned i = 0; i < BAND_LANES; i++) {
        int x = 8 + (int)i * (w + 6);
        unsigned source = panel->draft_slot[conductor_lane_order[i]];
        unsigned color = source ? PAPER : DIM;
        if (pad_hit_glow(panel, i)) view_rect(pixels, x - 2, 62, w + 4, 66, CHROME_CREAM);
        view_rect(pixels, x, 64, w, 62, source ? SKY : CARD);
        view_picnic(pixels, x + 5, 68, band_lane_name[conductor_lane_order[i]], &picnic_M, color);
        if (source) view_label_number(pixels, x + 5, 92, NULL, source, color, color);
        else view_picnic(pixels, x + 5, 96, "--", &picnic_M, color);
    }
    view_status_line(pixels, 134, ELITE_DRUM_PARTS > 1 ? "PAD 1-4 RESTORE" : "PAD 1-3 RESTORE", INK);
    view_small(pixels, 8, 154, "SESSION ONLY: COPY TO SAVE", DIM);
    view_conductor_queue(pixels, panel, 168);
}

/* The playhead can advance without repainting the lane previews. */
enum { CONDUCTOR_BAR_STRIP = 14 * VIEW_STRIP };

static void view_conductor_bar(uint16_t *pixels, const struct panel *panel, unsigned head)
{
    unsigned playing = view_header_beat(panel, head) != HEADER_STOPPED;
    for (unsigned step = 0; step < DRUM_STEPS; step++) {
        unsigned beat = step % 4u == 0;
        unsigned current = playing && step == head;
        view_rect(pixels, 102 + 7u * step, CONDUCTOR_BAR_STRIP + 2, 6, 4,
                  current ? beat ? CHROME_ELITE : CHROME_CREAM : beat ? EDGE : CARD); /* warm: orange on the beat, cream between */
    }
}

static unsigned view_conductor_step(const struct panel *panel, unsigned lane, unsigned step)
{
    if (lane == MIX_TELEGRAM) {
        if (step >= tg_pattern_length(&panel->sound)) return 0;
        unsigned entry = panel->sound.steps[step];
        unsigned pitch = entry & 127u;
        return pitch ? 5 + pitch / 7u : entry & TG_TIE ? 4 : 0;
    }
    if (lane == MIX_DILLO) {
        if (step >= panel->dillo_pattern.length) return 0;
        dillo_step event = panel->dillo_pattern.steps[step];
        if (event.event == DILLO_TIE) return 4;
        if (event.event != DILLO_NOTE) return 0;
        return 5 + event.pitch / 8u + (event.flags & DILLO_ACCENT ? 4 : 0);
    }
    const drum_pattern *pattern = lane == MIX_808 ? &panel->drums_pattern : &panel->drums_b.pattern;
    unsigned mask = 1u << step;
    unsigned height = 0;
    for (unsigned voice = 0; voice < D8_NUM; voice++) {
        if (!(pattern->hits[voice] & mask)) continue;
        unsigned hit = voice == D8_BD ? 20 : voice == D8_SD || voice == D8_CP ? 14 : 7;
        hit += pattern->accents[voice] & mask ? 4 : 0;
        if (hit > height) height = hit;
    }
    return height;
}

enum { CONDUCTOR_LANE_Y = 20, CONDUCTOR_LANE_HEIGHT = 37, CONDUCTOR_PIPS_X = 219, CONDUCTOR_PIPS_Y = 22 };

static void view_conductor_cue_pips(uint16_t *pixels, unsigned row, unsigned beat)
{
    unsigned y = CONDUCTOR_LANE_Y + row * CONDUCTOR_LANE_HEIGHT + CONDUCTOR_PIPS_Y;
    uint16_t color = band_lane_color[conductor_lane_order[row]];
    for (unsigned pip = 0; pip < HEADER_STOPPED; pip++)
        view_rect(pixels, CONDUCTOR_PIPS_X + pip * 5u, y, 3, 3, pip == beat ? color : BAND_GRID);
}

static void view_conductor_cue_motion(uint16_t *pixels, const struct panel *panel, unsigned beat)
{
    for (unsigned row = 0; row < BAND_LANES; row++)
        if (conductor_lane_state(panel, conductor_lane_order[row]).pending)
            view_conductor_cue_pips(pixels, row, beat);
}

/* A Band lane: its name, its pattern number big, its steps. The lane holding the keys has a
 * cream bar; a muted lane dims and says MUTE once; a struck pad flashes its lane. */
static void view_conductor_lane(uint16_t *pixels, const struct panel *panel, unsigned row, unsigned beat)
{
    unsigned lane = conductor_lane_order[row];
    struct conductor_row_state state = conductor_lane_state(panel, lane);
    unsigned keys = lane == (panel->conductor_dillo_keys ? MIX_DILLO : MIX_TELEGRAM);
    unsigned y = CONDUCTOR_LANE_Y + row * CONDUCTOR_LANE_HEIGHT;
    uint16_t color = band_lane_color[lane];
    uint16_t background = row & 1u ? VIEW_RGB(38, 33, 29) : VIEW_RGB(31, 27, 24);
    unsigned glow = 0;
    for (unsigned pad = 0; pad < TG_PADS; pad++)
        if (band_pad_lane(panel, pad) == lane && pad_hit_glow(panel, pad)) glow = pad_hit_glow(panel, pad);
    if (glow) background = picnic_mix(background, color, glow * 2u);
    view_rect(pixels, 0, y, VIEW_W, 35, background);
    view_rect(pixels, 0, y, 6, 35, keys ? CHROME_CREAM : state.enabled ? color : EDGE);
    static const char *const lane_short[MIX_AUDIO_LANES] = {
        [MIX_808] = ELITE_DRUM_PARTS > 1 ? "808A" : "808", [MIX_808_B] = "808B", [MIX_DILLO] = "303", [MIX_TELEGRAM] = "101"};
    view_picnic(pixels, 11, y + 1, lane_short[lane], &picnic_M, state.enabled ? color : DIM);
    if (!state.enabled) view_picnic(pixels, 11, y + 17, "MUTE", &picnic_S, CORAL);
    else if (panel->band_bottom && lane != MIX_808 && lane == band_pad_lane(panel, CONDUCTOR_ROW))
        view_picnic(pixels, 11, y + 20, "9-16", &picnic_S, DIM); /* the bottom pad row launches this lane */
    else if (keys) view_picnic(pixels, 11, y + 20, "KEYS", &picnic_S, CHROME_CREAM);
    unsigned active_library = lane == MIX_TELEGRAM && panel->telegram_library_active;
    unsigned queued_library = lane == MIX_TELEGRAM && panel->telegram_library;
    char number[4];
    unsigned slot = (state.pending ? state.queued : state.active) + 1u;
    number[0] = (char)('0' + slot / 10u); number[1] = (char)('0' + slot % 10u); number[2] = 0;
    const char *shown = (state.pending ? queued_library : active_library) ? "LIB" : number;
    const picnic_face *face = shown == number ? &picnic_L : &picnic_M;
    int nx = 50;
    if (state.pending) nx = view_picnic(pixels, 46, y + 9, ">", &picnic_M, color);
    view_picnic(pixels, nx, y - 4 + (face == &picnic_L ? 0 : 12), shown, face,
                state.pending ? color : state.enabled ? INK : DIM);
    if (panel->draft_slot[lane]) view_small(pixels, 94, y + 3, "*", SKY);
    for (unsigned step = 0; step < DRUM_STEPS; step++) {
        unsigned x = 102 + step * 7;
        unsigned h = view_conductor_step(panel, lane, step);
        uint16_t cell = step / 4u & 1u ? VIEW_RGB(44, 38, 33) : VIEW_RGB(52, 45, 39);
        view_rect(pixels, x, y + 4, 6, 27, cell);
        if (h) view_rect(pixels, x + 1, y + 31 - h, 4, h, state.pending || state.enabled ? color : DIM);
    }
    char pad_number[] = {(char)('1' + row), 0};
    if (state.pending) {
        view_small(pixels, CONDUCTOR_PIPS_X, y + 13, "CUE", color);
        view_conductor_cue_pips(pixels, row, beat);
    } else {
        view_rect(pixels, 222, y + 4, 12, 27, CARD);
        view_small(pixels, 226, y + 14, pad_number, DIM);
    }
}

static unsigned conductor_level(const struct panel *panel, unsigned lane)
{
    return lane == MIX_808_B ? panel->drums_b.mix[MIX_LEVEL] : panel->mixer.value[lane][MIX_LEVEL];
}

static struct strip_cell conductor_home_control(const struct panel *panel, unsigned knob)
{
    /* K1-K4 the levels in pad order, K5 tempo, K6/K7 the two filters, K8 master. */
    static const char *const levels[MIX_AUDIO_LANES] = {
        [MIX_808] = ELITE_DRUM_PARTS > 1 ? "DR A" : "808", [MIX_808_B] = "DR B", [MIX_DILLO] = "303", [MIX_TELEGRAM] = "101"};
    if (knob < BAND_LANES)
        return strip_set(levels[conductor_lane_order[knob]], conductor_level(panel, conductor_lane_order[knob]), 0, 127);
    if (knob < 4) return (struct strip_cell){0, 0};
    if (knob == 4)
        return panel->sound.clock.source == TG_CLOCK_USB ? (struct strip_cell){0, 0} :
               strip_set("BPM", panel->sound.tempo, 30, 300);
    if (knob == 5) return strip_set("LCUT", panel->sound.patch.value[TG_CUTOFF], 0, 127);
    if (knob == 6) return (struct strip_cell){"BCUT", strip_hz(panel->dillo_sound.tone.cutoff_hz)};
    return strip_set("MAIN", panel->sound.master, 0, 127);
}

static void view_conductor_knobs(uint16_t *pixels, const struct panel *panel,
                                 const struct view_livery *liv, unsigned hot)
{
    view_rect(pixels, 0, 184, VIEW_W, 36, VIEW_RGB(28, 25, 22));
    for (unsigned k = 0; k < 8; k++) {
        struct strip_cell cell = conductor_home_control(panel, k);
        struct view_livery active = *liv;
        if (hot == k + 1u && cell.label) {
            view_rect(pixels, k * 30, 185, 30, 35, liv->accent);
            active.ink = active.lamp = liv->ground;
        }
        view_knob_cell(pixels, k, cell.label, cell.value, &active);
    }
}

static void view_conductor_hints(uint16_t *pixels, const struct panel *panel, const struct view_livery *liv)
{
    view_rect(pixels, 0, 220, VIEW_W, 20, liv->ground);
    int x = 5;
    if (panel->draft_view) {
        x = view_keycap(pixels, x, "BT", "HOME", liv);
        if (conductor_scene_pending(panel)) view_keycap(pixels, x, "REC+<", "CANCEL", liv);
        return;
    }
    switch (strip_held(panel->held_buttons)) {
    case BTN_TIE:
        view_keycap(pixels, x, "CHORD", panel->hold ? "HOLD ON" : "HOLD OFF", liv);
        break;
    case BTN_REC:
        x = view_keycap(pixels, x, "PAD", "STORE", liv);
        x = view_keycap(pixels, x, "<", "CANCEL CUE", liv);
        view_keycap(pixels, x, ">", "DRAFTS", liv);
        break;
    case BTN_GLOBE:
        x = view_keycap(pixels, x, "PARA", "SETUPS", liv);
        if (ELITE_DRUM_PARTS == 1) x = view_keycap(pixels, x, "FX", "ROW", liv);
        else {
#ifdef ELITE_SYNTH_PRESETS
            x = view_keycap(pixels, x, "PATCH", "PATTERNS", liv);
#else
            x = view_keycap(pixels, x, "FX", "A/B", liv);
#endif
        }
        view_keycap(pixels, x, "KNOB BANK", "KEYS", liv);
        break;
    case BTN_SEQ:
        x = view_keycap(pixels, x, "PAD", "EDITOR", liv);
        view_keycap(pixels, x, "KEY", "TRANSP", liv);
        break;
    default:
        x = view_keycap(pixels, x, "SEQ", "EDIT", liv);
        x = view_keycap(pixels, x, "FX", "MIX", liv);
        x = view_keycap(pixels, x, "PATCH", "SND", liv);
        if (ELITE_DRUM_PARTS == 1) view_keycap(pixels, x, "BT", "PICK", liv);
        else view_keycap(pixels, x, "REC", "SAVE", liv);
        break;
    }
}

static void view_conductor(uint16_t *pixels, const struct panel *panel, unsigned head)
{
    const struct view_livery *liv = view_livery(panel);
    unsigned hot = panel->strip_knob &&
                   (uint32_t)(panel->view_frames - panel->strip_frame) < CONTROL_FOCUS_FRAMES ?
                   panel->strip_knob : 0;
    view_conductor_knobs(pixels, panel, liv, hot);
    view_conductor_hints(pixels, panel, liv);
    if (panel->draft_view) {
        view_conductor_drafts(pixels, panel);
        return;
    }
    if (panel->conductor_seq_pending) {
        view_picnic(pixels, 8, 22, "EDIT WHICH?", &picnic_L, INK);
        static const char *const lane_short[MIX_AUDIO_LANES] = {
            [MIX_808] = ELITE_DRUM_PARTS > 1 ? "808A" : "808", [MIX_808_B] = "808B", [MIX_DILLO] = "303", [MIX_TELEGRAM] = "101"};
        const int w = (VIEW_W - 16) / BAND_LANES - 6;
        for (unsigned i = 0; i < BAND_LANES; i++) {
            int x = 8 + (int)i * (w + 6);
            unsigned lane = conductor_lane_order[i];
            unsigned selected = panel->conductor_seq_pending == band_lane_editor(lane);
            view_rect(pixels, x, 70, w, 70, selected ? band_lane_color[lane] : CARD);
            view_picnic(pixels, x + 6, 76, lane_short[lane], &picnic_L, selected ? PAPER : band_lane_color[lane]);
            view_small(pixels, x + 6, 124, band_row_pads[i], selected ? PAPER : DIM);
        }
        view_status_line(pixels, 156, "RELEASE SEQ TO OPEN", SKY);
        return;
    }
    unsigned beat = view_header_beat(panel, head);
    for (unsigned row = 0; row < BAND_LANES; row++)
        view_conductor_lane(pixels, panel, row, beat);
    view_conductor_bar(pixels, panel, head);
#if ELITE_DRUM_PARTS > 1
    static const char *const banks[BAND_BANKS] = {"SLOTS 1-4", "SLOTS 5-8", "SLOTS 9-12", "SLOTS 13-16"};
#else
    static const char *const banks[BAND_BANKS] = {"1-4  808 1-8", "5-8  808 1-8", "9-12  808 9-16", "13-16  808 9-16"};
#endif
    const char *slots = panel->band_bottom ? (panel->band_bank < 2 ? "SLOTS 1-8" : "SLOTS 9-16") :
                        banks[panel->band_bank % BAND_BANKS];
    view_small(pixels, 8, 169, slots, DIM);
    if (hot && (hot <= BAND_LANES || hot > 4)) {
        /* The knob being turned, named in full with its value. */
        const char *label;
        unsigned value, digits = 3;
        if (hot <= BAND_LANES) {
            unsigned lane = conductor_lane_order[hot - 1u];
            label = band_lane_name[lane];
            value = conductor_level(panel, lane);
        } else if (hot == 5) {
            label = "TEMPO"; value = panel->sound.tempo;
        } else if (hot == 6) {
            label = "TELEGRAM CUT"; value = panel->sound.patch.value[TG_CUTOFF];
        } else if (hot == 7) {
            label = "DILLO CUT"; value = panel->dillo_sound.tone.cutoff_hz; digits = 5;
        } else {
            label = "MAIN LEVEL"; value = panel->sound.master;
        }
        int x = view_small(pixels, 119, 169, label, CHROME_CREAM) + 4;
        view_small_num(pixels, x, 169, value, digits, CHROME_CREAM);
    }

    const char *status = view_conductor_save_status(panel);
    if (!status && panel->conductor_copy_needed) status = view_copy_prompt(panel);
    if (!status && conductor_scene_pending(panel)) {
        unsigned scene = conductor_queued_scene(panel);
        int x = 8;
        if (scene) {
            char setup[] = "SETUP 0";
            setup[6] = (char)('0' + scene);
            x = view_small(pixels, x, 177, setup, CORAL) + 6;
        }
        view_small(pixels, x, 177, conductor_clock_lost(panel) ? "WAITS FOR CLOCK" :
                   conductor_running(panel) ? "NEXT BAR  REC+< CANCEL" : "LOADING CUE", CORAL);
    } else {
        if (!status && panel->cancel_cue >= CANCEL_CUE_DONE)
            status = panel->cancel_cue == CANCEL_CUE_DONE ? "CUE CANCELED" : "NO QUEUED CUE";
        if (!status && (drum_roll_active(&panel->drums_roll) || drum_roll_active(&panel->drums_b.roll)))
            status = "RPT TAP CLEAR  HOLD ROLL";
        if (!status) {
            unsigned drafts = 0;
            for (unsigned i = 0; i < MIX_AUDIO_LANES; i++) drafts |= panel->draft_slot[i];
            status = drafts ? "REC+> DRAFTS" : (panel->conductor_dirty || panel->conductor_startup_dirty) ?
                     "REC+PAD COPY  REC STARTUP*" : panel->library_status == LIB_SAVED ?
                     "SAVED" : NULL; /* idle: the hint row and hold layers carry the combinations */
        }
        if (status) view_small(pixels, 8, 177, status, DIM);
    }
}
