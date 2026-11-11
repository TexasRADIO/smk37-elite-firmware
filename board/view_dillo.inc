/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef ELITE_DILLO
#ifdef ELITE_DRUMS808
enum {
    DILLO_PLOT = VIEW_RGB(232, 226, 216), DILLO_GRID = VIEW_RGB(190, 182, 170),
    DILLO_STRIP = VIEW_RGB(200, 193, 182), DILLO_DIM = VIEW_RGB(82, 76, 68)
};
#endif
static void view_dillo_time_pass(uint16_t *pixels, const struct panel *panel)
{
#ifdef ELITE_DRUMS808
    /* Time pass: notes left to place, big; sixteen steps shaped like the pads, each drawn as what
     * it is (note block, rest outline, tie bar); the five pad meanings as chips; one live line. */
    view_rect(pixels, 0, 18, VIEW_W, 166, CHROME_303);
    view_picnic(pixels, 8, 20, "TIME PASS", &picnic_L, CHROME_303_INK);
    {
        char used[8];
        unsigned at = 0, u = panel->dillo_used, n = panel->dillo_pitch_count;
        if (u >= 10u) used[at++] = (char)('0' + u / 10u);
        used[at++] = (char)('0' + u % 10u);
        used[at++] = '/';
        if (n >= 10u) used[at++] = (char)('0' + n / 10u);
        used[at++] = (char)('0' + n % 10u);
        used[at] = 0;
        int r = VIEW_W - 8 - view_picnic_width(used, &picnic_M);
        view_picnic(pixels, r, 26, used, &picnic_M, CHROME_303_INK);
        view_picnic(pixels, r - 6 - view_picnic_width("NOTES", &picnic_S), 30, "NOTES", &picnic_S, DILLO_DIM);
    }
    const dillo_step *edit = panel->dillo_time_edit ? &panel->dillo_time[panel->dillo_time_edit - 1u] : NULL;
    for (unsigned i = 0; i < DILLO_STEPS; i++) {
        unsigned selected = panel->dillo_time_edit == i + 1u;
        unsigned next = !edit && i == panel->dillo_time_count;
        const dillo_step *step = &panel->dillo_time[i];
        int col = 8 + (int)(i % 8u) * 28, row = 58 + (int)(i / 8u) * 30;
        uint16_t cell = selected ? CHROME_303_RED : DILLO_PLOT;
        if (pad_hit_glow(panel, i)) cell = picnic_mix(cell, CHROME_303_RED, pad_hit_glow(panel, i) * 2u);
        view_rect(pixels, col, row, 26, 26, cell);
        if (next) { /* where the next tap lands */
            view_rect(pixels, col, row, 26, 2, CHROME_303_RED); view_rect(pixels, col, row + 24, 26, 2, CHROME_303_RED);
            view_rect(pixels, col, row, 2, 26, CHROME_303_RED); view_rect(pixels, col + 24, row, 2, 26, CHROME_303_RED);
        }
        if (i >= panel->dillo_time_count) continue;
        uint16_t ink = selected ? CHROME_303 : CHROME_303_INK;
        if (step->event == DILLO_NOTE) view_rect(pixels, col + 4, row + 8, 18, 10, ink);
        else if (step->event == DILLO_TIE) view_rect(pixels, col, row + 11, 26, 4, ink);
        else { view_rect(pixels, col + 8, row + 12, 10, 2, DILLO_DIM); }
        if (step->flags & DILLO_ACCENT) view_rect(pixels, col + 4, row + 3, 5, 4, CHROME_303_RED);
        if (step->flags & DILLO_SLIDE) view_slope(pixels, col + 14, row + 21, 12, row + 18, ink);
    }
    /* Pads 1-5 while entering: what each does; ACC and SLD light when armed for the next note. */
    unsigned flags = edit ? edit->flags : panel->dillo_take_flags;
    static const char *const chip[5] = {"1 NOTE", "2 REST", "3 TIE", "4 ACC", "5 SLD"};
    for (unsigned i = 0; i < 5; i++) {
        int x = 8 + (int)i * 45;
        unsigned lit = (i == 3 && (flags & DILLO_ACCENT)) || (i == 4 && (flags & DILLO_SLIDE));
        view_rect(pixels, x, 120, 42, 14, lit ? CHROME_303_RED : DILLO_GRID);
        view_small(pixels, x + 3, 123, chip[i], lit ? CHROME_303 : CHROME_303_INK);
    }
    if (edit && edit->event == DILLO_NOTE) { /* editing a note: its pitch and gate big */
        unsigned pitch = panel->dillo_pitches[dillo_time_rank(panel, panel->dillo_time_edit - 1u)];
        char note[6];
        int x = view_picnic(pixels, 8, 136, jukebox_note_name(note, pitch ? pitch : TG_NOTE_ZERO), &picnic_L, CHROME_303_INK) + 10;
        x = view_picnic(pixels, x, 150, "K8 PITCH  K6 GATE", &picnic_S, DILLO_DIM);
        (void)x;
        return;
    }
    char left[20] = "00 NOTES TO PLACE";
    unsigned remaining = panel->dillo_pitch_count > panel->dillo_used ? panel->dillo_pitch_count - panel->dillo_used : 0;
    left[0] = (char)('0' + remaining / 10u); left[1] = (char)('0' + remaining % 10u);
    if (remaining < 10u) { for (unsigned i = 0; left[i]; i++) left[i] = left[i + 1]; }
    if (remaining == 1u) { static const char one[] = "1 NOTE TO PLACE"; for (unsigned i = 0; i < sizeof one; i++) left[i] = one[i]; }
    const char *apply = panel->dillo_take_error ? "CHECK PITCH COUNT" :
        panel->dillo_time_pick ? (conductor_cues_at_bar(panel) ? "PICK A WRITTEN STEP" : "HOLD A PAD TO HEAR") :
        !panel->dillo_time_count ? "TAP THE TIMING" :
        panel->dillo_used < panel->dillo_pitch_count ?
            panel->dillo_time_count == DILLO_STEPS ? "EDIT OR UNDO A STEP" : left :
        conductor_cues_at_bar(panel) ? "REC: APPLY AT THE BAR" : "REC: APPLY TAKE";
    view_status_line(pixels, 140, apply, CHROME_303_RED);
    view_small(pixels, 8, 164, "CHORD+PAD EDIT   CHORD+< PITCH", DILLO_DIM);
#else
    const dillo_step *edit = panel->dillo_time_edit ? &panel->dillo_time[panel->dillo_time_edit - 1u] : NULL;
    view_text(pixels, 10, 75, "PITCH", 1, DIM);
    view_num(pixels, 58, 75, panel->dillo_used, 2, INK);
    view_text(pixels, 74, 75, "/", 1, DIM);
    view_num(pixels, 82, 75, panel->dillo_pitch_count, 2, INK);
    view_text(pixels, 128, 75, "STEPS", 1, DIM);
    view_num(pixels, 176, 75, panel->dillo_time_count, 2, LIME);
    view_text(pixels, 192, 75, "/16", 1, DIM);
    for (unsigned i = 0; i < DILLO_STEPS; i++) {
        unsigned x = 10 + i % 8 * 28, y = 96 + i / 8 * 29;
        unsigned next = i == panel->dillo_time_count;
        unsigned selected = panel->dillo_time_edit == i + 1u;
        view_rect(pixels, x, y, 24, 26, next ? SKY : CARD);
        if (selected) view_rect(pixels, x, y + 24, 24, 2, LIME);
        view_num(pixels, x + 4, y, i + 1, 2, selected ? LIME : next ? PAPER : i < panel->dillo_time_count ? INK : DIM);
        if (i >= panel->dillo_time_count) continue;
        const dillo_step *step = &panel->dillo_time[i];
        const char *mark = step->event == DILLO_NOTE ? "N" : step->event == DILLO_TIE ? "T" : "R";
        unsigned color = step->flags & DILLO_ACCENT ? CORAL : step->event == DILLO_TIE ? SKY : INK;
        view_text(pixels, x + 4, y + 11, mark, 1, color);
        if (step->flags & DILLO_SLIDE)
            view_rect(pixels, x + 13, y + 21, 8, 2, SKY);
    }
    view_text(pixels, 10, 152, panel->dillo_time_pick ? "PICK A WRITTEN STEP" : "1 NOTE   2 REST   3 TIE", 1, INK);
    unsigned flags = edit ? edit->flags : panel->dillo_take_flags;
    unsigned gate = edit ? edit->gate_percent : panel->dillo_take_gate;
    view_text(pixels, 10, 168, "4 ACCENT", 1, flags & DILLO_ACCENT ? CORAL : DIM);
    view_text(pixels, 83, 168, "5 SLIDE", 1, flags & DILLO_SLIDE ? SKY : DIM);
    view_text(pixels, 142, 168, "K6 GATE", 1, DIM);
    if (gate) view_num(pixels, 206, 168, gate, 3, INK);
    else view_text(pixels, 206, 168, "---", 1, DIM);
    const char *apply = "REC APPLY TAKE";
#ifdef ELITE_DRUMS808
    if (conductor_cues_at_bar(panel)) apply = "REC APPLY NEXT BAR";
#endif
    if (!panel->dillo_time_count) apply = "ENTER TIMING";
    else if (panel->dillo_used < panel->dillo_pitch_count)
        apply = panel->dillo_time_count == DILLO_STEPS ? "EDIT OR UNDO TO ADD NOTE" : "ENTER REMAINING NOTES";
    view_text(pixels, 10, 185, panel->dillo_take_error ? "CHECK PITCH COUNT" : apply, 1,
              panel->dillo_take_error ? CORAL : SKY);
    view_text(pixels, 10, 200, "CHORD+PAD EDIT  PARA CANCEL", 1, DIM);
    view_text(pixels, 10, 216, edit ? "< EXIT EDIT  CHORD+< PITCH" : "< UNDO  CHORD+< PITCH", 1, DIM);
#endif
}

