/* SPDX-License-Identifier: GPL-3.0-only */
#define ELITE_PANEL_EXACT_LIGHTS 1 /* settled pad colours; pad_flare is tested on its own */
#include "telegram101.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Model only the inputs consumed by the actual board panel source. */
enum {
    SMK_BTN_KNOB_BANK, SMK_BTN_FADER_BANK, SMK_BTN_LEFT, SMK_BTN_RIGHT,
    FM1_BTN_OCT_DOWN, FM1_BTN_OCT_UP,
};
static struct {
    uint64_t keys;
    uint32_t buttons, frames;
    uint16_t wheel[2], pedal;
    uint8_t fader[4], key_vel[TG_KEYS];
} fm1_in;
static uint32_t edges;
static int deltas[8];
static int32_t pad_event = -1;
static uint16_t smk_pad_own;
static uint32_t fm1_input_edges(unsigned unused)
{
    (void)unused;
    uint32_t e = edges;
    edges = 0;
    return e;
}
static int fm1_enc_take(unsigned k)
{
    int d = deltas[k];
    deltas[k] = 0;
    return d;
}
static int32_t smk_pad_take(void)
{
    int32_t event = pad_event;
    if (event >= 0 && ((unsigned)event >> 8) < TG_PADS) {
        unsigned bit = 1u << ((unsigned)event >> 8);
        if (event & 127) smk_pad_own |= bit;
        else smk_pad_own &= ~bit;
    }
    pad_event = -1;
    return event;
}
static uint32_t fm1_icfg(void) { return 0; }
static void fm1_irq_off(void) {}
static void fm1_icfg_set(uint32_t config) { (void)config; }

#define KNOB_SPEED_DEFAULT 0
#include "../board/panel101.inc"
#include "../board/pad_feedback.inc"
#include "font.inc"
#include "scope101.h"
static tg_scope test_scope;
#define TG_SCOPE_LIVE (&test_scope)
#include "../board/instrument_view.inc"

static void tap_bt(struct panel *p)
{
    fm1_in.buttons = edges = 1u << BTN_ASSIGN;
    read_controls(p, TG_STEPS);
    fm1_in.buttons = 0;
    read_controls(p, TG_STEPS);
}

static void tap_pad(struct panel *p, unsigned pad)
{
    pad_event = (int32_t)((pad << 8) | 100);
    read_controls(p, TG_STEPS);
    pad_event = (int32_t)(pad << 8);
    read_controls(p, TG_STEPS);
}

static void snapshot(struct panel *p, const char *path)
{
    fm1_in.frames += KEY_FOCUS_NOTICE_FRAMES + CONTROL_FOCUS_FRAMES;
    read_controls(p, TG_STEPS);
    static uint16_t pixels[VIEW_W * VIEW_H];
    draw_instrument(pixels, p, 0, 0, 0, 0, 0);
    FILE *file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P6\n%d %d\n255\n", VIEW_W, VIEW_H);
    for (unsigned i = 0; i < VIEW_W * VIEW_H; i++) {
        unsigned v = pixels[i];
        fputc((v >> 11) * 255 / 31, file);
        fputc(((v >> 5) & 63) * 255 / 63, file);
        fputc((v & 31) * 255 / 31, file);
    }
    assert(!fclose(file));
}

static void check_play_display(struct panel p)
{
    static uint16_t rest[VIEW_W * VIEW_H], sounding[VIEW_W * VIEW_H];
    for (unsigned i = 0; i < TG_SCOPE_RING; i++)
        test_scope.ring[i] = i % 64 < 32 ? -10000 : 10000;
    select_preset(&p, 71);
    const unsigned phrase[] = {60, 64, 0, 67, 0};
    for (unsigned usb = 0; usb <= 1; usb++) {
        p.sound.running = !usb;
        p.sound.clock.source = usb ? TG_CLOCK_USB : TG_CLOCK_INTERNAL;
        p.sound.clock.state = usb ? TG_CLOCK_RUNNING : TG_CLOCK_STOPPED;
        draw_instrument(rest, &p, 0, 0, 0, 0, 0);
        for (unsigned i = 0; i < sizeof phrase / sizeof phrase[0]; i++) {
            draw_instrument(sounding, &p, i, phrase[i], 0, 0, i % 4);
            /* Sound, bank and plot stay put up to the first keybed row. */
            assert(!memcmp(rest + 19 * VIEW_W, sounding + 19 * VIEW_W,
                           (147 - 19) * VIEW_W * sizeof *rest));
            int key_changed = memcmp(rest + 147 * VIEW_W, sounding + 147 * VIEW_W,
                                     22 * VIEW_W * sizeof *rest) != 0;
            assert(key_changed == (phrase[i] != 0));
            if (phrase[i]) {
                /* Count every glyph pixel, including the bottom row above the knob strip. */
                uint16_t glyph[16 * VIEW_W] = {0};
                view_note(glyph, 0, 0, phrase[i], 1, CHROME_BLUE);
                unsigned expected = 0, visible = 0;
                for (unsigned y = 0; y < 16; y++)
                    for (unsigned x = 0; x < 32; x++) {
                        expected += glyph[y * VIEW_W + x] != 0;
                        unsigned pixel = (168 + y) * VIEW_W + VIEW_W - 33 + x;
                        visible += sounding[pixel] != rest[pixel];
                    }
                assert(expected && visible == expected);
            }
        }
    }
    p.sound.clock.state = TG_CLOCK_STOPPED;
    draw_instrument(sounding, &p, 0, 60, 0, 0, 0);
    assert(memcmp(rest + VIEW_TEL_PLOT_Y * VIEW_W, sounding + VIEW_TEL_PLOT_Y * VIEW_W,
                  VIEW_TEL_PLOT_H * VIEW_W * sizeof *rest));
}

