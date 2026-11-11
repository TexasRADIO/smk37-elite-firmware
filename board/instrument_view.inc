/* SPDX-License-Identifier: GPL-3.0-only */
/* Native 240 x 240 layout. This renderer has no device I/O. */
enum {
    VIEW_W = 240, VIEW_H = 240, VIEW_STRIP = 12,
    /* Warm palette (SMK-37 Ultraman orange and grey): cream ink on warm charcoal. */
    INK = 0xf75a, PAPER = 0x18c2, CARD = 0x2944, EDGE = 0x4a07,
    DIM = 0xa4d1, LIME = 0xfc45, SKY = 0xedcf, CORAL = 0xfc8e
};

static void view_rect(uint16_t *pixels, int x, int y, int w, int h, uint16_t color)
{
    int left = x < 0 ? 0 : x;
    int top = y < 0 ? 0 : y;
    int right = x + w > VIEW_W ? VIEW_W : x + w;
    int bottom = y + h > VIEW_H ? VIEW_H : y + h;
    for (int row = top; row < bottom; row++)
        for (int col = left; col < right; col++)
            pixels[row * VIEW_W + col] = color;
}

static void view_text(uint16_t *pixels, int x, int y, const char *text, unsigned scale, uint16_t color)
{
    while (*text && x + (int)(8 * scale) <= VIEW_W) {
        unsigned c = (unsigned char)*text++;
        if (c < 32 || c > 126) c = '?';
        for (unsigned row = 0; row < 16; row++) {
            unsigned bits = font[c - 32u][row];
            for (unsigned col = 0; col < 8; col++)
                if (bits & (0x80u >> col))
                    view_rect(pixels, x + (int)(col * scale), y + (int)(row * scale), scale, scale, color);
        }
        x += (int)(8 * scale);
    }
}

/* Keep names clear of the live-note display; Library can use two lines. */
static void view_short_text(uint16_t *pixels, int x, int y, const char *text, unsigned limit, uint16_t color)
{
    char line[28];
    if (limit > 27) limit = 27;
    unsigned i = 0;
    for (; i < limit && text[i]; i++) line[i] = text[i];
    line[i] = 0;
    if (text[i] && i >= 3) line[i-1] = line[i-2] = line[i-3] = '.';
    view_text(pixels, x, y, line, 1, color);
}

static unsigned view_patch_name(uint16_t *pixels, const char *text)
{
    char line[28]; unsigned i = 0;
    for (; i < 27 && text[i]; i++) line[i] = text[i];
    line[i] = 0; view_text(pixels, 10, 162, line, 1, LIME);
    if (!text[i]) return 1;
    view_short_text(pixels, 10, 178, text + i, 27, LIME);
    return 2;
}

#include "picnic_title.inc"

static void view_title(uint16_t *pixels, int x, int y, const char *text, uint16_t color)
{
    for (; *text; text++) {
        unsigned c = (unsigned char)*text;
        if (c == ' ') { x += 5; continue; }
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        unsigned index = c >= 'A' && c <= 'Z' ? c - 'A' : c >= '0' && c <= '9' ? c - '0' + 26u : 36u;
        if (index >= 36) continue;
        const sw_rushing_glyph *glyph = &sw_rushing_glyphs[index];
        for (unsigned row = 0; row < SW_RUSHING_HEIGHT; row++)
            for (unsigned col = 0; col < glyph->width; col++) {
                unsigned bit = row * glyph->width + col;
                if (sw_rushing_bits[glyph->offset + bit / 8u] & (0x80u >> (bit % 8u)))
                    view_rect(pixels, x + (int)col, y + (int)row, 1, 1, color);
            }
        x += glyph->advance;
    }
}

static void view_num(uint16_t *pixels, int x, int y, unsigned value, unsigned width, uint16_t color)
{
    char digits[6] = {0};
    for (unsigned i = width; i; i--) {
        digits[i - 1u] = (char)('0' + value % 10u);
        value /= 10u;
    }
    view_text(pixels, x, y, digits, 1, color);
}