#ifdef ELITE_DRUMS808

static void view_rhythm_pattern(uint16_t *pixels, const struct panel *panel, unsigned lane,
                                unsigned x, unsigned y, uint16_t ink, uint16_t cue)
{
    struct conductor_row_state state = conductor_lane_state(panel, lane);
    x = view_small(pixels, x, y, state.pending ? "EDIT" : "PATTERN", ink) + 6;
    x = view_small_num(pixels, x, y, state.queued + 1u, 2, state.pending ? cue : ink) + 6;
    if (state.pending && state.enabled) {
        x = view_small(pixels, x, y, "NOW", ink) + 6;
        x = view_small_num(pixels, x, y, state.active + 1u, 2, ink) + 6;
    } else if (!state.enabled) x = view_small(pixels, x, y, "MUTE", cue) + 6;
    if (state.pending)
        view_small(pixels, x, y, conductor_clock_lost(panel) ? "WAITS FOR CLOCK" :
                   conductor_running(panel) ? "NEXT BAR" : "LOADING", cue);
}

/* The pattern a lane is playing, big: PATTERN and its number, or the cued number in the cue
 * colour with NEXT BAR, and MUTE once when it is muted. */
static void view_big_pattern(uint16_t *pixels, const struct panel *panel, unsigned lane,
                             int x, int y, uint16_t ink, uint16_t dim, uint16_t cue)
{
    struct conductor_row_state state = conductor_lane_state(panel, lane);
    x = view_picnic(pixels, x, y + 9, state.pending ? "NEXT" : "PATTERN", &picnic_M, dim) + 8;
    unsigned slot = (state.pending ? state.queued : state.active) + 1u;
    char number[3] = {(char)('0' + slot / 10u), (char)('0' + slot % 10u), 0};
    x = view_picnic(pixels, x, y - 4, number, &picnic_L, state.pending ? cue : ink) + 8;
    if (state.pending)
        view_picnic(pixels, x, y + 13, conductor_clock_lost(panel) ? "WAITS" : conductor_running(panel) ? "AT THE BAR" : "LOADING",
                    &picnic_S, cue);
    else if (!state.enabled) {
        int w = view_picnic_width("MUTE", &picnic_M) + 10;
        view_rect(pixels, VIEW_W - 8 - w, y + 6, w, 22, cue);
        view_picnic(pixels, VIEW_W - 3 - w, y + 8, "MUTE", &picnic_M, CHROME_CREAM);
    }
}


