/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared transport header for the combined Elite instrument. */
#define VIEW_RGB(r, g, b) (((r) >> 3) << 11 | ((g) >> 2) << 5 | ((b) >> 3))
enum {
    CHROME_BAND = VIEW_RGB(34, 29, 26), CHROME_CREAM = VIEW_RGB(243, 234, 215),
    CHROME_RED = VIEW_RGB(216, 53, 42), CHROME_TEL = VIEW_RGB(46, 41, 37),
    CHROME_ELITE = VIEW_RGB(255, 127, 39), CHROME_BLUE = CHROME_ELITE, CHROME_303 = VIEW_RGB(214, 207, 196),
    CHROME_303_INK = VIEW_RGB(27, 25, 22), CHROME_303_RED = VIEW_RGB(216, 38, 28),
    CHROME_808 = VIEW_RGB(14, 14, 14), CHROME_808_RED = VIEW_RGB(226, 64, 46),
    CHROME_808_ORANGE = VIEW_RGB(240, 138, 36), CHROME_808_YELLOW = VIEW_RGB(242, 194, 48),
    CHROME_808_WHITE = VIEW_RGB(239, 230, 207),
    HEADER_STOPPED = 4
};

#include "picnic_aa.inc"

/* Taylor Rushing's Picnic Filled, anti-aliased: each glyph pixel blends the ink into
 * what is already drawn, by its 4-bit coverage. y is the top of the capitals. */
static uint16_t picnic_mix(uint16_t bg, uint16_t ink, unsigned alpha)
{
    unsigned r = ((bg >> 11) * (15u - alpha) + (ink >> 11) * alpha + 7u) / 15u;
    unsigned g = (((bg >> 5) & 63u) * (15u - alpha) + ((ink >> 5) & 63u) * alpha + 7u) / 15u;
    unsigned b = ((bg & 31u) * (15u - alpha) + (ink & 31u) * alpha + 7u) / 15u;
    return (uint16_t)(r << 11 | g << 5 | b);
}

static int view_picnic(uint16_t *pixels, int x, int y, const char *text, const picnic_face *face, uint16_t ink)
{
    for (; *text; text++) {
        unsigned c = (unsigned char)*text;
        if (c < face->first || c >= face->first + face->count) c = '?';
        const picnic_glyph *g = &face->glyph[c - face->first];
        if (!g->advance && c != '?') g = &face->glyph['?' - face->first];
        if (x + g->advance > VIEW_W) break;
        for (int row = 0; row < g->height; row++) {
            int py = y + g->top + row;
            if (py < 0 || py >= VIEW_H) continue;
            for (int col = 0; col < g->width; col++) {
                int px = x + g->left + col;
                if (px < 0 || px >= VIEW_W) continue;
                unsigned i = (unsigned)(row * g->width + col);
                unsigned byte = face->bits[g->offset + i / 2u];
                unsigned alpha = i & 1u ? byte & 15u : byte >> 4;
                if (!alpha) continue;
                uint16_t *p = &pixels[py * VIEW_W + px];
                *p = alpha == 15u ? ink : picnic_mix(*p, ink, alpha);
            }
        }
        x += g->advance;
    }
    return x;
}

static int view_picnic_width(const char *text, const picnic_face *face)
{
    int width = 0;
    for (; *text; text++) {
        unsigned c = (unsigned char)*text;
        if (c >= face->first && c < face->first + face->count) width += face->glyph[c - face->first].advance;
    }
    return width;
}

/* The compact labels of the control cells and keycaps, now in Picnic. */
static int view_small(uint16_t *pixels, int x, int y, const char *label, uint16_t color)
{
    return view_picnic(pixels, x, y, label, &picnic_S, color);
}

static int view_small_num(uint16_t *pixels, int x, int y, unsigned value,
                          unsigned width, uint16_t color)
{
    char digits[6] = {0};
    for (unsigned i = width; i; i--) {
        digits[i - 1u] = (char)('0' + value % 10u);
        value /= 10u;
    }
    return view_small(pixels, x, y, digits, color);
}

static int view_small_width(const char *label)
{
    return view_picnic_width(label, &picnic_S);
}

