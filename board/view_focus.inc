/* SPDX-License-Identifier: GPL-3.0-only */
/* Redesign T2: focus takeover. While a knob is being turned (CONTROL_FOCUS_FRAMES after
 * its last detent) and its page does not already show its picture, the stage (y 18-183)
 * shows what that control does to the sound: K chip, name, value, a one-line note and
 * a picture drawn from the live state. Integer arithmetic only; no allocation. */

/* Drawn a few times a second in the foreground, never in the audio path: build for size. */
#define FOCUS_SMALL __attribute__((minsize))

enum {
    FOCUS_Y = 18, FOCUS_H = 166,
    PIC_X = 8, PIC_Y = 82, PIC_W = 224, PIC_H = 96
};

#define focus_sin view_sin64

FOCUS_SMALL static int focus_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

FOCUS_SMALL static void focus_px(uint16_t *pixels, int x, int y, uint16_t color)
{
    if (x >= 0 && x < VIEW_W && y >= FOCUS_Y && y < FOCUS_Y + FOCUS_H) pixels[y * VIEW_W + x] = color;
}

FOCUS_SMALL static void focus_line(uint16_t *pixels, int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        focus_px(pixels, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

/* Column trace: join each sample to the previous one vertically, as view_wave does. */
FOCUS_SMALL static void focus_trace(uint16_t *pixels, int x, const int16_t *ys, int n, uint16_t color)
{
    for (int i = 0; i < n; i++) {
        int a = ys[i], b = i ? ys[i - 1] : ys[i];
        int top = a < b ? a : b, bottom = a > b ? a : b;
        view_rect(pixels, x + i, top, 1, bottom - top + 1, color);
    }
}

FOCUS_SMALL static void focus_fill(uint16_t *pixels, int x, const int16_t *ys, int n, int base, uint16_t color)
{
    for (int i = 0; i < n; i++) if (ys[i] < base) view_rect(pixels, x + i, ys[i], 1, base - ys[i], color);
}

FOCUS_SMALL static char *focus_number(char *out, int value)
{
    char tmp[8];
    unsigned n = 0, v = (unsigned)(value < 0 ? -value : value);
    do { tmp[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < sizeof tmp);
    char *p = out;
    if (value < 0) *p++ = '-';
    while (n) *p++ = tmp[--n];
    *p = 0;
    return out;
}

/* ---------- pictures ---------- */

/* What the voice is doing, when the board supplies it (TG_LIVE, written after each audio block by
 * tg_live_read). Host renders and a build without it draw the stored sound only. Nothing in a
 * picture moves unless the voice moved it: there is no display clock in the filter or envelope. */
FOCUS_SMALL static unsigned focus_live(tg_live *live)
{
#ifdef TG_LIVE
    *live = *TG_LIVE;
    return live->active;
#else
    memset(live, 0, sizeof *live);
    return 0;
#endif
}

/* Filter position (0 closed .. 255 open, the engine's own normalized cutoff) to a column. */
FOCUS_SMALL static int filter_column(int position)
{
    return 10 + focus_clamp(position, 0, 255) * (PIC_W - 40) / 255;
}

FOCUS_SMALL static void filter_curve(int16_t *ys, int corner, int peak)
{
    int base = PIC_Y + PIC_H - 6, top = PIC_Y + 20;
    for (int i = 0; i < PIC_W; i++) {
        int d = i - corner, y = d <= 0 ? top : top + d * d / 20 + d;
        if (d > -12 && d < 12) y -= peak * (12 - (d < 0 ? -d : d)) / 12;
        ys[i] = (int16_t)focus_clamp(y, PIC_Y + 2, base);
    }
}

/* 24 dB low-pass at the stored CUTOFF with the stored RESONANCE as its peak. `reach` is how far the
 * stored ENV amount can move the corner (tg_filter_env_reach; negative closes): a bracket from the
 * corner to that limit. While a note sounds, a second outline shows where the filter is now. */
FOCUS_SMALL static void pic_filter(uint16_t *pixels, const struct view_livery *liv, const tg_patch *patch, int show_reach)
{
    int16_t ys[PIC_W];
    tg_live live;
    unsigned sounding = focus_live(&live);
    int base = PIC_Y + PIC_H - 6, top = PIC_Y + 20;
    int set = patch->value[TG_CUTOFF] * 255 / 127, corner = filter_column(set);
    int peak = patch->value[TG_RESONANCE] * (PIC_H / 2) / 127;
    int reach = tg_filter_env_reach(patch), limit = filter_column(set + reach);
    filter_curve(ys, corner, peak);
    for (int i = 0; i < PIC_W; i += 4) view_rect(pixels, PIC_X + i, base + 2, 2, 1, liv->lamp);
    focus_fill(pixels, PIC_X, ys, PIC_W, base, liv->lamp);
    focus_trace(pixels, PIC_X, ys, PIC_W, liv->accent);
    if (show_reach && reach) { /* the envelope's range: from the corner to as far as ENV can take it */
        int lo = corner < limit ? corner : limit, hi = corner < limit ? limit : corner;
        for (int y = top - 2; y < base; y += 3) focus_px(pixels, PIC_X + limit, y, liv->ink);
        view_rect(pixels, PIC_X + lo, top - 6, hi - lo + 1, 1, liv->ink);
        view_rect(pixels, PIC_X + lo, top - 8, 1, 5, liv->ink);
        view_rect(pixels, PIC_X + hi, top - 8, 1, 5, liv->ink);
        const char *label = "ENV LIMIT";
        int lw = view_small_width(label);
        view_small(pixels, PIC_X + (limit + 5 + lw < PIC_W ? limit + 5 : limit - 5 - lw), top + 30, label, liv->ink);
    }
    if (sounding && live.cutoff_now != live.cutoff_set) { /* where the held or releasing note has it now */
        int now = filter_column(live.cutoff_now);
        filter_curve(ys, now, peak);
        focus_trace(pixels, PIC_X, ys, PIC_W, liv->ink);
        view_rect(pixels, PIC_X + now, base - 3, 1, 6, liv->ink);
        view_small(pixels, PIC_X + focus_clamp(now + 16, 0, PIC_W - 44), top + 4, live.gate ? "NOW" : "RELEASE", liv->ink);
    }
}

/* The source wave, then the same wave through a one-pole low-pass at the cutoff. */
FOCUS_SMALL static void pic_wave(uint16_t *pixels, const struct view_livery *liv, const tg_patch *patch, int cutoff,
                     int level, int x, int w)
{
    int16_t raw[PIC_W], out[PIC_W];
    int mid = PIC_Y + PIC_H / 2, amp = (PIC_H * 2 / 5) * level / 127;
    int width = 50 - patch->value[TG_WIDTH] * 45 / 127;
    int alpha = 40 + cutoff * cutoff * 980 / (127 * 127), y = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < w; i++) {
            int phase = (i * 200 / w) % 100;
            int s = ((phase * 2 - 100) * patch->value[TG_SAW] + (phase < width ? 100 : -100) * patch->value[TG_PULSE] +
                     ((i * 100 / w) < 50 ? 100 : -100) * patch->value[TG_SUB]) / 254;
            y += (s - y) * alpha / 1024;
            if (pass) { raw[i] = (int16_t)(mid - s * amp / 100); out[i] = (int16_t)(mid - y * amp * 13 / 1000); }
        }
    view_rect(pixels, x, mid, w, 1, liv->lamp);
    focus_trace(pixels, x, raw, w, liv->lamp);
    focus_trace(pixels, x, out, w, liv->accent);
}

/* Envelope shapes as the engine makes them, 17 points across a stage (0..255 high).
 * Attack climbs toward 1.6 and stops at 1; decay and release fall by 95% across the stage. */
static const uint8_t adsr_rise[17] = {0, 24, 47, 68, 89, 108, 126, 142, 158, 173, 187, 200, 212, 224, 235, 245, 255};
static const uint8_t adsr_fall[17] = {255, 211, 175, 145, 121, 100, 83, 69, 57, 47, 39, 33, 27, 22, 19, 15, 13};

FOCUS_SMALL static int adsr_shape(const uint8_t *shape, int i, int w)
{
    int at = w > 0 ? i * 256 / w : 0, k = at >> 4, f = at & 15;
    if (k >= 16) return shape[16];
    return shape[k] + (shape[k + 1] - shape[k]) * f / 16;
}

/* `times`: print the hot stage's time by the engine's own law (tg_stage_ms). Dillo's borrowed use
 * of this picture passes 0 because those are not Telegram's stage controls. */
FOCUS_SMALL static void pic_adsr(uint16_t *pixels, const struct view_livery *liv, int a, int d, int s, int r, int hot, int times)
{
    int base = PIC_Y + PIC_H - 8, top = PIC_Y + 6, span = PIC_W - 8, high = base - top;
    int wa = 6 + a * span / 500, wd = 6 + d * span / 500, wr = 6 + r * span / 500, ws = span - wa - wd - wr;
    int sustain = s * 255 / 127;
    int px[5] = {PIC_X, PIC_X + wa, PIC_X + wa + wd, PIC_X + wa + wd + ws, PIC_X + span};
    int16_t ys[PIC_W];
    for (int i = 0; i < span; i++) {
        int x = PIC_X + i, level;
        if (x < px[1]) level = adsr_shape(adsr_rise, x - px[0], wa);
        else if (x < px[2]) level = sustain + (255 - sustain) * adsr_shape(adsr_fall, x - px[1], wd) / 255;
        else if (x < px[3]) level = sustain;
        else level = sustain * adsr_shape(adsr_fall, x - px[3], wr) / 255;
        ys[i] = (int16_t)(base - level * high / 255);
    }
    view_rect(pixels, PIC_X, base + 1, span, 1, liv->lamp);
    for (int k = 0; k < 4; k++) {
        static const char *const stage[] = {"A", "D", "S", "R"};
        focus_fill(pixels, px[k], ys + (px[k] - PIC_X), px[k + 1] - px[k], base, k == hot ? liv->lamp : liv->ground);
        focus_trace(pixels, px[k], ys + (px[k] - PIC_X), px[k + 1] - px[k], k == hot ? liv->accent : liv->ink);
        view_small(pixels, (px[k] + px[k + 1]) / 2 - 2, base + 3 > PIC_Y + PIC_H - 7 ? PIC_Y + PIC_H - 7 : base + 3,
                   stage[k], k == hot ? liv->accent : liv->ink);
    }
    if (times && hot != 2) { /* the hot stage's real time */
        unsigned ms = tg_stage_ms((unsigned)hot, (unsigned)(hot == 0 ? a : hot == 1 ? d : r));
        char text[12], *p = focus_number(text, ms >= 1000u ? (int)(ms / 1000u) : (int)ms);
        while (*p) p++;
        if (ms >= 1000u) { *p++ = '.'; *p++ = (char)('0' + ms % 1000u / 100u); }
        *p = 0;
        int x = view_small(pixels, PIC_X + PIC_W - 52, PIC_Y, text, liv->ink);
        view_small(pixels, x + 3, PIC_Y, ms >= 1000u ? "S" : "MS", liv->ink);
    }
    /* The sounding note: a dot on the curve at the stage and level the voice reports. No note, no dot. */
    tg_live live;
    if (times && focus_live(&live)) {
        int k = live.stage == TG_LIVE_ATTACK ? 0 : live.stage == TG_LIVE_DECAY ? 1 : 3;
        int want = base - live.level * high / 255, best = px[k], gap = 1 << 30;
        if (k == 1 && live.level <= sustain + 2) { k = 2; best = (px[2] + px[3]) / 2; } /* settled on sustain */
        else
            for (int x = px[k]; x < px[k + 1]; x++) {
                int miss = ys[x - PIC_X] - want;
                if (miss < 0) miss = -miss;
                if (miss < gap) { gap = miss; best = x; }
            }
        view_rect(pixels, best - 2, focus_clamp(want, top, base) - 2, 5, 5, liv->ink);
    }
}

FOCUS_SMALL static void pic_pulse(uint16_t *pixels, const struct view_livery *liv, int width, int pwm, int source, uint32_t t)
{
    int hi = PIC_Y + 10, lo = PIC_Y + PIC_H - 12, cw = PIC_W / 2;
    int rest = 50 - width * 45 / 127, swing = source ? focus_sin[(t / 8u) & 63] * pwm * 20 / (127 * 127) : 0;
    int duty = focus_clamp(rest - (swing < 0 ? -swing : swing), 4, 50);
    for (int c = 0; c < 2; c++) {
        int x0 = PIC_X + c * cw, xe = x0 + duty * cw / 100;
        if (source && pwm) {
            int low = x0 + focus_clamp(rest - pwm * 20 / 127, 4, 50) * cw / 100;
            for (int y = hi; y < lo; y += 2) view_rect(pixels, low, y, x0 + rest * cw / 100 - low + 1, 1, liv->lamp);
        }
        view_rect(pixels, x0, hi, 2, lo - hi, liv->accent); view_rect(pixels, x0, hi, xe - x0, 2, liv->accent);
        view_rect(pixels, xe, hi, 2, lo - hi, liv->accent); view_rect(pixels, xe, lo - 2, x0 + cw - xe, 2, liv->accent);
    }
    char text[8];
    focus_number(text, duty);
    int x = view_small(pixels, PIC_X + PIC_W - 60, PIC_Y, "DUTY ", liv->ink);
    x = view_small(pixels, x, PIC_Y, text, liv->ink);
    view_small(pixels, x, PIC_Y, " PCT", liv->ink);
}

/* LFO scrolling at a speed that rises with the rate; shape from the MOD selector. */
FOCUS_SMALL static void pic_lfo(uint16_t *pixels, const struct view_livery *liv, int rate, int wave, int depth, uint32_t t)
{
    static const int8_t random_levels[8] = {80, -90, 110, -20, 40, -120, 10, 95};
    int16_t ys[PIC_W];
    int mid = PIC_Y + PIC_H / 2, amp = (PIC_H * 2 / 5) * focus_clamp(depth, 8, 127) / 127;
    unsigned speed = 1u + (unsigned)(rate * rate) / 256u, offset = t * speed;
    for (int i = 0; i < PIC_W; i++) {
        unsigned p = (offset + (unsigned)i * 3u) % 256u;
        int v;
        if (wave == TG_LFO_SQUARE) v = p < 128u ? 127 : -127;
        else if (wave == TG_LFO_RANDOM) v = random_levels[((offset + (unsigned)i * 3u) / 256u) & 7u];
        else if (wave == TG_LFO_NOISE) v = random_levels[((unsigned)i * 7u + offset / 4u) & 7u];
        else v = p < 128u ? (int)p * 2 - 127 : 383 - (int)p * 2;
        ys[i] = (int16_t)(mid - v * amp / 127);
    }
    view_rect(pixels, PIC_X, mid, PIC_W, 1, liv->lamp);
    focus_trace(pixels, PIC_X, ys, PIC_W, liv->accent);
}

FOCUS_SMALL static void pic_glide(uint16_t *pixels, const struct view_livery *liv, int amount, int max, uint32_t t)
{
    int16_t ys[PIC_W];
    int y0 = PIC_Y + PIC_H - 18, y1 = PIC_Y + 14, span = PIC_W - 40, start = span / 10;
    int len = focus_clamp(amount * (span * 8 / 10) / (max ? max : 1), 2, span * 8 / 10);
    for (int i = 0; i < span; i++) {
        int y = i < start ? y0 : i < start + len ? y0 + (y1 - y0) * (i - start) / len : y1;
        ys[i] = (int16_t)y;
    }
    view_rect(pixels, PIC_X, y0, 12, 6, liv->lamp);
    view_rect(pixels, PIC_X + PIC_W - 12, y1, 12, 6, liv->lamp);
    focus_trace(pixels, PIC_X + 20, ys, span, liv->accent);
    (void)t; /* the path is the setting; no dot rides it on a display clock */
    view_small(pixels, PIC_X, y0 + 9, "C3", liv->ink);
    view_small(pixels, PIC_X + PIC_W - 14, y1 - 9, "C4", liv->ink);
}

/* Dillo's slide, on the pattern's own steps: the step that carries the slide, the note it slides
 * into, and the pitch path between them. The path is the voice's law (an RC approach whose time
 * constant is a fifth of SLIDE TIME) drawn against the length of a step at the current tempo. */
FOCUS_SMALL static void pic_slide(uint16_t *pixels, const struct view_livery *liv, const dillo_phrase *phrase,
                      unsigned cursor, int slide_ms, unsigned tempo)
{
    static const uint8_t approach[17] = {255, 187, 137, 100, 73, 54, 39, 29, 21, 15, 11, 8, 6, 4, 3, 2, 2};
    unsigned grid = dillo_phrase_grid(phrase), from = DILLO_STEPS, to = 0;
    for (unsigned n = 0; n < grid && from == DILLO_STEPS; n++) { /* the first slid step at or after the cursor */
        unsigned i = (cursor + n) % grid, j = (i + 1u) % grid;
        if (phrase->steps[i].event == DILLO_NOTE && (phrase->steps[i].flags & DILLO_SLIDE) &&
            phrase->steps[j].event == DILLO_NOTE) { from = i; to = j; }
    }
    if (from == DILLO_STEPS) {
        view_small(pixels, PIC_X, PIC_Y + PIC_H / 2 - 10, "NO STEP IN THIS PATTERN SLIDES", liv->ink);
        view_small(pixels, PIC_X, PIC_Y + PIC_H / 2 + 2, "MARK A STEP'S SLIDE TO HEAR THIS", liv->lamp);
        return;
    }
    int a = phrase->steps[from].pitch, b = phrase->steps[to].pitch, w = (PIC_W - 8) / 2;
    int lo = a < b ? a : b, hi = a < b ? b : a, range = hi - lo;
    int y_lo = PIC_Y + PIC_H - 26, y_hi = PIC_Y + 22;
    int ya = range ? y_lo - (a - lo) * (y_lo - y_hi) / range : (y_lo + y_hi) / 2;
    int yb = range ? y_lo - (b - lo) * (y_lo - y_hi) / range : (y_lo + y_hi) / 2;
    int step_ms = (int)(15000u / (tempo ? tempo : 120u));
    int len = focus_clamp(slide_ms * w / (step_ms ? step_ms : 1), 2, w);
    int16_t ys[PIC_W];
    for (int i = 0; i < 2 * w; i++) {
        int y = ya;
        if (i >= w) {
            int u = (i - w) * 256 / len, k = u >> 4, f = u & 15;
            int left = k >= 16 ? 0 : approach[k] + (approach[k + 1] - approach[k]) * f / 16;
            y = yb + (ya - yb) * left / 255;
        }
        ys[i] = (int16_t)y;
    }
    for (unsigned n = 0; n < 2; n++) { /* the two steps: number, note, accent */
        unsigned i = n ? to : from;
        int x = PIC_X + (int)n * (w + 8);
        char text[6], *num = focus_number(text, (int)i + 1);
        view_rect(pixels, x, PIC_Y + PIC_H - 14, w, 12, liv->lamp);
        int tx = view_small(pixels, x + 4, PIC_Y + PIC_H - 11, "STEP ", liv->ink);
        tx = view_small(pixels, tx, PIC_Y + PIC_H - 11, num, liv->ink);
        char note[6];
        tx = view_small(pixels, tx + 8, PIC_Y + PIC_H - 11, jukebox_note_name(note, phrase->steps[i].pitch ? phrase->steps[i].pitch : TG_NOTE_ZERO), liv->accent);
        if (phrase->steps[i].flags & DILLO_ACCENT) view_small(pixels, tx + 6, PIC_Y + PIC_H - 11, "ACC", CHROME_RED);
    }
    view_rect(pixels, PIC_X + w + 3, PIC_Y + 14, 1, PIC_H - 30, liv->lamp);
    focus_trace(pixels, PIC_X, ys, w, liv->accent);
    focus_trace(pixels, PIC_X + w + 8, ys + w, w, liv->accent);
    for (int k = -1; k <= 1; k++) { /* thicker */
        if (!k) continue;
        for (int i = 0; i < w; i++) { focus_px(pixels, PIC_X + i, ys[i] + k, liv->accent); focus_px(pixels, PIC_X + w + 8 + i, ys[w + i] + k, liv->accent); }
    }
    view_rect(pixels, PIC_X + w + 8, y_lo + 6, len, 2, liv->ink); /* how much of the next step the slide takes */
    view_small(pixels, PIC_X, PIC_Y, "SLIDES INTO THE NEXT NOTE", liv->ink);
}

FOCUS_SMALL static void pic_meter(uint16_t *pixels, const struct view_livery *liv, int level, uint32_t t)
{
    int h = PIC_H - 4, beat = (int)(t % 160u), fill = h * level / 127 * (160 - beat / 2) / 160;
    for (int k = 0; k < 2; k++)
        for (int y = 0; y < h; y += 3)
            view_rect(pixels, PIC_X + PIC_W - 16 + k * 8, PIC_Y + PIC_H - 2 - y, 6, 2,
                      y < fill ? (y > h * 85 / 100 ? CHROME_RED : liv->accent) : liv->lamp);
}

FOCUS_SMALL static void pic_keys(uint16_t *pixels, const struct view_livery *liv, int x, int y, int w, int h,
                     uint16_t lit_white, uint16_t lit_black, unsigned root)
{
    static const uint8_t white[7] = {0, 2, 4, 5, 7, 9, 11}, black[5] = {1, 3, 6, 8, 10};
    static const uint8_t black_after[5] = {0, 1, 3, 4, 5};
    int ww = w / 7;
    for (int i = 0; i < 7; i++) {
        unsigned pc = white[i];
        view_rect(pixels, x + i * ww, y, ww - 1, h, (lit_white >> i) & 1u ? liv->accent : liv->ink);
        if (pc == root) view_rect(pixels, x + i * ww + ww / 2 - 2, y + h - 7, 4, 4, CHROME_RED);
    }
    for (int i = 0; i < 5; i++) {
        unsigned pc = black[i];
        view_rect(pixels, x + (black_after[i] + 1) * ww - ww / 3, y, ww * 2 / 3, h * 3 / 5,
                  (lit_black >> i) & 1u ? liv->accent : liv->ground);
        if (pc == root) view_rect(pixels, x + (black_after[i] + 1) * ww - 2, y + h * 3 / 5 - 7, 4, 4, CHROME_RED);
    }
}

FOCUS_SMALL static void pic_scale(uint16_t *pixels, const struct view_livery *liv, unsigned root, unsigned type)
{
    static const uint8_t white[7] = {0, 2, 4, 5, 7, 9, 11}, black[5] = {1, 3, 6, 8, 10};
    unsigned mask = scale_intervals[type];
    uint16_t lw = 0, lb = 0;
    for (int i = 0; i < 7; i++) if (type && ((mask >> ((white[i] + 12u - root) % 12u)) & 1u)) lw |= (uint16_t)(1u << i);
    for (int i = 0; i < 5; i++) if (type && ((mask >> ((black[i] + 12u - root) % 12u)) & 1u)) lb |= (uint16_t)(1u << i);
    pic_keys(pixels, liv, PIC_X, PIC_Y + 6, PIC_W, PIC_H - 30, lw, lb, root);
    view_small(pixels, PIC_X, PIC_Y + PIC_H - 14, type ? "KEYS SNAP TO THE LIT NOTES" : "SCALE OFF - EVERY KEY PLAYS", liv->ink);
}

FOCUS_SMALL static void pic_keybed_track(uint16_t *pixels, const struct view_livery *liv, int amount)
{
    int ky = PIC_Y + PIC_H - 20;
    pic_keys(pixels, liv, PIC_X, ky, PIC_W / 3, 18, 0, 0, 12);
    pic_keys(pixels, liv, PIC_X + PIC_W / 3, ky, PIC_W / 3, 18, 0, 0, 12);
    pic_keys(pixels, liv, PIC_X + 2 * PIC_W / 3, ky, PIC_W / 3, 18, 0, 0, 12);
    for (int j = 0; j < 3; j++) {
        int kx = PIC_X + PIC_W / 6 + j * PIC_W / 3;
        int cx = PIC_X + PIC_W / 2 + (j - 1) * amount * (PIC_W * 35 / 100) / 127;
        focus_line(pixels, kx, ky - 2, cx, PIC_Y + 30, j == 1 ? liv->accent : liv->lamp);
        view_rect(pixels, cx - 1, PIC_Y + 22, 3, 8, j == 1 ? liv->accent : liv->ink);
    }
    view_small(pixels, PIC_X, PIC_Y + 4, "CUTOFF FOLLOWS THE KEY", liv->ink);
}

FOCUS_SMALL static void pic_vibrato(uint16_t *pixels, const struct view_livery *liv, int depth, int rate, uint32_t t)
{
    int16_t ys[PIC_W];
    int mid = PIC_Y + PIC_H / 2, amp = depth * (PIC_H * 38 / 100) / 127;
    unsigned speed = 1u + (unsigned)(rate * rate) / 512u;
    for (int i = 0; i < PIC_W; i += 6) view_rect(pixels, PIC_X + i, mid, 3, 1, liv->lamp);
    for (int i = 0; i < PIC_W; i++) ys[i] = (int16_t)(mid - focus_sin[(t * speed / 2u + (unsigned)i * 3u / 4u) & 63u] * amp / 127);
    focus_trace(pixels, PIC_X, ys, PIC_W, liv->accent);
    view_small(pixels, PIC_X, mid - 10, "C4", liv->ink);
}

FOCUS_SMALL static void pic_tiles(uint16_t *pixels, const struct view_livery *liv, const char *const *labels, unsigned n, unsigned selected)
{
    int gap = 4, w = (PIC_W - (int)(n - 1u) * gap) / (int)n;
    for (unsigned i = 0; i < n; i++) {
        int x = PIC_X + (int)i * (w + gap);
        uint16_t fill = i == selected ? liv->accent : liv->lamp, ink = i == selected ? liv->ground : liv->ink;
        view_rect(pixels, x + 1, PIC_Y + 4, w - 2, PIC_H - 8, fill);
        view_rect(pixels, x, PIC_Y + 5, w, PIC_H - 10, fill);
        view_small(pixels, x + (w - view_small_width(labels[i])) / 2, PIC_Y + PIC_H / 2 - 3, labels[i], ink);
    }
}

FOCUS_SMALL static void pic_footage(uint16_t *pixels, const struct view_livery *liv, unsigned range)
{
    static const char *const feet[] = {"16'", "8'", "4'", "2'"};
    int w = (PIC_W - 12) / 4;
    for (unsigned i = 0; i < 4; i++) {
        int x = PIC_X + (int)i * (w + 4);
        view_rect(pixels, x, PIC_Y + 4, w, 26, i == range ? liv->accent : liv->lamp);
        view_picnic(pixels, x + (w - view_picnic_width(feet[i], &picnic_M)) / 2, PIC_Y + 12, feet[i], &picnic_M, i == range ? liv->ground : liv->ink);
    }
    int lo = 24 + (int)range * 12, x0 = PIC_X + (lo - 24) * PIC_W / 72;
    for (int o = 0; o < 6; o++) pic_keys(pixels, liv, PIC_X + o * PIC_W / 6, PIC_Y + 40, PIC_W / 6, 20, 0, 0, 12);
    view_rect(pixels, x0, PIC_Y + 64, 37 * PIC_W / 72, 3, liv->accent);
    view_small(pixels, PIC_X, PIC_Y + 74, "THE 37 KEYS LAND HERE", liv->ink);
}

FOCUS_SMALL static void pic_tempo(uint16_t *pixels, const struct view_livery *liv, int bpm, uint32_t t)
{
    int cx = PIC_X + PIC_W / 2, cy = PIC_Y + 4, len = PIC_H - 16;
    unsigned beat_frames = (unsigned)(625 * 60 / (bpm > 0 ? bpm : 120));
    unsigned phase = t % (beat_frames * 2u) * 64u / (beat_frames * 2u);
    int angle = focus_sin[phase] * 8 / 127; /* +/- 8 of 64 steps: +/- 45 degrees */
    int ex = cx + focus_sin[(unsigned)angle & 63u] * len / 127;
    int ey = cy + focus_sin[(unsigned)(angle + 16) & 63u] * len / 127;
    focus_line(pixels, cx - 30, PIC_Y + PIC_H - 2, cx + 30, PIC_Y + PIC_H - 2, liv->lamp);
    focus_line(pixels, cx, cy, ex, ey, liv->accent);
    focus_line(pixels, cx + 1, cy, ex + 1, ey, liv->accent);
    view_rect(pixels, ex - 4, ey - 4, 9, 9, liv->ink);
    if (phase < 4u || (phase >= 32u && phase < 36u)) view_rect(pixels, cx - 2, PIC_Y + PIC_H - 6, 5, 3, liv->accent);
}

FOCUS_SMALL static void pic_drive(uint16_t *pixels, const struct view_livery *liv, int drive)
{
    int16_t clean[PIC_W], hot[PIC_W];
    int mid = PIC_Y + PIC_H / 2, amp = PIC_H * 38 / 100, gain = 128 + drive * 8;
    for (int i = 0; i < PIC_W; i++) {
        int s = focus_sin[(i * 128 / PIC_W) & 63];
        int g = focus_clamp(s * gain / 128, -127, 127);
        clean[i] = (int16_t)(mid - s * amp / 127);
        hot[i] = (int16_t)(mid - g * amp / 127);
    }
    view_rect(pixels, PIC_X, mid, PIC_W, 1, liv->lamp);
    focus_trace(pixels, PIC_X, clean, PIC_W, liv->lamp);
    focus_trace(pixels, PIC_X, hot, PIC_W, liv->accent);
}

FOCUS_SMALL static void pic_delay(uint16_t *pixels, const struct view_livery *liv, int amount, uint32_t t)
{
    int base = PIC_Y + PIC_H - 4, h = PIC_H - 10, ratio = 300 + amount * 620 / 127, lit = (int)(t / 160u) % 7;
    for (int k = 0; k < 7 && h > 1; k++) {
        view_rect(pixels, PIC_X + 4 + k * PIC_W / 7, base - h, 8, h, k == lit ? liv->accent : k ? liv->lamp : liv->ink);
        h = h * ratio / 1000;
    }
    view_rect(pixels, PIC_X, base, PIC_W, 1, liv->lamp);
}

FOCUS_SMALL static void pic_reverb(uint16_t *pixels, const struct view_livery *liv, int amount, uint32_t t)
{
    int cx = PIC_X + 20, cy = PIC_Y + PIC_H / 2, reach = (PIC_W - 30) * (32 + amount * 96 / 127) / 128;
    int grow = (int)(t % 320u), n = 30 + amount * 160 / 127;
    view_rect(pixels, cx - 3, cy - 3, 7, 7, liv->ink);
    unsigned seed = 43;
    for (int i = 0; i < n; i++) {
        seed = seed * 1103515245u + 12345u;
        int d = (int)((seed >> 16) % (unsigned)reach) * focus_clamp(grow * 3, 0, 320) / 320;
        int a = (int)((seed >> 8) & 63u);
        focus_px(pixels, cx + d, cy + focus_sin[a] * d / 280, d * 2 < reach ? liv->accent : liv->lamp);
    }
}

FOCUS_SMALL static void pic_tuner(uint16_t *pixels, const struct view_livery *liv, int cents)
{
    int cx = PIC_X + PIC_W / 2, cy = PIC_Y + PIC_H - 6, r = PIC_H - 14;
    for (int c = -50; c <= 50; c += 10) {
        unsigned a = (unsigned)(48 + c * 16 / 50) & 63u; /* 48 = straight up */
        int inner = r - (c % 50 ? 4 : 9);
        focus_line(pixels, cx + focus_sin[(a + 16u) & 63u] * inner / 127, cy + focus_sin[a] * inner / 127,
                   cx + focus_sin[(a + 16u) & 63u] * r / 127, cy + focus_sin[a] * r / 127, c ? liv->lamp : liv->ink);
    }
    unsigned a = (unsigned)(48 + cents * 16 / 50) & 63u;
    focus_line(pixels, cx, cy, cx + focus_sin[(a + 16u) & 63u] * (r - 2) / 127, cy + focus_sin[a] * (r - 2) / 127,
               cents ? liv->accent : CHROME_808_WHITE);
    view_rect(pixels, cx - 3, cy - 3, 7, 7, liv->ink);
    view_small(pixels, PIC_X, cy - 8, "-50", liv->ink);
    view_small(pixels, PIC_X + PIC_W - 18, cy - 8, "+50", liv->ink);
}

FOCUS_SMALL static void pic_channels(uint16_t *pixels, const struct view_livery *liv, unsigned channel, const char *note)
{
    for (unsigned i = 0; i < 16; i++) {
        int x = PIC_X + (int)(i % 8u) * (PIC_W / 8), y = PIC_Y + 4 + (int)(i / 8u) * 24;
        char text[4];
        view_rect(pixels, x + 1, y, PIC_W / 8 - 3, 20, i == channel ? liv->accent : liv->lamp);
        focus_number(text, (int)i + 1);
        view_small(pixels, x + (PIC_W / 8 - view_small_width(text)) / 2, y + 7, text, i == channel ? liv->ground : liv->ink);
    }
    view_small(pixels, PIC_X, PIC_Y + 58, note, liv->ink);
}

FOCUS_SMALL static void pic_decay(uint16_t *pixels, const struct view_livery *liv, int ms, int max_ms, const char *note)
{
    int16_t ys[PIC_W];
    int base = PIC_Y + PIC_H - 6, top = PIC_Y + 10, level = 1024;
    int k = focus_clamp(ms * 1000 / (max_ms ? max_ms : 1), 20, 1000), step = 1024 * 12 / (k + 12);
    for (int i = 0; i < PIC_W; i++) {
        ys[i] = (int16_t)(base - level * (base - top) / 1024);
        level = level * (1024 - step / 8) / 1024;
    }
    view_rect(pixels, PIC_X, base + 1, PIC_W, 1, liv->lamp);
    focus_fill(pixels, PIC_X, ys, PIC_W, base, liv->lamp);
    focus_trace(pixels, PIC_X, ys, PIC_W, liv->accent);
    if (note) view_small(pixels, PIC_X + 40, PIC_Y, note, liv->ink);
}

FOCUS_SMALL static void pic_saw_square(uint16_t *pixels, const struct view_livery *liv, unsigned square)
{
    int half = PIC_W / 2, top = PIC_Y + 10, bottom = PIC_Y + PIC_H - 22;
    uint16_t saw = square ? liv->lamp : liv->accent, sq = square ? liv->accent : liv->lamp;
    int x = PIC_X + 10, w = half - 30;
    focus_line(pixels, x, bottom, x + w / 2, top, saw); focus_line(pixels, x + w / 2, top, x + w / 2, bottom, saw);
    focus_line(pixels, x + w / 2, bottom, x + w, top, saw);
    x = PIC_X + half + 10;
    view_rect(pixels, x, top, 2, bottom - top, sq); view_rect(pixels, x, top, w / 2, 2, sq);
    view_rect(pixels, x + w / 2, top, 2, bottom - top, sq); view_rect(pixels, x + w / 2, bottom, w / 2, 2, sq);
    view_small(pixels, PIC_X + half / 2 - 9, PIC_Y + PIC_H - 10, "SAW", saw);
    view_small(pixels, PIC_X + half + half / 2 - 18, PIC_Y + PIC_H - 10, "SQUARE", sq);
}

FOCUS_SMALL static void pic_gate(uint16_t *pixels, const struct view_livery *liv, int percent)
{
    int cw = PIC_W / 8, mid = PIC_Y + PIC_H / 2;
    for (int i = 0; i < 8; i++) {
        view_rect(pixels, PIC_X + i * cw, mid - 6, focus_clamp(cw * percent / 100 - 1, 1, cw), 12, liv->accent);
        view_rect(pixels, PIC_X + i * cw, mid + 10, 1, 4, liv->lamp);
    }
    view_small(pixels, PIC_X, PIC_Y, "OF EACH STEP SOUNDS", liv->ink);
}

FOCUS_SMALL static void pic_swing(uint16_t *pixels, const struct view_livery *liv, int swing25)
{
    int cw = PIC_W / 8;
    for (int i = 0; i < 8; i++) {
        int x = PIC_X + i * cw + (i & 1 ? swing25 * cw / 25 : 0);
        view_rect(pixels, x, PIC_Y + 12, 3, PIC_H - 24, i & 1 ? liv->accent : liv->ink);
        view_rect(pixels, PIC_X + i * cw, PIC_Y + PIC_H - 8, 1, 4, liv->lamp);
    }
    view_small(pixels, PIC_X, PIC_Y, swing25 ? "EVERY SECOND STEP LANDS LATE" : "STRAIGHT", liv->ink);
}

FOCUS_SMALL static void pic_loop(uint16_t *pixels, const struct view_livery *liv, int length, int steps)
{
    int shown = length > 32 ? steps : 32, cw = PIC_W * 100 / shown;
    for (int i = 0; i < shown && i < 100; i++)
        view_rect(pixels, PIC_X + i * cw / 100, PIC_Y + 22, focus_clamp(cw / 100 - 1, 1, 7), PIC_H - 34,
                  i < length ? liv->lamp : liv->ground);
    int end = PIC_X + length * cw / 100;
    view_rect(pixels, PIC_X, PIC_Y + 12, end - PIC_X, 2, liv->accent);
    view_rect(pixels, PIC_X, PIC_Y + 12, 2, 7, liv->accent);
    view_rect(pixels, end - 2, PIC_Y + 12, 2, 7, liv->accent);
    focus_line(pixels, end - 2, PIC_Y + 13, end - 8, PIC_Y + 8, liv->accent);
    focus_line(pixels, end - 2, PIC_Y + 13, end - 8, PIC_Y + 18, liv->accent);
    view_small(pixels, PIC_X, PIC_Y, "PLAYS THIS MANY STEPS, THEN BACK", liv->ink);
}

#ifdef ELITE_DRUMS808
/* MOTION: the recorded movement of one knob across its bar. The thin line is the control's own
 * value; the bars are where the movement takes it; the marker is where the bar is now. */
FOCUS_SMALL static void pic_motion(uint16_t *pixels, const struct view_livery *liv, const struct panel *panel)
{
    unsigned target = panel->motion_target, range = motion_range(target), state = panel->motion_state;
    int top = PIC_Y + 14, base = PIC_Y + PIC_H - 14, high = base - top, w = PIC_W / MOTION_POINTS;
    int own = state == MOTION_RECORDING ? motion.base : (int)motion_get(panel, target);
    int own_y = base - own * high / (int)range;
    for (int i = 0; i < PIC_W; i += 3) focus_px(pixels, PIC_X + i, own_y, liv->ink);
    if (state != MOTION_ARMED)
        for (unsigned i = 0; i < MOTION_POINTS; i++) {
            unsigned done = state == MOTION_PLAYING || (unsigned)(((motion.position >> 8) + MOTION_POINTS - i) % MOTION_POINTS) * 256u <= motion.travelled;
            int value = focus_clamp(own + motion.offset[i], 0, (int)range), y = base - value * high / (int)range;
            if (!done) continue;
            if (y < own_y) view_rect(pixels, PIC_X + (int)i * w, y, w - 1, own_y - y + 1, liv->accent);
            else view_rect(pixels, PIC_X + (int)i * w, own_y, w - 1, y - own_y + 1, liv->accent);
        }
    for (unsigned beat = 0; beat <= 4; beat++) /* the bar's four beats */
        view_rect(pixels, focus_clamp(PIC_X + (int)beat * (PIC_W / 4), PIC_X, PIC_X + PIC_W - 1), base + 3, 1, 4, liv->lamp);
    if (state != MOTION_ARMED)
        view_rect(pixels, PIC_X + (int)(motion.position * (unsigned)PIC_W / MOTION_SPAN), top - 4, 1, base - top + 8, liv->ink);
    view_small(pixels, PIC_X, PIC_Y, state == MOTION_ARMED ? "TURN IT NOW: ONE BAR RECORDS" :
               state == MOTION_RECORDING ? "RECORDING ONE BAR" : "LOOPS WITH THIS SOUND", state == MOTION_PLAYING ? liv->ink : CHROME_RED);
    view_small(pixels, PIC_X, PIC_Y + PIC_H - 7, state == MOTION_PLAYING ? "REC REMOVES IT" : "REC CANCELS", liv->ink);
}

/* The recordable control the strip's knob was just turned for (MOTION target), or 0. */
FOCUS_SMALL static unsigned focus_motion_target(const struct panel *panel)
{
    return motion.turned_target && motion.turned_frames == panel->strip_frame ? motion.turned_target : 0;
}
#endif

/* ---------- which control, which picture ---------- */

enum focus_picture {
    PIC_NONE, PIC_LEVEL, PIC_CUTOFF, PIC_RES, PIC_ENV, PIC_PULSE, PIC_DECAY_101, PIC_LFO, PIC_GLIDE,
    PIC_TRACK, PIC_PWM, PIC_VCF_LFO, PIC_VIBRATO, PIC_FOOTAGE, PIC_TEMPO, PIC_DRIVE, PIC_DELAY,
    PIC_REVERB, PIC_TILES, PIC_TUNER, PIC_SCALE, PIC_CHANNEL, PIC_DECAY, PIC_SAWSQ, PIC_GATE,
    PIC_SWING, PIC_LOOP, PIC_ADSR_STAGE, PIC_SLIDE, PIC_MOTION
};

struct focus_card {
    unsigned picture, tiles, selected;
    const char *label, *unit, *note, *const *tile_label;
    int value, a, b; /* value shown; a, b: picture arguments */
    char text[12];   /* value as text when not numeric */
};

FOCUS_SMALL static int focus_telegram(const tg_controls *sound, unsigned control, struct focus_card *c)
{
    c->value = (int)control_value(sound, control);
    switch (control) {
    case CTRL_LEVEL: c->picture = PIC_LEVEL; c->label = "LEVEL"; c->note = "OUTPUT LEVEL"; return 1;
    case TG_CUTOFF: c->picture = PIC_CUTOFF; c->label = "CUTOFF"; c->note = "HIGHER = MORE HARMONICS"; return 1;
    case TG_RESONANCE: c->picture = PIC_RES; c->label = "RESONANCE"; c->note = "PEAK AT THE CUTOFF"; return 1;
    case TG_ENV: c->picture = PIC_ENV; c->label = "ENV MOD";
        c->note = sound->patch.filter_env_mode == TG_FILTER_ENV_CLOSE ? "HOW FAR EACH NOTE CLOSES IT" : "HOW FAR EACH NOTE OPENS IT"; return 1;
    case TG_WIDTH: c->picture = PIC_PULSE; c->label = "PULSE WIDTH"; c->note = "NARROWER = THINNER"; return 1;
    case TG_DECAY: c->picture = PIC_DECAY_101; c->label = "DECAY"; c->note = "HOW FAST IT FALLS"; return 1;
    case TG_ATTACK: case TG_SUSTAIN: case TG_RELEASE:
        c->picture = PIC_ADSR_STAGE; c->a = control == TG_ATTACK ? 0 : control == TG_SUSTAIN ? 2 : 3;
        c->label = control == TG_ATTACK ? "ATTACK" : control == TG_SUSTAIN ? "SUSTAIN" : "RELEASE"; return 1;
    case TG_LFO_RATE: c->picture = PIC_LFO; c->label = "LFO RATE"; c->note = "HOW FAST IT MOVES"; return 1;
    case TG_GLIDE: c->picture = PIC_GLIDE; c->label = "GLIDE"; c->note = "SLIDE BETWEEN NOTES"; return 1;
    case TG_TRACK: c->picture = PIC_TRACK; c->label = "KYBD TRACK"; return 1;
    case TG_PWM: c->picture = PIC_PWM; c->label = "PWM DEPTH"; c->note = "THE EDGE BREATHES"; return 1;
    case TG_FILTER_LFO: c->picture = PIC_VCF_LFO; c->label = "VCF LFO"; c->note = "LFO MOVES THE CUTOFF"; return 1;
    case TG_VIBRATO: c->picture = PIC_VIBRATO; c->label = "VIBRATO"; c->note = "LFO MOVES THE PITCH"; return 1;
    case CTRL_OSC_OCTAVE: {
        static const char *const octaves[] = {"-2", "-1", "0", "+1", "+2"};
        c->picture = PIC_TILES; c->label = "OSC OCTAVE"; c->tile_label = octaves; c->tiles = 5;
        c->selected = (unsigned)focus_clamp(c->value, 0, 4); c->value = -1; c->note = "SHIFTS THE OSCILLATOR"; return 1;
    }
    case CTRL_TEMPO: c->picture = PIC_TEMPO; c->label = "TEMPO"; c->unit = "BPM"; return 1;
    case CTRL_DRIVE: c->picture = PIC_DRIVE; c->label = "DRIVE"; c->note = "SQUARES OFF THE PEAKS"; return 1;
    case TG_DELAY: c->picture = PIC_DELAY; c->label = "DELAY"; c->note = "ECHOES ON THE BEAT"; return 1;
    case TG_REVERB: c->picture = PIC_REVERB; c->label = "REVERB"; c->note = "SIZE OF THE ROOM"; return 1;
    }
    return 0;
}

FOCUS_SMALL static void focus_copy_label(char *destination, const char *label)
{
    while (*label) *destination++ = *label++;
    *destination = 0;
}

FOCUS_SMALL static int focus_lookup(const struct panel *panel, unsigned k, struct focus_card *c)
{
    static const char *const sub_modes[] = {"-1 SQ", "-2 SQ", "-2 PULSE"};
    static const char *const triggers[] = {"GATE+TRIG", "GATE", "LFO"};
    static const char *const vca[] = {"ENV", "GATE"};
    static const char *const pwm_sources[] = {"MAN", "LFO", "ENV", "S+H", "SUB"};
    static const char *const lfo_waves[] = {"TRI", "SQUARE", "RANDOM", "NOISE"};
    static const char *const env_modes[] = {"SHARED", "OPEN", "CLOSE"};
    static const char *const glide_modes[] = {"OFF", "AUTO", "ON"};
    static const char *const off_on[] = {"OFF", "ON"};
    static const char *const clocks[] = {"INTERNAL", "USB MIDI"};
    static const char *const pedals[] = {"OFF", "PULLS LOW", "PULLS HIGH"};
    static const char *const profiles[] = {"1", "2", "3", "4", "5", "6", "7", "8"};
    static const char *const keys_focus[] = {"TEL", "DILLO"};
    const tg_controls *sound = &panel->sound;
    const tg_patch *patch = &sound->patch;
    unsigned page = panel->page;
    memset(c, 0, sizeof *c);
#define TILES(name, set, n, sel) do { c->picture = PIC_TILES; c->label = name; c->tile_label = set; \
    c->tiles = n; c->selected = sel; c->value = -1; } while (0)

    if (page == PAGE_CONDUCTOR && !panel->drums_repeat) {
        if (panel->conductor_seq_pending || panel->draft_view) return 0;
        if (k == 6 && sound->clock.source != TG_CLOCK_USB) return focus_telegram(sound, CTRL_TEMPO, c);
        if (k == 7) { TILES("KEYS", keys_focus, 2, panel->conductor_dillo_keys); c->note = "THE 37 KEYS PLAY THIS"; return 1; }
        return 0;
    }
    if (page == PAGE_DILLO_SOUND) {
        const bb_dillo_patch *tone = &panel->dillo_sound.tone;
        const dillo_decay *decay = &panel->dillo_sound.decay;
        if (panel->dillo_sound_page) {
            if (k == 0) { c->picture = PIC_ADSR_STAGE; c->label = "ATTACK"; c->unit = "MS"; c->value = tone->attack_ms;
                          c->a = 0; c->b = tone->attack_ms * 127 / 255 + 1; c->note = "HOW FAST THE NOTE OPENS"; return 1; }
            if (k == 1) { c->picture = PIC_TRACK; c->label = "KYBD TRACK"; c->value = tone->tracking; c->a = tone->tracking * 127 / 100; return 1; }
            if (k == 2) { c->picture = PIC_DECAY; c->label = "ACCENT DECAY"; c->value = decay->accent ? decay->accent : -1;
                          if (!decay->accent) memcpy(c->text, "STOCK", 6);
                          c->a = decay->accent ? decay->accent : DILLO_DECAY_MAX / 8; c->b = DILLO_DECAY_MAX; c->note = "ACCENTED NOTES"; return 1; }
            if (k == 3) { c->picture = PIC_DECAY; c->label = "AMP DECAY"; c->value = decay->amp ? decay->amp : -1;
                          if (!decay->amp) memcpy(c->text, "STOCK", 6);
                          c->a = decay->amp ? decay->amp : DILLO_DECAY_MAX; c->b = DILLO_DECAY_MAX; c->note = "WHILE THE GATE IS OPEN"; return 1; }
            if (k == 4) {
                static const char *const sweeps[] = {"STOCK", "LAG", "LONG"};
                TILES("ACC SWEEP", sweeps, 3, decay->sweep); c->note = "HOW REPEATED ACCENTS BUILD"; return 1;
            }
            return 0;
        }
        if (k == 5) { c->picture = PIC_SLIDE; c->label = "SLIDE TIME"; c->unit = "MS"; c->value = tone->slide_ms; c->a = tone->slide_ms; return 1; }
        if (k == 6) { c->picture = PIC_SAWSQ; c->label = "WAVEFORM"; c->value = -1; c->a = !tone->wave;
                      memcpy(c->text, tone->wave ? "SAW" : "SQUARE", tone->wave ? 4 : 7); return 1; }
        if (k == 7) { c->picture = PIC_DRIVE; c->label = "DRIVE"; c->value = tone->drive; c->a = tone->drive * 127 / 100; return 1; }
        return 0; /* K1-K5 draw on the squelch contour in place */
    }
    if (page == PAGE_DILLO) {
        dillo_step step = panel->dillo_pattern.steps[panel->dillo_cursor];
        if (k == 0) return focus_telegram(sound, CTRL_LEVEL, c);
        if (k == 5 && step.event != DILLO_REST && panel->dillo_record < DILLO_REC_PITCH) {
            c->picture = PIC_GATE; c->label = "GATE"; c->unit = "%"; c->value = step.gate_percent; c->a = step.gate_percent; return 1;
        }
        return 0;
    }
    if (page == PAGE_DRUMS || page == PAGE_CONDUCTOR || page == PAGE_MIX || page == PAGE_SCENES || page == PAGE_KITS ||
        page == PAGE_DRUM_PRESETS || page == PAGE_INSTRUMENTS || page == PAGE_SYNTH_PRESETS) {
        return 0; /* kit, roll, mixer, jukebox: in place */
    }
    if (panel->repeat_edit || panel->save_mode || page == PAGE_PATCH || page == PAGE_ASSIGN) return 0;
    if (page == PAGE_ARP) {
        if (k == 4) { c->picture = PIC_SWING; c->label = "SWING"; c->value = 50 + sound->arp_swing; c->unit = "%"; c->a = sound->arp_swing; return 1; }
        if (k == 6 && sound->clock.source == TG_CLOCK_INTERNAL) {
            c->picture = PIC_TEMPO; c->label = "ARP TEMPO"; c->unit = "BPM"; c->value = (int)tg_arp_tempo(sound); return 1;
        }
        if (k == 7) { TILES("CLOCK", clocks, 2, sound->clock.source == TG_CLOCK_USB); return 1; }
        return 0; /* mode, rate, gate, octaves, latch: the arp staircase in place */
    }
    if (page == PAGE_FILTER_ENV) {
        if (k == 0) { TILES("FILTER ENV", env_modes, 3, patch->filter_env_mode); return 1; }
        if (k == 1) { c->picture = PIC_ENV; c->label = "SWEEP"; c->value = patch->value[TG_ENV]; c->note = "HOW FAR THE FILTER ENV MOVES"; return 1; }
        return 0;
    }
    if (page == PAGE_GLOBAL) {
        if (k == 0 || k == 1) {
            const char *const *roots = view_root_names;
            const char *const *scales = view_scale_names;
            _Static_assert(sizeof view_scale_names / sizeof view_scale_names[0] == sizeof scale_intervals / sizeof scale_intervals[0],
                           "scale names follow scale_intervals");
            const char *name = k ? scales[panel->scale_type] : roots[panel->scale_root];
            c->picture = PIC_SCALE; c->label = k ? "SCALE" : "SCALE ROOT"; c->value = -1;
            focus_copy_label(c->text, name);
            c->a = panel->scale_root; c->b = panel->scale_type; return 1;
        }
        if (k == 2 || k == 3) {
            c->picture = PIC_CHANNEL; c->label = k == 2 ? "KEY CHANNEL" : "PAD CHANNEL";
            c->a = k == 2 ? panel->midi_channel : panel->pad_midi_channel; c->value = c->a + 1;
            c->note = k == 2 ? "KEYS SEND AND RECEIVE HERE" : "NOTE PADS SEND HERE"; return 1;
        }
        if (k == 4) { static const char *const routes[] = {"OFF", "USB", "TRS", "BOTH"}; TILES("MIDI OUT", routes, 4, panel->midi_tx); return 1; }
        if (k == 7) { TILES("CLOCK OUT", off_on, 2, panel->clock_out); return 1; }
        if (k == 5) { TILES("PROFILE", profiles, 8, panel->profile); return 1; }
        if (k == 6) { TILES("PEDAL", pedals, 3, panel->pedal_mode); return 1; }
        return 0;
    }
    if (page == PAGE_FX) {
        if (k < sizeof fx_map) return focus_telegram(sound, fx_map[k], c);
        if (k == 3) { TILES("GLIDE MODE", glide_modes, 3, patch->glide_mode); return 1; }
        if (k == 7) return focus_telegram(sound, TG_GLIDE, c);
        return 0;
    }
    if (page == PAGE_STEPS) {
        if (k == 5) { c->picture = PIC_GATE; c->label = "GATE"; c->unit = "%"; c->value = (int)tg_phrase_gate(sound); c->a = c->value; return 1; }
        if (k == 6) { c->picture = PIC_LOOP; c->label = "LENGTH"; c->unit = "STEPS"; c->value = (int)tg_pattern_length(sound); c->a = c->value; return 1; }
        if (k == 7) return 0; /* pitch or cursor: the step grid in place */
    }
    if (page == PAGE_MANUAL) {
        if (panel->fader_bank == 0 && k == 7) { TILES("SUB WAVE", sub_modes, 3, patch->sub_mode); return 1; }
        if (panel->fader_bank == 2 && k == 6) { TILES("TRIGGER", triggers, 3, patch->trigger); return 1; }
        if (panel->fader_bank == 2 && k == 7) { TILES("VCA", vca, 2, patch->amp_gate); return 1; }
        if (panel->fader_bank == 3) {
            if (k == 3) { c->picture = PIC_TUNER; c->label = "FINE TUNE"; c->unit = "CENT"; c->value = sound->tune_cents; c->a = sound->tune_cents; return 1; }
            if (k == 4) return focus_telegram(sound, TG_VIBRATO, c);
            if (k == 5) { TILES("PWM SOURCE", pwm_sources, TG_PWM_SOURCE_COUNT, patch->pwm); return 1; }
            if (k == 6) { TILES("LFO WAVE", lfo_waves, 4, patch->lfo_wave); return 1; }
            if (k == 7) { c->picture = PIC_FOOTAGE; c->label = "VCO RANGE"; c->value = -1; c->a = patch->range;
                          static const char *const feet[] = {"16'", "8'", "4'", "2'"};
                          focus_copy_label(c->text, feet[patch->range & 3u]); return 1; }
        }
    }
    (void)off_on;
#undef TILES
    return focus_telegram(sound, panel->knob_map[panel->knob_bank][k], c);
}

/* A knob drawn as the hardware is: a ring of 32 positions over 270 degrees, the pointer at the
 * value, and for a moment after a turn a fading trail behind it so the screen feels the turn. */
static const int8_t focus_dial_point[32][2] = {{-6, -6}, {-7, -5}, {-8, -4}, {-9, -3}, {-9, -2}, {-9, 0}, {-9, 1}, {-9, 2}, {-8, 4}, {-8, 5}, {-7, 6}, {-6, 7}, {-5, 8}, {-3, 8}, {-2, 9}, {-1, 9}, {1, 9}, {2, 9}, {3, 8}, {5, 8}, {6, 7}, {7, 6}, {8, 5}, {8, 4}, {9, 2}, {9, 1}, {9, 0}, {9, -2}, {9, -3}, {8, -4}, {7, -5}, {6, -6}};

FOCUS_SMALL static void focus_dial(uint16_t *pixels, const struct view_livery *liv, int cx, int cy, int value, int max, uint32_t age)
{
    if (max <= 0) max = 127;
    int at = value < 0 ? 0 : value > max ? 31 : value * 31 / max;
    for (int i = 0; i < 32; i += 2)
        view_rect(pixels, cx + focus_dial_point[i][0], cy + focus_dial_point[i][1], 2, 2, liv->lamp);
    if (age < 90u) { /* the trail: three dots behind the pointer, fading */
        for (int t = 1; t <= 3; t++) {
            int j = at - t * (int)(3u - age / 30u);
            if (j < 0 || j > 31) continue;
            view_rect(pixels, cx + focus_dial_point[j][0], cy + focus_dial_point[j][1], 2, 2,
                      picnic_mix(liv->ground, liv->accent, (unsigned)(12 - t * 3)));
        }
    }
    for (int r = 0; r <= 7; r++) /* the pointer */
        view_rect(pixels, cx + focus_dial_point[at][0] * r / 9, cy + focus_dial_point[at][1] * r / 9, 2, 2, liv->accent);
    view_rect(pixels, cx + focus_dial_point[at][0] - 1, cy + focus_dial_point[at][1] - 1, 4, 4, liv->accent);
}

FOCUS_SMALL static void view_focus(uint16_t *pixels, const struct panel *panel)
{
    if (!panel->strip_knob || (uint32_t)(panel->view_frames - panel->strip_frame) >= CONTROL_FOCUS_FRAMES) return;
    unsigned k = panel->strip_knob - 1u;
    struct focus_card c;
    if (!focus_lookup(panel, k, &c) || c.picture == PIC_NONE) return;
    const struct view_livery *liv = view_livery(panel);
    const tg_controls *sound = &panel->sound;
    const tg_patch *patch = &sound->patch;
    uint32_t t = panel->view_frames / 10u; /* ~62 animation steps a second */

    view_rect(pixels, 0, FOCUS_Y, VIEW_W, FOCUS_H, liv->ground);
    char chip[] = {'K', (char)('1' + k), 0};
    view_rect(pixels, 6, 23, 18, 13, liv->accent);
    view_small(pixels, 9, 26, chip, liv->ground);
    view_picnic(pixels, 32, 24, c.label, &picnic_M, liv->ink);
    char number[12];
    const char *value = c.value >= 0 || c.picture == PIC_TUNER ? focus_number(number, c.value) :
                        c.tile_label ? c.tile_label[c.selected] : c.text;
    int width = view_picnic_width(value, &picnic_L);
    view_picnic(pixels, 234 - width, 44, value, &picnic_L, liv->accent);
    if (c.value >= 0 && c.picture != PIC_TILES)
        focus_dial(pixels, liv, 15, 56, c.value, c.picture == PIC_TEMPO ? 300 : 127,
                   panel->view_frames - panel->strip_frame);
    if (c.unit) view_small(pixels, 234 - view_small_width(c.unit), 70, c.unit, liv->ink);
    if (c.note && c.picture != PIC_CHANNEL) /* beside the value when it fits, else under it */
        view_small(pixels, 32, 32 + view_small_width(c.note) + 6 < 234 - width ? 46 : 74, c.note, liv->ink);

#ifdef ELITE_DRUMS808
    unsigned motion_target = focus_motion_target(panel);
    if (c.picture == PIC_MOTION || (panel->motion_state && motion_target && motion_target == panel->motion_target)) {
        pic_motion(pixels, liv, panel);
        return;
    }
    if (motion_target && !panel->motion_state) { /* this knob's movement can be recorded */
        const char *hint = "REC: RECORD A BAR";
        view_small(pixels, 234 - view_small_width(hint), 26, hint, liv->ink);
    }
#endif
    switch (c.picture) {
    case PIC_LEVEL:
        pic_wave(pixels, liv, patch, patch->value[TG_CUTOFF], c.value, PIC_X, PIC_W - 24);
        pic_meter(pixels, liv, c.value, t);
        break;
    case PIC_CUTOFF: case PIC_RES: pic_filter(pixels, liv, patch, 0); break;
    case PIC_ENV: pic_filter(pixels, liv, patch, 1); break;
    case PIC_PULSE: pic_pulse(pixels, liv, patch->value[TG_WIDTH], 0, 0, t); break;
    case PIC_PWM: pic_pulse(pixels, liv, patch->value[TG_WIDTH], patch->value[TG_PWM], patch->pwm, t); break;
    case PIC_DECAY_101:
        pic_adsr(pixels, liv, patch->value[TG_ATTACK], patch->value[TG_DECAY], patch->value[TG_SUSTAIN],
                 patch->value[TG_RELEASE], 1, 1);
        break;
    case PIC_ADSR_STAGE:
        if (c.b) pic_adsr(pixels, liv, c.b - 1, 60, 0, 12, 0, 0); /* Dillo: attack into the 303 decay */
        else pic_adsr(pixels, liv, patch->value[TG_ATTACK], patch->value[TG_DECAY], patch->value[TG_SUSTAIN],
                      patch->value[TG_RELEASE], c.a, 1);
        break;
    case PIC_LFO: pic_lfo(pixels, liv, patch->value[TG_LFO_RATE], patch->lfo_wave, 127, t); break;
    case PIC_VCF_LFO: pic_lfo(pixels, liv, patch->value[TG_LFO_RATE], patch->lfo_wave, c.value, t); break;
    case PIC_GLIDE: pic_glide(pixels, liv, c.b ? c.a : c.value, c.b ? c.b : 127, t); break;
    case PIC_TRACK: pic_keybed_track(pixels, liv, c.a ? c.a : c.value); break;
    case PIC_VIBRATO: pic_vibrato(pixels, liv, c.value, patch->value[TG_LFO_RATE], t); break;
    case PIC_FOOTAGE: pic_footage(pixels, liv, (unsigned)patch->range & 3u); break;
    case PIC_TEMPO: pic_tempo(pixels, liv, c.value, panel->view_frames); break;
    case PIC_DRIVE: pic_drive(pixels, liv, c.a ? c.a : c.value); break;
    case PIC_DELAY: pic_delay(pixels, liv, c.value, t); break;
    case PIC_REVERB: pic_reverb(pixels, liv, c.value, t); break;
    case PIC_TILES: pic_tiles(pixels, liv, c.tile_label, c.tiles, c.selected); break;
    case PIC_TUNER: pic_tuner(pixels, liv, c.a); break;
    case PIC_SCALE: pic_scale(pixels, liv, (unsigned)c.a, (unsigned)c.b); break;
    case PIC_CHANNEL: pic_channels(pixels, liv, (unsigned)c.a, c.note); break;
    case PIC_DECAY: pic_decay(pixels, liv, c.a, c.b, 0); break;
    case PIC_SAWSQ: pic_saw_square(pixels, liv, (unsigned)c.a); break;
    case PIC_GATE: pic_gate(pixels, liv, c.a); break;
    case PIC_SWING: pic_swing(pixels, liv, c.a); break;
    case PIC_LOOP: pic_loop(pixels, liv, c.a, TG_STEPS); break;
    case PIC_MOTION: break; /* drawn above */
    case PIC_SLIDE: pic_slide(pixels, liv, &panel->dillo_pattern, panel->dillo_cursor, c.a, sound->tempo); break;
    }
}