#ifdef ELITE_PERCUSSION_727
/* Drive the panel into the conductor, rendering through a whole bar and its next downbeat.
 * The outgoing stock rhythm strikes every step. HANAN WHITE opens with a bombo. */
enum { LAYER_DISABLED, LAYER_MUTED, LAYER_PLAYING };
static void check_layer_downbeat(struct panel p, unsigned usb, unsigned outgoing)
{
    const uint8_t packed[] = {196, 0, 0, 2}, hanan[] = {150, 140, 132};
    const percussion_clip clips[P727_SOUNDS] = {
        [P727_BONGO_HIGH] = {0, 3}, [P727_BOMBO] = {0, 3},
    };
    const percussion_bank bank = {.packed727 = packed, .hanan = hanan, .clips = clips};
    conductor_launch launch;
    conductor_init(&launch);
    assert(conductor_set_percussion_kit(&launch, &bank, P727_STOCK));
    p.layer_available = p.drums_b_initialized = 1;
    p.drums_b.enabled = outgoing != LAYER_DISABLED;
    p.drums_b.muted = outgoing == LAYER_MUTED;
    p.drums_b.pattern.hits[0] = 0xffff;
    p.sound.running = !usb;
    if (usb) p.sound.clock = (tg_clock_snapshot){.source = TG_CLOCK_USB, .state = TG_CLOCK_RUNNING,
                                                .epoch = 1, .phase = 1};
    conductor_patterns req = {.publication = 1, .drum_init = 2, .drums_b = p.drums_b};
    for (unsigned step = 0; step <= DRUM_STEPS; step++) {
        if (usb) p.sound.clock.steps++;
        if (step == 1) {
            tap_bt(&p);
            tap_pad(&p, PICK_BAND);
            deltas[3] = 1;
            read_controls(&p, TG_STEPS);
            assert(p.band_layer == 1 && layer_on(&p));
            req.drums_b = p.drums_b;
            req.publication++;
            struct panel canceled = p;
            conductor_patterns canceled_req = req;
            assert(conductor_cancel_pending(&canceled, &canceled_req, &launch.active, &p.sound)
                   == (1u << MIX_808_B));
            assert(canceled.drums_b.enabled == (outgoing != LAYER_DISABLED));
            assert(canceled.drums_b.muted == (outgoing == LAYER_MUTED));
            assert(!canceled_req.drums_b.rhythm_entry && !canceled.drums_b.rhythm_entry);
        }
        unsigned period = (TG_RATE * 15u + p.sound.tempo / 2u) / p.sound.tempo;
        unsigned frames = step == DRUM_STEPS ? 64u : period;
        for (unsigned offset = 0; offset < frames; offset += 64u) {
            int32_t bass[128] = {0}, drums[128] = {0}, layer[128] = {0};
            unsigned count = frames - offset < 64u ? frames - offset : 64u;
            assert(!conductor_mix(&launch, bass, drums, layer, count, &p.sound,
                                  offset ? period - offset : 0, &req));
            if (step < DRUM_STEPS && outgoing != LAYER_PLAYING) {
                for (unsigned i = 0; i < count * 2u; i++) assert(layer[i] == 0);
            } else if (!offset) {
                unsigned expected = step == DRUM_STEPS ? P727_BOMBO : P727_BONGO_HIGH;
                assert(layer[0] > 0 && launch.drums_b.percussion.voice[0].sound == expected);
            }
        }
    }
    assert(launch.active.drums_b.cue == req.drums_b.cue);
}