static unsigned strip_scale(int value, int low, int high)
{
    if (high <= low) return 0;
    if (value < low) value = low;
    if (value > high) value = high;
    return (unsigned)((value - low) * 127 / (high - low));
}

/* Octaves above 20 Hz, so the Dillo cutoff arc moves evenly across 20 Hz-16 kHz. */
static unsigned strip_hz(unsigned hz)
{
    unsigned octaves = 0, base = 20;
    while (hz >= base * 2u && octaves < 10) { base *= 2u; octaves++; }
    unsigned fraction = hz > base ? (hz - base) * 127u / base : 0;
    return strip_scale((int)(octaves * 127u + fraction), 0, 1230);
}

/* One cycle of sine, 64 steps, +/-127: shared by the 808 voices and the focus pictures. */
static const int8_t view_sin64[64] = {
    0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126, 127, 126, 125, 122, 117, 112,
    106, 98, 90, 81, 71, 60, 49, 37, 25, 12, 0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106,
    -112, -117, -122, -125, -126, -127, -126, -125, -122, -117, -112, -106, -98, -90, -81, -71, -60,
    -49, -37, -25, -12
};

struct view_livery {
    uint16_t ground, ink, accent, chip, chip_ink, lamp, shell, stripe, skin;
};

static const struct view_livery *view_livery(const struct panel *panel)
{
    static const struct view_livery band = {
        CHROME_BAND, CHROME_CREAM, CHROME_RED, CHROME_CREAM, CHROME_BAND,
        EDGE, CHROME_CREAM, CHROME_RED, CHROME_CREAM
    };
    static const struct view_livery telegram = {
        CHROME_TEL, INK, CHROME_BLUE, CHROME_BLUE, CHROME_TEL,
        EDGE, CHROME_CREAM, CHROME_RED, CHROME_CREAM
    };
    static const struct view_livery dillo = {
        CHROME_303, CHROME_303_INK, CHROME_303_RED, CHROME_303_INK, CHROME_303,
        DIM, CHROME_303_INK, DIM, CHROME_303_INK
    };
    static const struct view_livery drums = {
        CHROME_808, CHROME_808_WHITE, CHROME_808_ORANGE, CHROME_808_ORANGE, CHROME_808,
        EDGE, CHROME_CREAM, CHROME_RED, CHROME_CREAM
    };
    switch (panel->page) {
    case PAGE_CONDUCTOR: return panel->drums_repeat ? &drums : &band;
    case PAGE_MIX: case PAGE_SCENES: case PAGE_INSTRUMENTS: return &band;
    case PAGE_DILLO: case PAGE_DILLO_SOUND: return &dillo;
    case PAGE_DRUM_PRESETS: return panel->rhythm_source == RHYTHM_BAND ? &band : &drums;
    case PAGE_DRUMS: case PAGE_KITS: return &drums;
    default: return &telegram;
    }
}

static unsigned view_header_beat(const struct panel *panel, unsigned rhythm_head)
{
    const tg_controls *sound = &panel->sound;
    unsigned running = sound->clock.source == TG_CLOCK_USB ?
                       sound->clock.state == TG_CLOCK_RUNNING : sound->running;
    return running && rhythm_head < DRUM_STEPS ? rhythm_head / 4u : HEADER_STOPPED;
}

/* ---- The Dillo: Texas Radio's armadillo, from the brand art (tools/dillo_art.py) ---- */
#include "dillo_art.inc"

static unsigned dillo_alpha(const uint8_t *data, unsigned bits, unsigned width, unsigned x, unsigned y)
{
    unsigned i = y * width + x, per = 8u / bits;
    unsigned byte = data[i / per], shift = 8u - bits * (i % per + 1u);
    return (byte >> shift) & ((1u << bits) - 1u);
}

/* Draw a Dillo mask with its alpha scaled to 0..max (of 15), clipped to rows y0..y1. When only
 * is set, paint only over pixels of that colour, so the Dillo stays behind everything drawn. */