static void view_note(uint16_t *pixels, int x, int y, unsigned note, unsigned scale, uint16_t color)
{
    static const char *const names[] = {"C ", "C#", "D ", "D#", "E ", "F ", "F#", "G ", "G#", "A ", "A#", "B "};
    if (!note) {
        view_text(pixels, x, y, "--", scale, color);
        return;
    }
    if (note == TG_NOTE_ZERO) note = 0;
    char label[5] = "   ";
    const char *name = names[note % 12u];
    label[0] = name[0]; label[1] = name[1];
    if (note < 12) { label[2] = '-'; label[3] = '1'; }
    else label[2] = (char)('0' + note / 12u - 1u);
    view_text(pixels, x, y, label, scale, color);
}

#ifdef ELITE_DRUMS808
static const char *view_copy_prompt(const struct panel *panel)
{
    static const char *const message[] = {
        [MIX_TELEGRAM] = "COPY TEL ON CONDUCTOR",
        [MIX_DILLO] = "COPY DILLO ON CONDUCTOR",
        [MIX_808] = ELITE_DRUM_PARTS > 1 ? "COPY 808 A ON CONDUCTOR" : "COPY 808 ON BAND",
        [MIX_808_B] = "COPY 808 B ON CONDUCTOR"
    };
    return message[panel->conductor_copy_needed - 1u];
}

static void view_edit_pattern(uint16_t *pixels, const struct panel *panel)
{
    unsigned lane = conductor_edit_lane(panel);
    unsigned library = lane == MIX_TELEGRAM && panel->telegram_library;
    struct conductor_row_state state = conductor_lane_state(panel, lane);
    view_text(pixels, 10, 31, state.pending ? "EDIT" : "PATTERN", 1, DIM);
    unsigned x = state.pending ? 48 : 70;
    unsigned color = state.pending ? CORAL : INK;
    if (library) view_text(pixels, x, 31, "LIB", 1, color);
    else view_num(pixels, x, 31, state.queued + 1u, 2, color);
    if (state.pending && state.enabled) {
        view_text(pixels, 82, 31, "NOW", 1, DIM);
        if (lane == MIX_TELEGRAM && panel->telegram_library_active) view_text(pixels, 110, 31, "LIB", 1, LIME);
        else view_num(pixels, 110, 31, state.active + 1u, 2, LIME);
    } else if (!state.enabled) {
        view_text(pixels, state.pending ? 82 : 102, 31, "MUTE", 1, DIM);
    }
    if (state.pending) view_text(pixels, 150, 31,
                                 conductor_clock_lost(panel) ? "CLOCK LOST" :
                                 conductor_running(panel) ? "NEXT BAR" : "LOADING", 1, CORAL);
}
#endif

#ifdef ELITE_DRUMS808
static const char *view_conductor_save_status(const struct panel *panel)
{
    if (panel->conductor_save_state == CONDUCTOR_SAVE_NONE) return NULL;
    if (panel->library_status == LIB_UNAVAILABLE) return "STORAGE UNAVAILABLE";
    if (panel->conductor_save_state == CONDUCTOR_SAVE_FAILED) return "SAVE FAILED  REC RETRY";
    if (panel->library_status == LIB_WAIT_QUIET) return "WAITING FOR SILENCE";
    return panel->library_status == LIB_WAIT_STOP ? "STOP / RELEASE TO SAVE" : "SAVING";
}
#endif

#ifdef ELITE_DRUMS808
#include "view_chrome.inc"
#endif
#include "view_tel.inc"
#include "view_dillo.inc"

#ifdef ELITE_DRUMS808
#include "view_footer.inc"
#include "view_band.inc"
#include "view_808.inc"
#include "view_focus.inc"
#endif