static void check_layer_selection(struct panel p, unsigned usb)
{
    p.layer_available = 1;
    p.sound.running = !usb;
    p.sound.clock.source = usb ? TG_CLOCK_USB : TG_CLOCK_INTERNAL;
    p.sound.clock.state = usb ? TG_CLOCK_RUNNING : TG_CLOCK_STOPPED;
    p.drums_b_initialized = 1; /* A saved setup can retain a pattern with this part disabled. */
    p.drums_b.enabled = 0;
    p.layer_mix[0] = 51;
    p.layer_mix[7] = 87;
    p.drums_pattern = drum_demo;
    tap_bt(&p);
    tap_pad(&p, PICK_BAND);
    assert(p.page == PAGE_CONDUCTOR && !strcmp(layer_name(&p), "OFF"));
    deltas[3] = 1;
    read_controls(&p, TG_STEPS);
    assert(layer_on(&p) && p.band_layer == 1);
    tap_bt(&p);
    tap_pad(&p, PICK_COUNT);
    assert(p.rhythm_source == RHYTHM_LAYER && rhythm_selected(&p) == 1);
    for (unsigned rhythm = 2; rhythm <= LAYER_RHYTHM_COUNT; rhythm++) {
        deltas[6] = 1;
        read_controls(&p, TG_STEPS);
        assert(rhythm_selected(&p) == rhythm && p.band_layer == rhythm);
    }
    p.drums_b.muted = 1;
    deltas[6] = 1;
    read_controls(&p, TG_STEPS);
    assert(layer_on(&p) && rhythm_selected(&p) == 1);
    deltas[6] = -1;
    read_controls(&p, TG_STEPS);
    assert(!layer_on(&p) && !p.band_layer);
    p.drums_b.enabled = 0;
    tap_pad(&p, 2);
    assert(layer_on(&p) && rhythm_selected(&p) == 3);
    tap_bt(&p);
    tap_pad(&p, PICK_BAND);
    assert(!strcmp(layer_name(&p), layer_rhythm_names[2]));
    assert(p.layer_mix[0] == 51 && p.layer_mix[7] == 87 && conductor_running(&p));
    assert(!memcmp(&p.drums_pattern, &drum_demo, sizeof drum_demo));
}
#endif

int main(int argc, char **argv)
{
    struct panel p = {.sound = {.tempo = 118, .master = 64}};
    init_map(&p);
    select_preset(&p, 0);
    assert(p.page == PAGE_TONE);
    check_play_display(p);
#ifdef ELITE_PERCUSSION_727
    check_layer_selection(p, 0);
    check_layer_selection(p, 1);
    for (unsigned usb = 0; usb <= 1; usb++)
        for (unsigned outgoing = LAYER_DISABLED; outgoing <= LAYER_PLAYING; outgoing++)
            check_layer_downbeat(p, usb, outgoing);
    puts("PASS: silent 727 part enters on the selected downbeat on internal and USB clocks");
#endif
    p.pad_light = 127;
    p.sound.running = 1;
    tap_bt(&p);
    assert(p.page == PAGE_INSTRUMENTS && picker_rows(&p) == 4);
    for (unsigned pad = 4; pad < TG_PADS; pad++) {
        assert(!pad_color(&p, pad, 0));
        tap_pad(&p, pad);
        assert(p.page == PAGE_INSTRUMENTS);
    }
    tap_bt(&p);
    assert(p.page == PAGE_TONE);
#ifdef ELITE_PERCUSSION_727
    p.layer_available = 1;
#endif
    const unsigned pages[] = {PAGE_TONE, PAGE_DILLO, PAGE_DRUMS, PAGE_CONDUCTOR, PAGE_DRUM_PRESETS};
    unsigned count = picker_rows(&p);
    for (unsigned from = 0; from < count; from++) {
        for (unsigned to = 0; to < count; to++) {
            tap_bt(&p);
            tap_pad(&p, from);
            assert(p.page == pages[from]);
            tap_bt(&p);
            assert(p.page == PAGE_INSTRUMENTS && picker_current(&p) == from);
            assert(pad_color(&p, to, 0));
            if (argc == 4 && from == 0 && to == 0) snapshot(&p, argv[1]);
            tap_pad(&p, to);
            assert(p.page == pages[to] && p.sound.running);
            if (to == 4) assert(p.rhythm_source == RHYTHM_LAYER);
            if (argc == 4 && from == 0 && to == 3) snapshot(&p, argv[2]);
            if (argc == 4 && from == 0 && to == 4) snapshot(&p, argv[3]);
        }
    }
    tap_bt(&p);
    tap_pad(&p, PICK_BAND);
    fm1_in.buttons = edges = 1u << BTN_PATCH;
    read_controls(&p, TG_STEPS);
    fm1_in.buttons = 0;
    read_controls(&p, TG_STEPS);
    assert(library_active(&p));
    tap_bt(&p);
    assert(p.page == PAGE_CONDUCTOR && !library_active(&p));
    fm1_in.buttons = edges = 1u << BTN_ASSIGN;
    read_controls(&p, TG_STEPS);
    fm1_in.frames += HOME_HOLD_FRAMES;
    read_controls(&p, TG_STEPS);
    assert(p.page == PAGE_ASSIGN);
    fm1_in.buttons = 0;
    read_controls(&p, TG_STEPS);
    tap_bt(&p);
    assert(p.page == PAGE_CONDUCTOR);
    puts("PASS: playing-page navigation, transport, picker lights, unavailable pads and cancel");
}