static void view_dillo_mask(uint16_t *pixels, int x0, int y0, const uint8_t *data, const uint8_t (*span)[2],
                            unsigned w, unsigned h, unsigned bits, uint16_t ink, unsigned max, int clip0, int clip1,
                            int only, uint16_t ground)
{
    unsigned levels = (1u << bits) - 1u;
    for (unsigned y = 0; y < h; y++) {
        int py = y0 + (int)y;
        if (py < clip0 || py >= clip1 || py < 0 || py >= VIEW_H) continue;
        for (unsigned x = span[y][0]; x < span[y][1]; x++) {
            int px = x0 + (int)x;
            if (px < 0 || px >= VIEW_W) continue;
            unsigned a = dillo_alpha(data, bits, w, x, y);
            if (!a) continue;
            uint16_t *p = &pixels[py * VIEW_W + px];
            if (only && *p != ground) continue;
            *p = picnic_mix(*p, ink, (a * max + levels / 2u) / levels);
        }
    }
}

static uint16_t view_stage_ground(const uint16_t *pixels)
{
    /* The page's own ground: the colour shared by at least two of three quiet corners of the stage. */
    uint16_t a = pixels[182 * VIEW_W + 2], b = pixels[182 * VIEW_W + 120], c = pixels[181 * VIEW_W + 60];
    return a == b || a == c ? a : b == c ? b : 0;
}

/* Behind every page: the Dillo, large and faint, in the page's own ink. */
static void view_dillo_wallpaper(uint16_t *pixels, const struct view_livery *liv)
{
#ifdef ELITE_NO_DILLO_WALLPAPER /* timing fallback: the header mark and playhead rider stay */
    (void)pixels; (void)liv;
    return;
#endif
    uint16_t ground = view_stage_ground(pixels);
    if (!ground && pixels[182 * VIEW_W + 2]) return;
    view_dillo_mask(pixels, VIEW_W - DILLO_WALL_W - 4, 26, dillo_wall, dillo_wall_span, DILLO_WALL_W, DILLO_WALL_H, DILLO_WALL_BITS,
                    liv->ink, 3u, 19, 184, 1, ground);
}

static void view_header_motion(uint16_t *pixels, const struct panel *panel, unsigned beat)
{
    const struct view_livery *liv = view_livery(panel);
    for (unsigned i = 0; i < HEADER_STOPPED; i++) {
        uint16_t color = beat == i ? (i == 0 ? liv->accent : liv->ink) : liv->lamp;
        view_rect(pixels, 121 + i * 6, 7, 4, 4, color);
    }

    /* The Dillo in the header: still when stopped, stepping a pixel on each beat while playing.
     * Both frames stay inside one LCD strip. */
    view_rect(pixels, 212, 0, 28, VIEW_STRIP, liv->ground);
    unsigned stopped = beat == HEADER_STOPPED;
    view_dillo_mask(pixels, 220, stopped ? 1 : (int)(beat & 1u), dillo_icon, dillo_icon_span, DILLO_ICON_W, DILLO_ICON_H, DILLO_ICON_BITS,
                    stopped ? liv->shell : liv->ink, 15u, 0, VIEW_STRIP, 0, 0);
}

static void view_header(uint16_t *pixels, const struct panel *panel, const char *title,
                        const char *activity, unsigned beat)
{
    const struct view_livery *liv = view_livery(panel);
    view_rect(pixels, 0, 0, VIEW_W, 18, liv->ground);
    view_rect(pixels, 3, 1, 117, 15, liv->chip);
    const picnic_face *title_face = view_picnic_width(title, &picnic_M) <= 108 ? &picnic_M : &picnic_S;
    view_picnic(pixels, 7, title_face == &picnic_M ? 3 : 5, title, title_face, liv->chip_ink);
    unsigned tempo = control_value(&panel->sound, CTRL_TEMPO);
    char bpm[4] = {(char)('0' + tempo / 100u % 10u), (char)('0' + tempo / 10u % 10u),
                   (char)('0' + tempo % 10u), 0};
    view_picnic(pixels, 147, 3, tempo ? bpm + (tempo < 100u) + (tempo < 10u) : "---", &picnic_M, liv->ink);
    view_picnic(pixels, 178, 5, activity, &picnic_S, liv->ink);
    if (panel->sound.clock.source == TG_CLOCK_USB)
        view_rect(pixels, 147, 15, 26, 2, liv->accent);
    view_rect(pixels, 0, 17, VIEW_W, 1, liv->lamp);
    view_header_motion(pixels, panel, beat);
}