/* The nine-banded armadillo, 18 x 10, walking: shell, bands, head, tail and two legs that swap per step. */
static void view_dillo_walker(uint16_t *pixels, int cx, int y, unsigned gait)
{
    static const uint8_t widths[] = {8, 12, 16, 18, 18, 18, 18};
    for (unsigned row = 0; row < sizeof widths; row++)
        view_rect(pixels, cx - widths[row] / 2, y + (int)row, widths[row], 1, CHROME_303_INK);
    for (int x = -6; x <= 6; x += 2) {
        int inset = x == -6 || x == 6 ? 2 : x == -4 || x == 4 ? 1 : 0;
        view_rect(pixels, cx + x, y + 1 + inset, 1, 6 - inset, DILLO_GRID);
    }
    view_rect(pixels, cx + 9, y + 4, 3, 3, CHROME_303_INK);
    view_rect(pixels, cx + 9, y + 2, 1, 2, CHROME_303_INK);
    view_rect(pixels, cx + 12, y + 5, 2, 1, CHROME_303_INK);
    view_rect(pixels, cx - 11, y + 7, 2, 1, CHROME_303_INK);
    view_rect(pixels, cx - 5 + (int)(gait & 1u), y + 7, 2, 3, CHROME_303_INK);
    view_rect(pixels, cx + 3 - (int)(gait & 1u), y + 7, 2, 3, CHROME_303_INK);
}