static void draw_instrument_page(uint16_t *pixels, const struct panel *panel,
                                 unsigned head, unsigned note, unsigned fault, unsigned safe, unsigned beat)
{
    static const char *const rate_name[] = {"1/16", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T"};
    const tg_controls *sound = &panel->sound;
    view_rect(pixels, 0, 0, VIEW_W, VIEW_H, PAPER);
    const char *title = "TELEGRAM 101";
    const char *step_help = sound->running || tg_clock_active(&sound->clock) ?
                            "SEQ+PLAY CLOCK  BT HOME" : "HOLD PAD TO HEAR   BT HOME";
#ifdef ELITE_DILLO
    if (panel->page == PAGE_DILLO) title = "DILLO";
#ifndef ELITE_DRUMS808
    else if (panel->dillo_enabled) title = "TELEGRAM";
#endif
    step_help = "SEQ+PARA DILLO  BT HOME";
#endif
#ifdef ELITE_DRUMS808
    static const char *const tel_title[] = {
        [PAGE_TONE] = "TELEGRAM 101", [PAGE_MANUAL] = "101 SHAPE", [PAGE_STEPS] = "101 SEQUENCE",
        [PAGE_FILTER_ENV] = "101 FILTER ENV", [PAGE_FX] = "101 FX", [PAGE_ARP] = "101 ARP",
        [PAGE_PATCH] = "101 SOUNDS", [PAGE_GLOBAL] = "GLOBAL", [PAGE_ASSIGN] = "ASSIGN"
    };
    if (panel->page < sizeof tel_title / sizeof tel_title[0] && tel_title[panel->page])
        title = tel_title[panel->page];
    if (panel->page == PAGE_CONDUCTOR)
        title = panel->drums_repeat ? drum_part(panel) ? "ROLL 808 B" : ELITE_DRUM_PARTS > 1 ? "ROLL 808 A" : "ROLL 808" :
                panel->draft_view ? "DRAFTS" : "BAND";
    if (panel->page == PAGE_MIX) title = "MIXER";
    if (panel->page == PAGE_SCENES) title = "SETUPS";
    if (panel->page == PAGE_DILLO_SOUND) title = "DILLO SOUND";
    if (panel->page == PAGE_KITS) title = "SOUND KITS";
    if (panel->page == PAGE_DRUM_PRESETS)
        title = panel->rhythm_source == RHYTHM_BAND ? (rhythm_group(panel) ? rhythm_group(panel) : "BAND PRESETS") :
                panel->rhythm_source == RHYTHM_LAYER ? "727 LAYER" : "RHYTHM PRESETS";
#ifdef ELITE_SYNTH_PRESETS
    if (panel->page == PAGE_SYNTH_PRESETS)
        title = synth_preset_duets(panel) ? "ACID DUETS" : panel->synth_preset_dillo ? "DILLO PATTERNS" : "101 PATTERNS";
#endif
    if (panel->page == PAGE_INSTRUMENTS) title = "INSTRUMENTS";
    if (panel->page == PAGE_DRUMS) title = drum_part(panel) ? "DRUMS 808 B" : ELITE_DRUM_PARTS > 1 ? "DRUMS 808 A" : "DRUMS 808";
    if (panel->key_focus_notice) title = panel->conductor_dillo_keys ? "KEYS DILLO" : "KEYS 101";
    step_help = "SEQ+PARA DILLO  SEQ+FX 808";
#endif
    const char *activity = sound->clock.source == TG_CLOCK_USB && sound->clock.state == TG_CLOCK_LOST ? "LOST" :
                           sound->clock.source == TG_CLOCK_USB && sound->clock.state == TG_CLOCK_WAITING ? "WAIT" :
                           sound->clock.source == TG_CLOCK_USB && sound->clock.state == TG_CLOCK_STOPPED ? "STOP" :
                           sound->running ? "PLAY" : tg_arp_active(sound) ? "ARP" :
                           panel->hold && sound->live_note ? "HOLD" : "STOP";
#ifdef ELITE_DRUMS808
    view_header(pixels, panel, title, fault || safe ? "STOP" : activity,
                fault || safe ? HEADER_STOPPED : beat);
#else
    (void)beat;
    view_title(pixels, 10, 5, title, LIME);
    unsigned activity_color = sound->running || tg_arp_active(sound) ||
                              (panel->hold && sound->live_note) ? LIME : DIM;
    if (sound->clock.source == TG_CLOCK_USB && sound->clock.state != TG_CLOCK_RUNNING)
        activity_color = sound->clock.state == TG_CLOCK_LOST ? CORAL : DIM;
    view_text(pixels, 173, 6, activity, 1, activity_color);
#if defined(ELITE_DILLO) && !defined(ELITE_DRUMS808)
    if (panel->dillo_enabled && panel->page != PAGE_DILLO) {
        unsigned internal = sound->clock.source == TG_CLOCK_INTERNAL;
        view_text(pixels, 120, 6, internal ? "DILLO" : "DLO --", 1, internal ? LIME : DIM);
    }
#endif
    view_rect(pixels, 10, 25, 220, 1, EDGE);
#endif
    if (fault || safe) {
#ifdef ELITE_DRUMS808
        jukebox_big(pixels, 12, 54, VIEW_W - 24, safe ? "SAFE MODE" : "AUDIO STOPPED", CORAL);
        view_picnic(pixels, 12, 104, "RESTART THE ELITE", &picnic_M, INK);
#else
        view_text(pixels, 12, 54, safe ? "SAFE MODE" : "AUDIO STOPPED", 2, CORAL);
        view_text(pixels, 12, 104, "RESTART THE ELITE", 1, INK);
#endif
        return;
    }
#ifdef ELITE_DILLO
    if (panel->page == PAGE_DILLO) {
        view_dillo_steps(pixels, panel, head);
        return;
    }
#endif
#ifdef ELITE_DRUMS808
    if (panel->page == PAGE_DILLO_SOUND) {
        view_dillo_sound(pixels, panel);
        return;
    }
    if (panel->page == PAGE_KITS) {
        view_drum_kits(pixels, panel);
        return;
    }
#ifdef ELITE_SYNTH_PRESETS
    if (panel->page == PAGE_SYNTH_PRESETS) {
        view_synth_presets(pixels, panel);
        return;
    }
#endif
    if (panel->page == PAGE_DRUM_PRESETS) {
        view_drum_presets(pixels, panel, beat);
        return;
    }
    if (panel->page == PAGE_INSTRUMENTS) {
        view_instruments(pixels, panel);
        return;
    }
    if (panel->page == PAGE_DRUMS || (panel->page == PAGE_CONDUCTOR && panel->drums_repeat)) {
        unsigned rate = drum_part(panel) ? panel->drums_b.roll.rate : panel->drums_roll.rate;
        view_drum_steps(pixels, panel, head, rate_name[rate]);
        return;
    }
#endif
    if (panel->key_transpose_armed) {
#ifdef ELITE_DRUMS808
        view_picnic(pixels, 10, 40, "KEY TRANSPOSE", &picnic_M, DIM);
        jukebox_big(pixels, 10, 70, VIEW_W - 20, "PLAY THE ROOT", LIME);
        view_picnic(pixels, 10, 124, "SECOND C: ORIGINAL", &picnic_M, INK);
        view_picnic(pixels, 10, 214, "SCALE CANCELS", &picnic_S, DIM);
#else
        view_title(pixels, 10, 55, "KEY TRANSPOSE", INK);
        view_text(pixels, 10, 95, "PLAY ROOT KEY", 2, LIME);
        view_text(pixels, 10, 139, "SECOND C = ORIGINAL PITCH", 1, DIM);
        view_text(pixels, 10, 220, "SCALE CANCEL", 1, DIM);
#endif
        return;
    }
#ifdef ELITE_DRUMS808
    if (panel->page == PAGE_SCENES) {
        view_conductor_scenes(pixels, panel);
        return;
    }
    if (panel->page == PAGE_MIX) {
        view_conductor_mix(pixels, panel);
        return;
    }
    if (panel->page == PAGE_CONDUCTOR) {
        view_conductor(pixels, panel, head);
        return;
    }
#endif
    view_telegram(pixels, panel, head, note, rate_name, step_help);
}

static void draw_instrument(uint16_t *pixels, const struct panel *panel,
                            unsigned head, unsigned note, unsigned fault, unsigned safe, unsigned beat)
{
    draw_instrument_page(pixels, panel, head, note, fault, safe, beat);
#ifdef ELITE_DRUMS808
    if (!fault && !safe && !panel->key_transpose_armed) {
        view_focus(pixels, panel);
        if (view_globe_map_visible(panel)) view_globe_map(pixels, panel);
        view_footer(pixels, panel);
    }
    view_dillo_wallpaper(pixels, view_livery(panel)); /* the Dillo is always there, behind the page */
#endif
}