static int view_keycap(uint16_t *pixels, int x, const char *key, const char *action,
                       const struct view_livery *liv)
{
    int w = view_small_width(key) + 4;
    view_rect(pixels, x + 1, 224, w - 2, 12, liv->chip);
    view_rect(pixels, x, 225, w, 10, liv->chip);
    view_small(pixels, x + 2, 227, key, liv->chip_ink);
    return view_small(pixels, x + w + 3, 227, action, liv->ink) + 6;
}

static void view_knob_cell(uint16_t *pixels, unsigned slot, const char *label, unsigned value,
                           const struct view_livery *liv)
{
    /* 270-degree arc, clockwise from the lower-left stop. */
    static const int8_t arc[25][2] = {
        {-6, 6},
        {-7, 4},
        {-7, 3},
        {-8, 2},
        {-8, 0},
        {-8, -2},
        {-7, -3},
        {-7, -4},
        {-6, -6},
        {-4, -7},
        {-3, -7},
        {-2, -8},
        {0, -8},
        {2, -8},
        {3, -7},
        {4, -7},
        {6, -6},
        {7, -4},
        {7, -3},
        {8, -2},
        {8, 0},
        {8, 2},
        {7, 3},
        {7, 4},
        {6, 6},
    };
    int x = 30 * slot;
    if (slot) view_rect(pixels, x, 188, 1, 28, liv->lamp);
    if (!label) {
        char key[] = {'K', (char)('1' + slot), 0};
        view_small(pixels, x + 3, 187, key, liv->lamp);
        view_small(pixels, x + 13, 198, "-", liv->lamp);
        return;
    }
    unsigned selected = value * 24u / 127u;
    for (unsigned i = 0; i < 25; i++)
        view_rect(pixels, x + 15 + arc[i][0], 197 + arc[i][1], 1, 1,
                  i <= selected ? liv->ink : liv->lamp);
    for (int i = 0; i <= 6; i++)
        view_rect(pixels, x + 15 + i * arc[selected][0] / 8, 197 + i * arc[selected][1] / 8,
                  1, 1, liv->ink);
    char fit[8];
    unsigned n = 0;
    while (label[n] && n < sizeof fit - 1u) { fit[n] = label[n]; n++; }
    fit[n] = 0;
    while (n > 1u && view_small_width(fit) > 29) fit[--n] = 0; /* trim inside the 30 px cell */
    view_small(pixels, x + (31 - view_small_width(fit)) / 2, 209, fit, liv->ink);
}

/* ---- Preset jukebox helpers, shared by every Library list ---- */
static void jukebox_number(char out[6], unsigned n)
{
    char digits[6];
    unsigned len = 0;
    do { digits[len++] = (char)('0' + n % 10u); n /= 10u; } while (n && len < 5u);
    while (len < 2u) digits[len++] = '0';
    for (unsigned i = 0; i < len; i++) out[i] = digits[len - 1u - i];
    out[len] = 0;
}

/* The biggest Picnic face the text fits in, from Large down. Legibility first: the screen is
 * read from a stand, so every card leads with one big name. */
/* Picnic's lower case is small capitals, and Large carries only capitals: draw names upper case. */
static const char *jukebox_caps(const char *text, char out[32])
{
    unsigned i = 0;
    for (; text[i] && i < 31u; i++) out[i] = text[i] >= 'a' && text[i] <= 'z' ? (char)(text[i] - 32) : text[i];
    out[i] = 0;
    return out;
}

static const picnic_face *jukebox_face(const char *text, int width)
{
    if (view_picnic_width(text, &picnic_L) <= width) return &picnic_L;
    if (view_picnic_width(text, &picnic_M) <= width) return &picnic_M;
    return &picnic_S;
}