static void view_dillo_roll(uint16_t *pixels, const struct panel *panel, unsigned head)
{
    const dillo_phrase *phrase = &panel->dillo_pattern;
    view_rect(pixels, 0, 18, VIEW_W, 166, CHROME_303);
    view_big_pattern(pixels, panel, MIX_DILLO, 8, 20, CHROME_303_INK, DILLO_DIM, CHROME_303_RED);
    enum { ROLL_TOP = 72, ROLL_H = 56, ROLL_BASE = ROLL_TOP + ROLL_H - 6 };
    unsigned low = 127, high = 0;
    for (unsigned i = 0; i < phrase->length; i++) {
        const dillo_step *step = &phrase->steps[i];
        if (step->event != DILLO_NOTE) continue;
        if (step->pitch < low) low = step->pitch;
        if (step->pitch > high) high = step->pitch;
    }
    int positions[DILLO_STEPS];
    for (unsigned i = 0; i < DILLO_STEPS; i++) {
        const dillo_step *step = &phrase->steps[i];
        int x = 8 + 14 * i;
        positions[i] = high > low && step->event == DILLO_NOTE && i < phrase->length ?
                       ROLL_BASE - (int)((step->pitch - low) * (unsigned)(ROLL_H - 12) / (high - low)) : ROLL_TOP + ROLL_H / 2;
        uint16_t column = i == head ? DILLO_GRID : i >= phrase->length ? CHROME_303 : i / 4u & 1u ? DILLO_PLOT : DILLO_STRIP;
        unsigned glow = pad_hit_glow(panel, i);
        if (glow) column = picnic_mix(column, CHROME_303_RED, glow * 2u);
        view_rect(pixels, x, ROLL_TOP, 13, ROLL_H, column);
        if (i == panel->dillo_cursor) view_rect(pixels, x, ROLL_TOP + ROLL_H + 2, 13, 3, CHROME_303_RED);
    }
    for (unsigned i = 0; i < phrase->length; i++) {
        const dillo_step *step = &phrase->steps[i];
        int x = 8 + 14 * i;
        if (step->event == DILLO_TIE) {
            view_rect(pixels, x + 2, ROLL_BASE + 2, 9, 2, DILLO_DIM);
        } else if (step->event == DILLO_NOTE) {
            int width = 4 + step->gate_percent * 8 / 100;
            view_rect(pixels, x + 1, positions[i], width, 5, i == head ? CHROME_303_RED : CHROME_303_INK);
            if (step->flags & DILLO_ACCENT) view_rect(pixels, x + 5, ROLL_TOP + 3, 3, 3, CHROME_303_RED);
            if (step->flags & DILLO_SLIDE) {
                unsigned next = i + 1u;
                while (next < phrase->length && phrase->steps[next].event == DILLO_TIE) next++;
                if (next < phrase->length && phrase->steps[next].event == DILLO_NOTE)
                    view_slope(pixels, x + 1 + width, positions[i] + 1,
                               (next - i) * 14 - width, positions[next] + 1, DILLO_DIM);
                else view_rect(pixels, x + 8, ROLL_TOP + 8, 4, 2, DILLO_DIM);
            }
        }
    }
    /* The step under the cursor: its note big, then gate and the two articulations. */
    const dillo_step *step = &phrase->steps[panel->dillo_cursor];
    unsigned event = panel->dillo_cursor < phrase->length ? step->event : DILLO_REST;
    int x = 8;
    if (event == DILLO_NOTE) {
        unsigned pitch = step->pitch ? step->pitch : TG_NOTE_ZERO;
        char text[6];
        unsigned at = 0;
        for (const char *c = jukebox_pc_name[pitch % 12u]; *c; c++) text[at++] = *c;
        int octave = (int)(pitch / 12u) - 1;
        if (octave < 0) { text[at++] = '-'; octave = -octave; }
        text[at++] = (char)('0' + octave % 10);
        text[at] = 0;
        x = view_picnic(pixels, 8, 132, text, &picnic_L, CHROME_303_INK) + 10;
    } else x = view_picnic(pixels, 8, 141, event == DILLO_TIE ? "TIE" : "REST", &picnic_M, DILLO_DIM) + 10;
    if (event != DILLO_REST) {
        char gate[4] = {(char)('0' + step->gate_percent / 100u % 10u), (char)('0' + step->gate_percent / 10u % 10u),
                        (char)('0' + step->gate_percent % 10u), 0};
        const char *g = step->gate_percent >= 100u ? gate : gate + 1;
        x = view_picnic(pixels, x, 145, "GATE", &picnic_S, DILLO_DIM) + 4;
        x = view_picnic(pixels, x, 141, g, &picnic_M, CHROME_303_INK) + 8;
    }
    int right = VIEW_W - 8;
    if (step->flags & DILLO_SLIDE && event != DILLO_REST) {
        int w = view_picnic_width("SLD", &picnic_M) + 8;
        right -= w;
        view_rect(pixels, right, 139, w, 22, CHROME_303_INK);
        view_picnic(pixels, right + 4, 141, "SLD", &picnic_M, CHROME_303);
        right -= 4;
    }
    if (step->flags & DILLO_ACCENT && event != DILLO_REST) {
        int w = view_picnic_width("ACC", &picnic_M) + 8;
        right -= w;
        view_rect(pixels, right, 139, w, 22, CHROME_303_RED);
        view_picnic(pixels, right + 4, 141, "ACC", &picnic_M, CHROME_303);
    }
    (void)x;
    /* Only live states; the button combinations live in the hint row and hold layers. */
    const char *hint = panel->dillo_pitch_paused ? "TELEGRAM KEYS, REC PAUSED" :
                       panel->dillo_record ? "PLAY A KEY: PITCH" :
                       panel->dillo_audition ? "RELEASE TO STOP" :
                       panel->dillo_undo == UNDO_NEW ? "CHORD+< UNDO TAKE" : 0;
    if (hint) view_status_line(pixels, 164, hint, DILLO_DIM);
    /* The armadillo walks the playhead along the top of the roll. */
    if (head < phrase->length && conductor_running(panel)) /* our Dillo rides the playhead, a step at a time */
        view_dillo_mask(pixels, 8 + 14 * (int)head + 6 - DILLO_SPRITE_W / 2, ROLL_TOP - DILLO_SPRITE_H - 1 + (int)(head & 1u),
                        dillo_sprite, dillo_sprite_span, DILLO_SPRITE_W, DILLO_SPRITE_H, DILLO_SPRITE_BITS, CHROME_303_INK, 15u, 19, ROLL_TOP, 0, 0);
}
#endif