static void jukebox_big(uint16_t *pixels, int x, int y, int width, const char *name, uint16_t ink)
{
    char caps[32];
    const char *text = jukebox_caps(name, caps);
    const picnic_face *face = jukebox_face(text, width);
    int dy = face == &picnic_L ? 0 : face == &picnic_M ? 8 : 12;
    view_picnic(pixels, x, y + dy, text, face, ink);
}

/* The keyboard's 37 keys, the pitch classes in use lit orange and the root red. */
static void jukebox_keys(uint16_t *pixels, int y, int height, uint16_t used, unsigned root, uint16_t stage)
{
    static const uint8_t white_pc[7] = {0, 2, 4, 5, 7, 9, 11};
    for (unsigned pass = 0; pass < 2; pass++)
        for (unsigned k = 0, white = 0; k < 37; k++) {
            unsigned pc = k % 12u, is_white = 0, on = used >> pc & 1u, is_root = on && pc == root;
            for (unsigned i = 0; i < 7; i++) is_white |= white_pc[i] == pc;
            int x = 10 + (int)white * 10;
            if (is_white) {
                if (!pass) view_rect(pixels, x, y, 9, height, is_root ? CHROME_RED : on ? CHROME_808_ORANGE : VIEW_RGB(232, 220, 203));
                white++;
            } else if (pass) {
                view_rect(pixels, x - 4, y, 7, height * 3 / 5, stage);
                view_rect(pixels, x - 3, y, 5, height * 3 / 5 - 1, is_root ? VIEW_RGB(168, 50, 29) : on ? VIEW_RGB(196, 95, 22) : stage);
            }
        }
}

static const char *const jukebox_pc_name[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/* ROOT and its note name, big, at the left of a how-to-play card; the tip at the right. */
static void jukebox_root_tip(uint16_t *pixels, int y, uint16_t used, unsigned root, const char *tip, uint16_t ink, uint16_t red)
{
    if (used) {
        int x = view_picnic(pixels, 8, y + 6, "ROOT", &picnic_M, DIM);
        view_picnic(pixels, x + 8, y, jukebox_pc_name[root], &picnic_L, red);
    }
    view_picnic(pixels, VIEW_W - 8 - view_picnic_width(tip, &picnic_S), y + 12, tip, &picnic_S, ink);
}

/* A MIDI note as text: C#3, A-1. */
static const char *jukebox_note_name(char out[6], unsigned pitch)
{
    unsigned at = 0;
    for (const char *c = jukebox_pc_name[pitch % 12u]; *c; c++) out[at++] = *c;
    int octave = (int)(pitch / 12u) - 1;
    if (octave < 0) { out[at++] = '-'; octave = -octave; }
    out[at++] = (char)('0' + octave % 10);
    out[at] = 0;
    return out;
}

/* A small coloured tag with Medium text; returns its left edge when drawn right-aligned at x. */
static int view_tag_left(uint16_t *pixels, int right, int y, const char *text, uint16_t fill, uint16_t ink)
{
    int w = view_picnic_width(text, &picnic_M) + 10;
    view_rect(pixels, right - w, y, w, 22, fill);
    view_picnic(pixels, right - w + 5, y + 2, text, &picnic_M, ink);
    return right - w;
}

/* A step or slot number big, after a Medium label: STEP 07. Returns the right edge. */
static int view_label_number(uint16_t *pixels, int x, int y, const char *label, unsigned n, uint16_t dim, uint16_t ink)
{
    if (label) x = view_picnic(pixels, x, y + 9, label, &picnic_M, dim) + 8;
    char text[4];
    unsigned at = 0;
    if (n >= 100u) text[at++] = (char)('0' + n / 100u);
    text[at++] = (char)('0' + n / 10u % 10u);
    text[at++] = (char)('0' + n % 10u);
    text[at] = 0;
    return view_picnic(pixels, x, y - 4, text, &picnic_L, ink);
}

/* The biggest face a status line fits in. */
static void view_status_line(uint16_t *pixels, int y, const char *text, uint16_t ink)
{
    const picnic_face *face = view_picnic_width(text, &picnic_M) <= VIEW_W - 16 ? &picnic_M : &picnic_S;
    view_picnic(pixels, 8, y + (face == &picnic_M ? 0 : 4), text, face, ink);
}