static void view_dillo_steps(uint16_t *pixels, const struct panel *panel, unsigned head)
{
    if (panel->dillo_record >= DILLO_REC_PITCH) {
        unsigned pitch = panel->dillo_record == DILLO_REC_PITCH;
#ifdef ELITE_DRUMS808
        if (pitch) {
            view_rect(pixels, 0, 18, VIEW_W, 166, CHROME_303);
            view_picnic(pixels, 8, 20, "PITCH PASS", &picnic_L, CHROME_303_INK);
            view_label_number(pixels, 8, 50, NULL, panel->dillo_pitch_count, DILLO_DIM, CHROME_303_RED);
            view_picnic(pixels, 50, 59, "PITCHES", &picnic_S, CHROME_303_INK);
            for (unsigned i = 0; i < panel->dillo_pitch_count; i++) {
                unsigned x = 8 + i % 4u * 57u, y = 84 + i / 4u * 15u;
                unsigned selected = panel->dillo_pitch_replace == i + 1u;
                if (selected) view_rect(pixels, x - 2, y - 1, 55, 15, DILLO_GRID);
                char note[6];
                view_picnic(pixels, x, y - 2, jukebox_note_name(note, panel->dillo_pitches[i]), &picnic_M,
                            selected ? CHROME_303_RED : CHROME_303_INK);
            }
            /* One live line; REC, PARA and < are on the hint row. */
            view_status_line(pixels, 152, panel->dillo_pitch_paused ? "TELEGRAM KEYS, PAUSED" :
                             panel->dillo_pitch_replace ? "PLAY KEY TO REPLACE" : "KEYS ADD, PADS PICK", CHROME_303_INK);
        } else view_dillo_time_pass(pixels, panel);
#else
        view_title(pixels, 10, 38, pitch ? "PITCH PASS" : "TIME PASS", INK);
        if (pitch) {
            view_text(pixels, 10, 77, "PITCHES", 1, DIM);
            view_num(pixels, 82, 77, panel->dillo_pitch_count, 2, LIME);
            for (unsigned i = 0; i < panel->dillo_pitch_count; i++) {
                unsigned x = 10 + i % 4 * 56, y = 100 + i / 4 * 19;
                unsigned selected = panel->dillo_pitch_replace == i + 1u;
                view_num(pixels, x, y, i + 1u, 2, selected ? LIME : DIM);
                view_note(pixels, x + 18, y, panel->dillo_pitches[i], 1, selected ? LIME : INK);
            }
            view_text(pixels, 10, 181,
#ifdef ELITE_DRUMS808
                      panel->dillo_pitch_paused ? "TELEGRAM KEYS  REC PAUSED" :
#endif
                      panel->dillo_pitch_replace ? "PLAY KEY TO REPLACE" :
                      "KEYS ADD  PADS SELECT", 1, SKY);
            view_text(pixels, 10, 197, "REC FOR TIME", 1, INK);
            view_text(pixels, 10, 216, panel->dillo_pitch_replace ? "< DESELECT  PARA CANCEL" :
                      "< UNDO  PARA CANCEL", 1, DIM);
        } else {
            view_dillo_time_pass(pixels, panel);
        }
#endif
        return;
    }
#ifdef ELITE_DRUMS808
    view_dillo_roll(pixels, panel, head);
#else
    view_title(pixels, 10, 38, "DILLO STEPS", INK);
    for (unsigned i = 0; i < DILLO_STEPS; i++) {
        const dillo_step *step = &panel->dillo_pattern.steps[i];
        unsigned x = 10 + i % 8 * 28, y = 72 + i / 8 * 34;
        unsigned color = i == head ? SKY : i == panel->dillo_cursor ? LIME : CARD;
        view_rect(pixels, x, y, 24, 29, color);
        view_num(pixels, x + 4, y + 2, i + 1, 2, color == CARD ? INK : PAPER);
        if (step->event == DILLO_TIE)
            view_rect(pixels, x + 4, y + 21, 16, 2, SKY);
        else if (step->event == DILLO_NOTE)
            view_rect(pixels, x + 4, y + 21, step->flags & DILLO_SLIDE ? 16 : 7, 2,
                      step->flags & DILLO_ACCENT ? CORAL : INK);
    }
    const dillo_step *step = &panel->dillo_pattern.steps[panel->dillo_cursor];
    if (step->event == DILLO_NOTE)
        view_note(pixels, 10, 148, step->pitch ? step->pitch : TG_NOTE_ZERO, 2, LIME);
    else view_text(pixels, 10, 148, step->event == DILLO_TIE ? "TIE" : "REST", 2, DIM);
    if (step->event != DILLO_REST) {
        view_text(pixels, 96, 148, "GATE", 1, DIM);
        view_num(pixels, 138, 148, step->gate_percent, 3, INK);
    }
    view_text(pixels, 10, 176, "K5 ACC K6 GATE K7 SLD K8 PIT", 1, DIM);
    view_text(pixels, 10, 189, "CHORD+PAD TIE  RPT+PAD REST", 1, DIM);
    if (!panel->dillo_record) view_text(pixels, 10, 202, "CHORD+> SHIFT", 1, DIM);
    view_text(pixels, 124, 202, "CHORD+< UNDO", 1, panel->dillo_undo ? LIME : DIM);
    view_text(pixels, 10, 215,
#ifdef ELITE_DRUMS808
              panel->dillo_pitch_paused ? "TELEGRAM KEYS  REC PAUSED" :
#endif
              panel->dillo_record ? "PLAY KEY TO ENTER PITCH" :
#ifdef ELITE_DRUMS808
              panel->dillo_audition ? "RELEASE PAD TO STOP" :
              !conductor_running(panel) ? "HOLD PAD TO HEAR   PATCH" :
              panel->dillo_enabled ? "PATCH  REC  CHORD+REC TAKE" :
#else
              panel->sound.clock.source == TG_CLOCK_USB ? "INTERNAL CLOCK REQUIRED" :
              panel->dillo_enabled ? "REC PITCH CHORD+REC TAKE" :
#endif
              "DILLO MUTED  PARA ON", 1, SKY);
#endif
}
#endif

#ifdef ELITE_DRUMS808
static void view_dillo_sound(uint16_t *pixels, const struct panel *panel)
{
    static const char *const user_names[] = {"USER 1", "USER 2", "USER 3", "USER 4"};
    unsigned slot = panel->dillo_sound_slot;
    const char *name = slot < BB_DILLO_PATCHES ? bb_dillo_bank[slot].name :
        slot < DILLO_SOUND_PAGE_SLOTS ? user_names[slot - BB_DILLO_PATCHES] :
        slot < DILLO_CLASSIC_START ? dillo_expansion[slot - DILLO_SOUND_PAGE_SLOTS].name :
        slot < DILLO_DEVIL_START ? dillo_classic[slot - DILLO_CLASSIC_START].name :
        slot < DILLO_SOUND_SLOTS ? dillo_devilfish[slot - DILLO_DEVIL_START].name :
        dillo_studio_sound_name(slot - DILLO_SOUND_SLOTS);
    const bb_dillo_patch *patch = &panel->dillo_sound.tone;
    unsigned mod = panel->dillo_sound_page;
    view_rect(pixels, 0, 18, VIEW_W, 166, CHROME_303);
    jukebox_big(pixels, 8, 19, 160, name, CHROME_303_INK);
    if (dillo_sound_edited(panel)) view_picnic(pixels, 170, 22, "*", &picnic_M, CHROME_303_RED);
    view_tag_left(pixels, VIEW_W - 8, 22, mod ? "MOD" : "TONE", CHROME_303_INK, CHROME_303);
    char bank_label[6];
    unsigned bank_now = panel->dillo_sound_bank + 1u, bank_all = dillo_sound_banks(), at = 0;
    if (bank_now > 9u) bank_label[at++] = (char)('0' + bank_now / 10u);
    bank_label[at++] = (char)('0' + bank_now % 10u);
    bank_label[at++] = '/';
    if (bank_all > 9u) bank_label[at++] = (char)('0' + bank_all / 10u);
    bank_label[at++] = (char)('0' + bank_all % 10u);
    bank_label[at] = 0;
    view_small(pixels, 202, 63, bank_label, DILLO_DIM);
    unsigned base = panel->dillo_sound_bank * DILLO_SOUND_PAGE_SLOTS;
    for (unsigned i = 0; i < DILLO_SOUND_PAGE_SLOTS && base + i < dillo_sound_total(); i++) {
        unsigned x = 8 + i * 16;
        unsigned active = base + i == slot;
        unsigned occupied = conductor_dillo_sound_exists(panel->dillo_sound_slots, base + i);
        uint16_t c = active ? CHROME_303_INK : occupied ? DILLO_GRID : DILLO_PLOT;
        if (pad_hit_glow(panel, i)) c = picnic_mix(c, CHROME_303_RED, pad_hit_glow(panel, i) * 3u);
        view_rect(pixels, x, 59, 14, 13, c);
        view_small_num(pixels, x + 1, 62, i + 1u, 2, active ? CHROME_303 : DILLO_DIM);
    }

    const dillo_phrase *phrase = &panel->dillo_pattern;
    view_rect(pixels, 8, 76, 224, 47, DILLO_PLOT);
    view_rect(pixels, 8, 116, 224, 1, DILLO_GRID);
    for (unsigned i = 0; i < DILLO_STEPS; i++) {
        const dillo_step *step = &phrase->steps[i];
        if (i >= phrase->length || step->event != DILLO_NOTE) continue;
        int x = 8 + 14 * i;
        unsigned accented = step->flags & DILLO_ACCENT;
        unsigned height = 8 + patch->envelope * 22u / 100u + (accented ? patch->accent * 8u / 100u : 0);
        unsigned attack = 1 + patch->attack_ms / 64u;
        unsigned fall = 2 + patch->decay * 9u / 100u;
        unsigned sustain = height * fall / 15u;
        view_slope(pixels, x, 116, attack, 116 - height, CHROME_303_INK);
        view_slope(pixels, x + attack, 116 - height, fall, 116 - sustain, CHROME_303_INK);
        if (accented) view_rect(pixels, x + 3, 79, 3, 3, CHROME_303_RED);
    }

    /* The TB-303's knob row, in its panel order, with each value under its knob. The knob being
     * turned is drawn in the 303's LED red. */
    unsigned accent = panel->dillo_sound.decay.accent, amp = panel->dillo_sound.decay.amp;
    unsigned hot = panel->strip_knob && (uint32_t)(panel->view_frames - panel->strip_frame) < CONTROL_FOCUS_FRAMES ?
                   panel->strip_knob - 1u : 8u;
    static const char *const sweep[DILLO_SWEEP_COUNT] = {"STOCK", "LAG", "LONG"};
    struct { const char *label; unsigned position; unsigned number; const char *text; } knob[6];
    unsigned count;
    if (mod) {
        knob[0] = (__typeof__(knob[0])){"ATK", patch->attack_ms * 127u / 255u, patch->attack_ms, 0};
        knob[1] = (__typeof__(knob[0])){"TRK", patch->tracking * 127u / 100u, patch->tracking, 0};
        knob[2] = (__typeof__(knob[0])){"ADC", accent * 127u / DILLO_DECAY_MAX, accent * DILLO_DECAY_STEP_MS, accent ? 0 : "STOCK"};
        knob[3] = (__typeof__(knob[0])){"AMP", amp * 127u / DILLO_DECAY_MAX, amp * DILLO_DECAY_STEP_MS, amp ? 0 : "STOCK"};
        knob[4] = (__typeof__(knob[0])){"SWP", panel->dillo_sound.decay.sweep * 127u / (DILLO_SWEEP_COUNT - 1u), 0,
                                        sweep[panel->dillo_sound.decay.sweep]};
        count = 5;
    } else {
        knob[0] = (__typeof__(knob[0])){"CUT", strip_hz(patch->cutoff_hz), patch->cutoff_hz, 0};
        knob[1] = (__typeof__(knob[0])){"RES", patch->resonance_x100 * 127u / 2000u, patch->resonance_x100 / 100u, 0};
        knob[2] = (__typeof__(knob[0])){"ENV", patch->envelope * 127u / 100u, patch->envelope, 0};
        knob[3] = (__typeof__(knob[0])){"DEC", patch->decay * 127u / 100u, 30u + 10u * patch->decay, 0};
        knob[4] = (__typeof__(knob[0])){"ACC", patch->accent * 127u / 100u, patch->accent, 0};
        knob[5] = (__typeof__(knob[0])){"SLD", patch->slide_ms * 127u / 255u, patch->slide_ms, 0};
        count = 6;
    }
    static const int8_t tip[25][2] = {
        {-6, 6}, {-7, 4}, {-7, 3}, {-8, 2}, {-8, 0}, {-8, -2}, {-7, -3}, {-7, -4}, {-6, -6}, {-4, -7}, {-3, -7}, {-2, -8},
        {0, -8}, {2, -8}, {3, -7}, {4, -7}, {6, -6}, {7, -4}, {7, -3}, {8, -2}, {8, 0}, {8, 2}, {7, 3}, {7, 4}, {6, 6}
    };
    static const uint8_t half[21] = {3, 5, 6, 7, 8, 8, 9, 9, 10, 10, 10, 10, 10, 9, 9, 8, 8, 7, 6, 5, 3};
    for (unsigned i = 0; i < count; i++) {
        int cx = 24 + (int)i * 38, cy = 140;
        uint16_t cap = i == hot ? CHROME_303_RED : CHROME_303_INK;
        for (int row = 0; row < 21; row++) view_rect(pixels, cx - half[row], cy - 10 + row, half[row] * 2 + 1, 1, cap);
        unsigned step = knob[i].position * 24u / 127u;
        for (int p = 2; p <= 8; p++)
            view_rect(pixels, cx + p * tip[step][0] / 8, cy + p * tip[step][1] / 8, 1, 1, CHROME_303);
        int w = view_small_width(knob[i].label);
        view_small(pixels, cx - w / 2, 154, knob[i].label, CHROME_303_INK);
        char text[8];
        const char *value = knob[i].text;
        if (!value) {
            unsigned v = knob[i].number, n = 0;
            char tmp[6];
            do { tmp[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < sizeof tmp);
            for (unsigned j = 0; j < n; j++) text[j] = tmp[n - 1u - j];
            text[n] = 0;
            value = text;
        }
        w = view_small_width(value);
        view_small(pixels, cx - w / 2, 164, value, i == hot ? CHROME_303_RED : DILLO_DIM);
    }
    const char *status = view_conductor_save_status(panel);
    if (!status) status =
        panel->dillo_sound_status == KIT_EMPTY ? "EMPTY SOUND SLOT" :
        panel->dillo_sound_status == KIT_CAPTURED && panel->conductor_dirty ? "CAPTURED  REC SAVE SOUNDS" :
        panel->library_status == LIB_SAVED && !panel->conductor_dirty ? "SOUNDS SAVED" :
        panel->dillo_sound_bank > 3u ? "STUDIO SOUNDS  PARA NEXT PAGE" :
        panel->dillo_sound_bank == 3u ? "DEVILFISH  PARA NEXT PAGE" :
        panel->dillo_sound_bank == 2u ? "CLASSIC 303  PARA NEXT PAGE" :
        panel->dillo_sound_bank ? "BASS 1-4 ACID 5-8 LEAD 9-12" : "REC+PAD 9-12 CAPTURE";
    view_small(pixels, 8, 175, status, DILLO_DIM);

}
#endif
