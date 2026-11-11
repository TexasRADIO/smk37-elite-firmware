/* SPDX-License-Identifier: GPL-3.0-only */
static const char *const control_label[CTRL_COUNT] = {
    "SAW", "PULSE", "SUB", "NOISE", "CUTOFF", "RESO", "ENV", "TRACK", "ATTACK", "DECAY",
    "SUSTAIN", "RELEASE", "WIDTH", "PWM", "LFO RATE", "VCF LFO", "GLIDE", "VCO MOD",
    "DELAY", "REVERB", "P BEND", "F BEND", "P WHEEL", "F WHEEL", "LEVEL", "TEMPO", "OSC OCT", "KEY OCT", "ENV X2", "VCF LO", "LFO KEY", "VCF IDL", "DRIVE", "VCF ATK", "VCF DEC", "VCF SUS", "VCF REL", "ACC TONE", "ACC LVL", "PWM SRC"
};

static const char *const pwm_source_name[TG_PWM_SOURCE_COUNT] = {"MAN", "LFO", "ENV", "S&H", "SUB"};

static void view_octave(uint16_t *pixels,int x,int y,unsigned value,uint16_t color)
{
    int oct=(int)value-2;char text[4]={oct<0?'-':'+','0',(char)('0'+(oct<0?-oct:oct)),0};
    view_text(pixels,x,y,text,1,color);
}

static void view_dial(uint16_t *pixels, int x, int y, unsigned position,
                      const char *name, unsigned control, unsigned value, unsigned scale, uint16_t color, unsigned known)
{
    char key[] = "K1";
    char short_name[7] = {0};
    for (unsigned j = 0; j < 6 && name[j]; j++) short_name[j] = name[j];
    key[1] = (char)('1' + position);
    view_rect(pixels, x, y, 52, 45, CARD);
    view_text(pixels, x + 3, y + 2, short_name, 1, INK);
    view_text(pixels, x + 3, y + 19, key, 1, DIM);
    if (known && control == CTRL_PWM_SOURCE) view_text(pixels, x+26, y+19, pwm_source_name[value], 1, color);
    else if (known && scale==4) view_octave(pixels,x+26,y+19,value,color);
    else if (known && scale==1) view_text(pixels,x+26,y+19,value ? "ON" : "OFF",1,color);
    else if (known) view_num(pixels, x + 26, y + 19, value, 3, color);
    else view_text(pixels, x + 26, y + 19, "---", 1, DIM);
    view_rect(pixels, x + 3, y + 38, 46, 3, EDGE);
    view_rect(pixels, x + 3, y + 38, (int)(46u * (value < scale ? value : scale) / scale), 3, color);
}

static void view_wave(uint16_t *pixels, const tg_patch *patch, unsigned playing)
{
    int previous = 80;
    unsigned width = 54u - patch->value[TG_WIDTH] * 45u / 127u;
    unsigned noise = 43;
    view_rect(pixels, 10, 80, 220, 1, EDGE);
    for (unsigned i = 0; i < 220; i++) {
        unsigned phase = i % 110u;
        noise = (noise * 73u + 19u) & 255u;
        int mix = ((int)phase - 55) * patch->value[TG_SAW] +
                  (phase < width ? 42 : -42) * patch->value[TG_PULSE] +
                  (i < 110 ? 32 : -32) * patch->value[TG_SUB] +
                  ((int)noise - 128) * patch->value[TG_NOISE] / 5;
        int y = 80 - mix / 1400;
        int top = y < previous ? y : previous;
        int bottom = y > previous ? y : previous;
        view_rect(pixels, 10 + (int)i, top, 1, bottom - top + 1, playing ? LIME : DIM);
        previous = y;
    }
}

static void view_slope(uint16_t *pixels, int x, int y, int width, int end_y, uint16_t color)
{
    for (int i = 0; i < width; i++) {
        int from = y + (end_y - y) * i / width;
        int to = y + (end_y - y) * (i + 1) / width;
        int top = from < to ? from : to;
        int bottom = from > to ? from : to;
        view_rect(pixels, x + i, top, 1, bottom - top + 2, color);
    }
}

static void view_pulse(uint16_t *pixels, unsigned width, int y, uint16_t color)
{
    int edge = 24 + (int)width;
    view_rect(pixels, 24, y, width, 2, color);
    view_rect(pixels, edge, y, 2, 15, color);
    view_rect(pixels, edge, y + 13, 216 - edge, 2, color);
}

static void view_pulse_focus(uint16_t *pixels, const tg_patch *patch, unsigned control)
{
    /* The native pulse starts at half duty and both controls narrow it. */
    unsigned width = 96u - patch->value[TG_WIDTH] * 92u / 127u;
    int narrow = (int)width - (int)(patch->value[TG_PWM] * 92u / 127u);
    if (narrow < 4) narrow = 4;
    if (control != TG_WIDTH && patch->pwm != TG_PWM_MANUAL) {
        view_pulse(pixels, width, 146, SKY);
        view_pulse(pixels, (unsigned)narrow, 171, LIME);
        view_text(pixels, 24, 194, pwm_source_name[patch->pwm], 1, SKY);
        view_text(pixels, 57, 194, "WIDTH RANGE", 1, DIM);
    } else {
        view_pulse(pixels, width, 158, LIME);
        view_text(pixels, 24, 194, control == TG_WIDTH ? "RESTING WIDTH" : "PWM SOURCE MANUAL", 1, DIM);
    }
}

static void view_envelope_focus(uint16_t *pixels, const tg_patch *patch, unsigned control)
{
    int attack = 10 + patch->value[TG_ATTACK] * 38 / 127;
    int decay = 10 + patch->value[TG_DECAY] * 38 / 127;
    int release = 10 + patch->value[TG_RELEASE] * 38 / 127;
    int sustain_y = 191 - patch->value[TG_SUSTAIN] * 47 / 127;
    int x = 24;
    view_rect(pixels, x, 193, 192, 1, EDGE);
    view_slope(pixels, x, 191, attack, 144, control == TG_ATTACK ? LIME : SKY);
    x += attack;
    view_slope(pixels, x, 144, decay, sustain_y, control == TG_DECAY ? LIME : SKY);
    x += decay;
    view_slope(pixels, x, sustain_y, 32, sustain_y, control == TG_SUSTAIN ? LIME : SKY);
    x += 32;
    view_slope(pixels, x, sustain_y, release, 191, control == TG_RELEASE ? LIME : SKY);
    view_text(pixels, 24, 198, "ATTACK DECAY SUS REL", 1, DIM);
}

static void view_lfo_focus(uint16_t *pixels, const tg_patch *patch, unsigned control)
{
    static const uint8_t random_levels[] = {28, 3, 35, 16, 6, 31, 11, 24};
    unsigned cycle_span = 192u + patch->value[TG_LFO_RATE] * 1152u / 127u;
    int depth = control == TG_LFO_RATE ? 127 : patch->value[control];
    int previous = 168;
    view_rect(pixels, 24, 168, 192, 1, EDGE);
    for (unsigned x = 0; x < 192; x++) {
        unsigned progress = x * cycle_span / 192u;
        unsigned phase = progress % 192u;
        int level = (int)(phase < 96 ? phase : 192 - phase) * 36 / 96;
        switch (patch->lfo_wave) {
        case TG_LFO_SQUARE: level = phase < 96 ? 36 : 0; break;
        case TG_LFO_RANDOM: level = random_levels[progress / 192u]; break;
        case TG_LFO_NOISE: level = random_levels[x % 8u]; break;
        }
        int y = 168 - (level - 18) * depth / 127;
        int top = y < previous ? y : previous;
        int bottom = y > previous ? y : previous;
        view_rect(pixels, 24 + (int)x, top, 1, bottom - top + 1, LIME);
        previous = y;
    }
    const char *label = control == TG_VIBRATO ? "PITCH MODULATION" :
                        control == TG_FILTER_LFO ? "CUTOFF MODULATION" : "LFO SHAPE";
    view_text(pixels, 24, 198, label, 1, DIM);
}

static void view_control_focus(uint16_t *pixels, const struct panel *panel)
{
    const tg_controls *sound = &panel->sound;
    unsigned control = panel->focus_control - 1u;
    unsigned value = control_value(sound, control);
    unsigned max = control == CTRL_TEMPO ? 300u : control_switch(control) ? 1u :
                   control_five_positions(control) ? 4u : 127u;
    unsigned offset = control == CTRL_TEMPO ? 30u : 0u;
    unsigned position = (value - offset) * 192u / (max - offset);
    unsigned slot = panel->focus_slot;
    char source[] = {slot < 8 ? 'K' : 'F', (char)('1' + (slot < 8 ? slot : slot - 8)), 0};
    view_text(pixels, 12, 34, source, 1, SKY);
    view_text(pixels, 12, 55, control_label[control], 2, INK);

    char number[] = {(char)('0' + value / 100u), (char)('0' + value / 10u % 10u),
                     (char)('0' + value % 10u), 0};
    const char *display = number + (value < 10u ? 2u : value < 100u ? 1u : 0u);
    char octave[] = {value < 2u ? '-' : '+', (char)('0' + (value < 2u ? 2u - value : value - 2u)), 0};
    if (control == CTRL_PWM_SOURCE) display = pwm_source_name[value];
    else if (control_switch(control)) display = value ? "ON" : "OFF";
    else if (control == CTRL_OSC_OCTAVE || control == CTRL_KEY_OCTAVE) display = octave;
    view_text(pixels, 12, 91, display, 3, LIME);

    if (panel->focus_pickup) {
        int physical = (int)(panel->focus_position * 192u / 127u);
        view_rect(pixels, 24, 169, 192, 4, EDGE);
        view_rect(pixels, 22 + (int)position, 154, 5, 25, SKY);
        view_rect(pixels, 22 + physical, 162, 5, 25, CORAL);
        view_text(pixels, 12, 199, "FADER", 1, CORAL);
        view_text(pixels, 76, 199, "CROSS SAVED VALUE", 1, SKY);
    } else if (control == TG_CUTOFF || control == TG_RESONANCE) {
        /* Illustrative response. The horizontal scale is the patch's
         * normalized cutoff range, not a measurement in hertz. */
        int corner = 32 + sound->patch.value[TG_CUTOFF] * 176 / 127;
        int peak = sound->patch.value[TG_RESONANCE] * 17 / 127;
        int previous = 161;
        view_rect(pixels, 24, 192, 192, 1, EDGE);
        view_rect(pixels, corner, 146, 1, 47, EDGE);
        for (int x = 24; x < 216; x++) {
            int d = x - corner;
            int distance = d < 0 ? -d : d;
            int slope = d <= 0 ? 0 : d >= 24 ? 29 : d * 29 / 24;
            int y = 161 + slope - (distance < 10 ? peak * (10 - distance) / 10 : 0);
            int top = y < previous ? y : previous;
            int bottom = y > previous ? y : previous;
            view_rect(pixels, x, top, 1, bottom - top + 2, LIME);
            previous = y;
        }
    } else if (control == TG_WIDTH || control == TG_PWM || control == CTRL_PWM_SOURCE) {
        view_pulse_focus(pixels, &sound->patch, control);
    } else if (control >= TG_ATTACK && control <= TG_RELEASE) {
        view_envelope_focus(pixels, &sound->patch, control);
    } else if (control == TG_LFO_RATE || control == TG_VIBRATO || control == TG_FILTER_LFO) {
        view_lfo_focus(pixels, &sound->patch, control);
    } else {
        view_rect(pixels, 24, 155, 192, 21, EDGE);
        view_rect(pixels, 24, 155, (int)position, 21, LIME);
    }
    view_text(pixels, 12, 220, panel->page == PAGE_MANUAL ? "SHAPE" : panel->page == PAGE_FX ? "FX" : "PLAY", 1, DIM);
}

static void view_arp_editor(uint16_t *pixels, const struct panel *panel, const char *rate)
{
    static const char *const mode[] = {"OFF", "UP", "DOWN", "EXCLUDE", "INCLUDE", "RANDOM", "ORDER", "REPEAT"};
    const tg_controls *sound = &panel->sound;
    unsigned usb = sound->clock.source == TG_CLOCK_USB;
    view_title(pixels, 10, 32, "ARPEGGIATOR", INK);
    for (int row = 0; row < 4; row++) {
        view_rect(pixels, 10, 57 + row * 37, 106, 34, CARD);
        view_rect(pixels, 124, 57 + row * 37, 106, 34, CARD);
    }
    view_text(pixels, 15, 57, "K1 MODE", 1, DIM);
    view_text(pixels, 15, 73, mode[sound->arp], 1, LIME);
    view_text(pixels, 129, 57, "K2 RATE", 1, DIM);
    view_text(pixels, 129, 73, rate, 1, SKY);
    view_text(pixels, 15, 94, "K3 GATE %", 1, DIM);
    view_num(pixels, 15, 110, sound->arp_gate ? sound->arp_gate : 75, 3, SKY);
    view_rect(pixels, 47, 116, 60, 4, EDGE);
    view_rect(pixels, 47, 116, (sound->arp_gate ? sound->arp_gate : 75) * 60 / 100, 4, LIME);
    view_text(pixels, 129, 94, "K4 OCTAVES", 1, DIM);
    view_num(pixels, 129, 110, sound->arp_octaves + 1u, 1, SKY);
    for (unsigned octave = 0; octave < 4; octave++)
        view_rect(pixels, 150 + 16 * octave, 114, 12, 8, octave <= sound->arp_octaves ? LIME : EDGE);
    view_text(pixels, 15, 131, "K5 SWING %", 1, DIM);
    view_num(pixels, 15, 147, 50u + sound->arp_swing, 2, SKY);
    view_text(pixels, 129, 131, "K6 LATCH", 1, DIM);
    view_text(pixels, 129, 147, panel->arp_latch ? "ON" : "OFF", 1, panel->arp_latch ? LIME : SKY);
    view_text(pixels, 15, 168, "K7 BPM", 1, DIM);
    if (usb) view_text(pixels, 15, 184, "EXTERNAL", 1, DIM);
    else view_num(pixels, 15, 184, tg_arp_tempo(sound), 3, SKY);
    view_text(pixels, 129, 168, "K8 CLOCK", 1, DIM);
    view_text(pixels, 129, 184, usb ? "USB MIDI" : "INTERNAL", 1, SKY);
    view_text(pixels, 10, 217, "ARP EXIT", 1, DIM);
    view_text(pixels, 129, 217, "CHORD LATCH", 1, DIM);
}

#ifdef ELITE_DRUMS808
enum {
    TEL_STAGE = VIEW_RGB(28, 25, 22), TEL_STRIP = VIEW_RGB(36, 31, 27),
    TEL_PLOT = VIEW_RGB(43, 37, 32), TEL_GRID = VIEW_RGB(65, 53, 44),
    TEL_FILL = VIEW_RGB(72, 46, 30)
};

static const char *const tel_short_control[CTRL_COUNT] = {
    "SAW", "PULS", "SUB", "NOIS", "CUT", "RES", "ENV", "TRK", "ATK", "DEC",
    "SUS", "REL", "PW", "PWM", "LFO", "VLF", "GLD", "VIB", "DLY", "REV",
    "PBND", "FBND", "PWHL", "FWHL", "LVL", "BPM", "OCT", "KEY", "ENV2", "LOW",
    "LRET", "IDLE", "DRV", "FATK", "FDEC", "FSUS", "FREL", "ACTN", "ACLV", "PSRC"
};

static unsigned tel_control_position(const tg_controls *sound, unsigned control)
{
    unsigned value = control_value(sound, control);
    if (control == CTRL_TEMPO) return (bounded((int)value, 30, 300) - 30u) * 127u / 270u;
    if (control_switch(control)) return value ? 127u : 0u;
    if (control_five_positions(control)) return value * 127u / 4u;
    return bounded((int)value, 0, 127);
}

static void view_tel_plot_segment(uint16_t *pixels, int x, int from, int to, uint16_t color)
{
    int y = from < to ? from : to;
    int h = from < to ? to - from : from - to;
    view_rect(pixels, x, y, 1, h + 1, color);
}

static void view_tel_oscillator(uint16_t *pixels, const tg_patch *patch, int x, int y, int w, int h)
{
    int previous = y + h / 2;
    int width = 64 - patch->value[TG_WIDTH] * 61 / 127;
    unsigned noise = 43;
    view_rect(pixels, x, previous, w, 1, TEL_GRID);
    for (int i = 0; i < w; i++) {
        int progress = 256 * i / w;
        int phase = progress % 128;
        noise = (noise * 73u + 19u) & 255u;
        int sample = (phase - 64) * patch->value[TG_SAW] +
                     (phase < width ? 64 : -64) * patch->value[TG_PULSE] +
                     (progress < 128 ? 64 : -64) * patch->value[TG_SUB] +
                     ((int)noise - 128) * patch->value[TG_NOISE] / 2;
        int point = y + h / 2 - sample * (h / 2 - 1) / (4 * 64 * 127);
        view_tel_plot_segment(pixels, x + i, previous, point, CHROME_BLUE);
        previous = point;
    }
}

static void view_tel_filter(uint16_t *pixels, const tg_patch *patch, int x, int y, int w, int h)
{
    int corner = 4 + patch->value[TG_CUTOFF] * (w - 9) / 127;
    int resonance = patch->value[TG_RESONANCE] * (h / 3) / 127;
    int base = y + h - 1;
    int previous = y + h / 3;
    for (int i = 0; i < w; i++) {
        int d = i - corner, distance = d < 0 ? -d : d;
        int slope = d < 0 ? 0 : d > 12 ? h / 2 : d * (h / 2) / 12;
        int peak = distance < 7 ? resonance * (7 - distance) / 7 : 0;
        int point = y + h / 3 + slope - peak;
        view_rect(pixels, x + i, point, 1, base - point + 1, TEL_FILL);
        view_tel_plot_segment(pixels, x + i, previous, point, CHROME_BLUE);
        previous = point;
    }
}

static void view_tel_envelope(uint16_t *pixels, const tg_patch *patch, unsigned filter,
                              int x, int y, int w, int h)
{
    unsigned levels[TG_ADSR_STAGES];
    for (unsigned i = 0; i < TG_ADSR_STAGES; i++)
        levels[i] = filter && patch->filter_env_mode != TG_FILTER_ENV_SHARED ?
                    tg_filter_adsr_value(patch, i) : patch->value[TG_ATTACK + i];
    int a = w / 16 + levels[0] * w / (127 * 5);
    int d = w / 16 + levels[1] * w / (127 * 5);
    int r = w / 16 + levels[3] * w / (127 * 5);
    unsigned closing = filter && patch->filter_env_mode == TG_FILTER_ENV_CLOSE;
    int base = closing ? y + 1 : y + h - 2;
    int peak = closing ? y + h - 2 : y + 1;
    int sustain = base + (peak - base) * (int)levels[2] / 127;
    view_rect(pixels, x, base, w, 1, TEL_GRID);
    view_slope(pixels, x, base, a, peak, CHROME_BLUE);
    view_slope(pixels, x + a, peak, d, sustain, CHROME_BLUE);
    view_slope(pixels, x + a + d, sustain, w - a - d - r, sustain, CHROME_BLUE);
    view_slope(pixels, x + w - r, sustain, r, base, CHROME_BLUE);
}

static void view_tel_lfo(uint16_t *pixels, const tg_patch *patch, int x, int y, int w, int h)
{
    static const int points[] = {7, -3, 5, -6, -1, 3, -7, 1};
    int previous = y + h / 2;
    int cycles = 1 + patch->value[TG_LFO_RATE] * 6 / 127;
    view_rect(pixels, x, previous, w, 1, TEL_GRID);
    for (int i = 0; i < w; i++) {
        int progress = 32 * cycles * i / w;
        int phase = progress % 32;
        int sample = (phase < 16 ? phase : 32 - phase) - 8;
        if (patch->lfo_wave == TG_LFO_SQUARE) sample = phase < 16 ? 7 : -7;
        if (patch->lfo_wave == TG_LFO_RANDOM) sample = points[progress / 32];
        if (patch->lfo_wave == TG_LFO_NOISE) sample = points[(i ^ (i / 8)) & 7];
        int point = y + h / 2 - sample * (h / 2 - 1) / 8;
        view_tel_plot_segment(pixels, x + i, previous, point, CHROME_BLUE);
        previous = point;
    }
}

static void view_tel_keyboard(uint16_t *pixels, const struct panel *panel, unsigned note)
{
    static const uint16_t black = (1u << 1) | (1u << 3) | (1u << 6) | (1u << 8) | (1u << 10);
    const int first = untransposed_key_note(panel, 0) + panel->key_transpose;
    for (unsigned key = 0, white = 0; key < TG_KEYS; key++) {
        int pitch = first + (int)key;
        unsigned sharp = (black >> (key % 12u)) & 1u;
        if (!sharp) {
            view_rect(pixels, 10 + 10 * white, 147, 9, 22, note && pitch == (int)note ? CHROME_BLUE : INK);
            white++;
        }
    }
    for (unsigned key = 0, white = 0; key < TG_KEYS; key++) {
        int pitch = first + (int)key;
        if (black & (1u << (key % 12u)))
            view_rect(pixels, 10 + 10 * white - 4, 147, 7, 12, note && pitch == (int)note ? CHROME_BLUE : TEL_STAGE);
        else white++;
    }
}

/* Telegram home: the sound's name big, the note being played big, one envelope picture and
 * the keybed. Struck pads (when they play sounds) flash the name. */
static void view_tel_play_stage(uint16_t *pixels, const struct panel *panel, unsigned note)
{
    const tg_patch *patch = &panel->sound.patch;
    const char *name = panel_sound_name(panel);
    unsigned glow = 0;
    for (unsigned pad = 0; pad < TG_PADS; pad++) if (pad_hit_glow(panel, pad)) glow = pad_hit_glow(panel, pad);
    if (glow && !panel->pad_keys) view_rect(pixels, 0, 19, VIEW_W, 42, picnic_mix(TEL_STAGE, CHROME_BLUE, glow * 2u));
    jukebox_big(pixels, 8, 22, VIEW_W - 22, name, INK);
    if (panel_sound_edited(panel)) view_picnic(pixels, 226, 24, "*", &picnic_M, CORAL);

    /* One line: the note in the large face while it sounds, else which knobs and pads. */
    if (note) {
        char text[6];
        unsigned at = 0;
        for (const char *c = jukebox_pc_name[note % 12u]; *c; c++) text[at++] = *c;
        int octave = (int)(note / 12u) - 1;
        if (octave < 0) { text[at++] = '-'; octave = -octave; }
        text[at++] = (char)('0' + octave % 10);
        text[at] = 0;
        view_picnic(pixels, 8, 58, text, &picnic_L, CHROME_BLUE);
    } else {
        unsigned bank = panel->knob_bank;
        unsigned custom = memcmp(panel->knob_map[bank], tg_default_knob_map[bank], sizeof panel->knob_map[bank]) != 0;
        view_picnic(pixels, 8, 68, custom ? "USER KNOBS" : bank ? "MOD KNOBS" : "TONE KNOBS", &picnic_M, DIM);
    }
    const char *chip = panel->hold ? "HOLD" : panel->repeat_on ? "REPEAT" : NULL;
    char key[8];
    if (panel->key_transpose) {
        int shift = panel->key_transpose;
        unsigned n = (unsigned)(shift < 0 ? -shift : shift), at = 0;
        key[at++] = 'K'; key[at++] = 'E'; key[at++] = 'Y'; key[at++] = shift < 0 ? '-' : '+';
        if (n >= 10u) key[at++] = (char)('0' + n / 10u);
        key[at++] = (char)('0' + n % 10u);
        key[at] = 0;
        chip = key;
    }
    if (chip) {
        int w = view_picnic_width(chip, &picnic_M) + 10;
        view_rect(pixels, VIEW_W - 8 - w, 66, w, 22, CHROME_BLUE);
        view_picnic(pixels, VIEW_W - 3 - w, 68, chip, &picnic_M, TEL_STAGE);
    }

    /* One picture: how the sound moves (the gated VCA as a box). */
    view_rect(pixels, 8, 94, 224, 48, TEL_PLOT);
    if (patch->amp_gate) {
        view_slope(pixels, 12, 138, 8, 100, CHROME_BLUE);
        view_slope(pixels, 20, 100, 200, 100, CHROME_BLUE);
        view_slope(pixels, 220, 100, 8, 138, CHROME_BLUE);
    } else view_tel_envelope(pixels, patch, 0, 12, 98, 216, 40);
    view_tel_keyboard(pixels, panel, note);
}

enum { VIEW_PICKUP_SPAN = 47, VIEW_PICKED_UP = VIEW_RGB(86, 211, 106) };

static unsigned view_fader_offset(unsigned position)
{
    return position * VIEW_PICKUP_SPAN / 127u;
}

static void view_pickup_fader(uint16_t *pixels, int x, int bottom, unsigned value,
                              unsigned physical, unsigned caught, uint16_t rail)
{
    int target_y = bottom - (int)view_fader_offset(value);
    int physical_y = bottom - (int)view_fader_offset(physical);
    view_rect(pixels, x - 2, bottom - VIEW_PICKUP_SPAN, 5, VIEW_PICKUP_SPAN + 1, rail);
    view_rect(pixels, x - 7, target_y - 2, 15, 5, caught ? VIEW_PICKED_UP : CHROME_CREAM);
    if (caught || target_y == physical_y) return;
    view_rect(pixels, x - 7, physical_y - 2, 15, 1, CHROME_RED);
    view_rect(pixels, x - 7, physical_y + 2, 15, 1, CHROME_RED);
    view_rect(pixels, x - 7, physical_y - 1, 1, 3, CHROME_RED);
    view_rect(pixels, x + 7, physical_y - 1, 1, 3, CHROME_RED);
    int direction = target_y > physical_y ? 1 : -1;
    for (int row = 0; row < 4; row++)
        view_rect(pixels, x + 14 - row, physical_y - row * direction, 2 * row + 1, 1, CHROME_RED);
}

/* 101 shape and filter envelope: the section's name, its picture, and four faders whose values
 * stand over them in Medium; the section's switch as one coloured tag. */
static void view_tel_shape_stage(uint16_t *pixels, const struct panel *panel)
{
    static const char *const names[] = {"SOURCE MIX", "FILTER", "ENVELOPE", "MODULATOR"};
    static const char *const envelope[] = {"ATK", "DEC", "SUS", "REL"};
    const tg_controls *sound = &panel->sound;
    const tg_patch *patch = &sound->patch;
    unsigned filter_env = panel->page == PAGE_FILTER_ENV;
    unsigned bank = panel->fader_bank;
    view_picnic(pixels, 8, 21, filter_env ? "FILTER ENV" : names[bank], &picnic_M, INK);
    const char *tag = NULL;
    if (filter_env) {
        static const char *const modes[] = {"SHARED", "OPENS", "CLOSES"};
        tag = modes[patch->filter_env_mode];
    } else if (bank == 0) {
        static const char *const sub[] = {"SUB -1", "SUB -2", "SUB PULSE"};
        tag = sub[patch->sub_mode];
    } else if (bank == 2) {
        static const char *const triggers[] = {"GATE+TRIG", "GATE", "LFO"};
        tag = patch->amp_gate ? "VCA GATE" : triggers[patch->trigger];
    } else if (bank == 3) {
        static const char *const waves[] = {"TRIANGLE", "SQUARE", "RANDOM", "NOISE"};
        tag = waves[patch->lfo_wave];
    }
    if (tag) view_tag_left(pixels, VIEW_W - 8, 20, tag, CHROME_BLUE, TEL_STAGE);
    view_rect(pixels, 8, 46, 224, 50, TEL_PLOT);
    if (filter_env || bank == 2) view_tel_envelope(pixels, patch, filter_env, 12, 50, 216, 42);
    else if (bank == 0) view_tel_oscillator(pixels, patch, 12, 50, 216, 42);
    else if (bank == 1) view_tel_filter(pixels, patch, 12, 50, 216, 42);
    else view_tel_lfo(pixels, patch, 12, 50, 216, 42);
    if (filter_env) { /* accent's two amounts, as thin bars in the picture's foot */
        const unsigned amount[] = {patch->accent_tone, patch->accent_level};
        for (unsigned i = 0; i < 2; i++) view_rect(pixels, 12 + 110 * (int)i, 92, (100 * (int)amount[i] + 126) / 127, 2, CORAL);
    }
    for (unsigned f = 0; f < 4; f++) {
        unsigned control = panel->fader_map[bank][f];
        unsigned value = filter_env ? tg_filter_adsr_value(patch, f) : tel_control_position(sound, control);
        int x = 36 + 56 * f;
        char num[4] = {(char)('0' + value / 100u % 10u), (char)('0' + value / 10u % 10u), (char)('0' + value % 10u), 0};
        const char *v = value >= 100u ? num : value >= 10u ? num + 1 : num + 2;
        unsigned caught = fader_caught & (1u << f);
        view_picnic(pixels, x - view_picnic_width(v, &picnic_M) / 2, 99, v, &picnic_M, caught ? VIEW_PICKED_UP : INK);
        view_pickup_fader(pixels, x, 166, value, last_fader[f], caught, TEL_GRID);
        const char *label = filter_env ? envelope[f] : tel_short_control[control];
        view_small(pixels, x + 10, 158, label, DIM);
    }
}

/* 101 steps: the step under the cursor big, sixteen steps laid out like the pads (two rows of
 * eight) with each note's height, and the cursor's note big with gate and loop length. */
static void view_tel_sequence_stage(uint16_t *pixels, const struct panel *panel, unsigned head)
{
    const tg_controls *sound = &panel->sound;
    unsigned length = tg_pattern_length(sound);
    unsigned base = step_page_base(panel, head);
    view_label_number(pixels, 8, 22, "STEP", panel->cursor + 1u, DIM, INK);
    {
        unsigned lane = conductor_edit_lane(panel);
        struct conductor_row_state state = conductor_lane_state(panel, lane);
        unsigned slot = state.queued + 1u;
        char pat[] = "PATTERN 00";
        pat[8] = (char)('0' + slot / 10u); pat[9] = (char)('0' + slot % 10u);
        if (lane == MIX_TELEGRAM && panel->telegram_library) { pat[8] = 'L'; pat[9] = 0; }
        int right = VIEW_W - 8;
        if (!state.enabled) right = view_tag_left(pixels, right, 30, "MUTE", CORAL, TEL_STAGE) - 6;
        view_picnic(pixels, right - view_picnic_width(pat, &picnic_S), 37, pat, &picnic_S, state.pending ? CORAL : DIM);
    }
    unsigned low = 127, high = 0;
    for (unsigned i = base; i < base + TG_PADS && i < length; i++) {
        unsigned pitch = sound->steps[i] & 127u;
        if (pitch && pitch < low) low = pitch;
        if (pitch > high) high = pitch;
    }
    for (unsigned pad = 0; pad < TG_PADS && base + pad < TG_STEPS; pad++) {
        unsigned i = base + pad;
        int x = 8 + (int)(pad % 8u) * 28, y = 62 + (int)(pad / 8u) * 40;
        unsigned pitch = sound->steps[i] & 127u;
        uint16_t cell = i == head ? TEL_FILL : i >= length ? TEL_STAGE : pad / 4u & 1u ? TEL_PLOT : TEL_STRIP;
        unsigned glow = pad_hit_glow(panel, pad);
        if (glow) cell = picnic_mix(cell, CHROME_BLUE, glow * 2u);
        view_rect(pixels, x, y, 26, 36, cell);
        if (i < length && pitch) {
            int level = high > low ? (int)((pitch - low) * 26u / (high - low)) : 13;
            int width = sound->steps[i] & TG_TIE ? 26 : 8 + (int)(tg_phrase_gate(sound) * 14u / 100u);
            view_rect(pixels, x + 2, y + 30 - level, width - 2, 5, i == head ? INK : CHROME_BLUE);
        } else if (i < length && (sound->steps[i] & TG_TIE)) view_rect(pixels, x, y + 26, 26, 3, CHROME_BLUE);
        if (i == panel->cursor) view_rect(pixels, x, y + 36, 26, 3, CORAL);
    }
    unsigned entry = sound->steps[panel->cursor];
    char note[6];
    int x = 8;
    if (panel->cursor < length && (entry & 127u))
        x = view_picnic(pixels, 8, 140, jukebox_note_name(note, entry & 127u), &picnic_L, CHROME_BLUE) + 10;
    else x = view_picnic(pixels, 8, 150, panel->cursor >= length ? "OUT" : (entry & TG_TIE) ? "TIE" : "REST", &picnic_M, DIM) + 10;
    x = view_picnic(pixels, x, 154, "GATE", &picnic_S, DIM) + 4;
    char num[4] = {(char)('0' + tg_phrase_gate(sound) / 100u % 10u), (char)('0' + tg_phrase_gate(sound) / 10u % 10u),
                   (char)('0' + tg_phrase_gate(sound) % 10u), 0};
    x = view_picnic(pixels, x, 150, tg_phrase_gate(sound) >= 100u ? num : num + 1, &picnic_M, INK) + 8;
    if (panel->record) view_tag_left(pixels, VIEW_W - 8, 148, "REC", CORAL, TEL_STAGE);
    else {
        char len[4] = {(char)('0' + length / 100u % 10u), (char)('0' + length / 10u % 10u), (char)('0' + length % 10u), 0};
        const char *l = length >= 100u ? len : len + 1;
        int lx = VIEW_W - 8 - view_picnic_width(l, &picnic_M);
        view_picnic(pixels, lx, 150, l, &picnic_M, INK);
        view_picnic(pixels, lx - 4 - view_picnic_width("LEN", &picnic_S), 154, "LEN", &picnic_S, DIM);
    }
    (void)x;
    if (panel->phrase_undo && !panel->record) view_picnic(pixels, 8, 174, "CHORD+< UNDO", &picnic_S, DIM);
}

static void view_tel_rhythm_stage(uint16_t *pixels, const struct panel *panel,
                                  const char *const *rate_name, unsigned note)
{
    static const char *const modes[] = {"ARP OFF", "ARP UP", "ARP DOWN", "ARP EXCLUDE",
                                      "ARP INCLUDE", "ARP RANDOM", "ARP ORDER", "ARP REPEAT"};
    static const uint8_t contour[][8] = {
        [TG_ARP_UP] = {0, 1, 2, 3, 0, 1, 2, 3},
        [TG_ARP_DOWN] = {3, 2, 1, 0, 3, 2, 1, 0},
        [TG_ARP_BOUNCE] = {0, 1, 2, 3, 2, 1, 0, 1},
        [TG_ARP_INCLUSIVE] = {0, 1, 2, 3, 3, 2, 1, 0},
        [TG_ARP_RANDOM] = {2, 0, 1, 0, 3, 2, 1, 3},
        [TG_ARP_ORDER] = {1, 3, 0, 2, 1, 3, 0, 2},
        [TG_ARP_REPEAT] = {0, 0, 1, 1, 2, 2, 3, 3}
    };
    const tg_controls *sound = &panel->sound;
    unsigned repeat = panel->repeat_edit;
    unsigned rate = repeat ? sound->repeat_rate : sound->arp_rate;
    unsigned gate = repeat ? sound->repeat_gate : sound->arp_gate;
    unsigned swing = repeat ? sound->repeat_swing : sound->arp_swing;
    unsigned latch = repeat ? panel->repeat_latch : panel->arp_latch;
    if (!gate) gate = 75;
    /* The mode big, its rate as a tag; gate, swing and octaves on one Medium line. */
    jukebox_big(pixels, 8, 20, 160, repeat ? "REPEAT" : modes[sound->arp], INK);
    view_tag_left(pixels, VIEW_W - 8, 24, rate_name[rate], CHROME_BLUE, TEL_STAGE);
    {
        char line[32];
        unsigned at = 0, sw = 50u + swing;
        for (const char *c = "GATE "; *c; c++) line[at++] = *c;
        if (gate >= 100u) line[at++] = (char)('0' + gate / 100u);
        line[at++] = (char)('0' + gate / 10u % 10u); line[at++] = (char)('0' + gate % 10u);
        for (const char *c = "  SW "; *c; c++) line[at++] = *c;
        line[at++] = (char)('0' + sw / 10u % 10u); line[at++] = (char)('0' + sw % 10u);
        if (!repeat) {
            line[at++] = ' '; line[at++] = ' ';
            line[at++] = (char)('1' + sound->arp_octaves);
            for (const char *c = " OCT"; *c; c++) line[at++] = *c;
        }
        line[at] = 0;
        view_status_line(pixels, 44 + 18, line, DIM);
    }
    if (latch) view_tag_left(pixels, VIEW_W - 8, 60, "LATCH", LIME, TEL_STAGE);
    view_rect(pixels, 8, 86, 224, 60, TEL_PLOT);
    view_rect(pixels, 8, 134, 224, 1, TEL_GRID);
    if (repeat || sound->arp != TG_ARP_OFF) {
        for (unsigned step = 0; step < 8; step++) {
            unsigned shift = step & 1u ? swing / 4u : 0;
            int x = 12 + step * 27 + shift;
            int y = repeat ? 92 : 122 - contour[sound->arp][step] * 10;
            int w = 4 + gate * 16u / 100u;
            view_rect(pixels, x, y, w, repeat ? 42 : 8,
                      repeat && !panel->repeat_on ? TEL_GRID : CHROME_BLUE);
        }
    }
    char name[6];
    int x = 8;
    if (note) x = view_picnic(pixels, 8, 146, jukebox_note_name(name, note), &picnic_L, CHROME_BLUE) + 10;
    view_picnic(pixels, x, 156, repeat ? panel->repeat_on ? "HOLD A PAD" : "REPEAT OFF" :
                sound->clock.source == TG_CLOCK_USB ? "USB CLOCK" : note ? "" : "HOLD A CHORD", &picnic_M, DIM);
    if (!repeat && sound->clock.source != TG_CLOCK_USB) {
        unsigned bpm = tg_arp_tempo(sound);
        char t[4] = {(char)('0' + bpm / 100u % 10u), (char)('0' + bpm / 10u % 10u), (char)('0' + bpm % 10u), 0};
        const char *tt = bpm >= 100u ? t : t + 1;
        int r = VIEW_W - 8 - view_picnic_width("BPM", &picnic_S);
        view_picnic(pixels, r, 160, "BPM", &picnic_S, DIM);
        view_picnic(pixels, r - 4 - view_picnic_width(tt, &picnic_M), 156, tt, &picnic_M, INK);
    }
}

static void view_tel_fx_stage(uint16_t *pixels, const struct panel *panel)
{
    const tg_patch *patch = &panel->sound.patch;
    static const char *const names[] = {"DRIVE", "DELAY", "REVERB"};
    static const char *const glide[] = {"OFF", "AUTO", "ON"};
    unsigned drive = patch->drive;
    unsigned delay = patch->value[TG_DELAY];
    unsigned reverb = patch->value[TG_REVERB];
    const unsigned values[] = {drive, delay, reverb, patch->glide_mode * 63u};
    for (unsigned i = 0; i < 3; i++) {
        int y = 21 + 49 * i;
        view_rect(pixels, 8, y, 224, 44, TEL_PLOT);
        view_small(pixels, 13, y + 5, names[i], DIM);
        char num[4] = {(char)('0' + values[i] / 100u % 10u), (char)('0' + values[i] / 10u % 10u), (char)('0' + values[i] % 10u), 0};
        view_picnic(pixels, 13, y + 18, values[i] >= 100u ? num : values[i] >= 10u ? num + 1 : num + 2, &picnic_M, INK);
    }
    int previous = 42;
    for (int x = 65; x < 223; x++) {
        int p = (x - 65) % 79;
        int v = (p < 40 ? p : 79 - p) - 20;
        int dry = 42 - v / 2;
        int wet = 42 - (v * (127 + drive * 3)) / (2 * 127);
        wet = (int)bounded(wet, 27, 57);
        view_rect(pixels, x, dry, 1, 1, EDGE);
        view_tel_plot_segment(pixels, x, previous, wet, CHROME_BLUE);
        previous = wet;
    }
    for (unsigned i = 0, height = 30; i < 6; i++, height = height * delay / (2u * 127u))
        view_rect(pixels, 69 + 25 * i, 107 - height, 5, height, i ? CHROME_BLUE : INK);
    view_rect(pixels, 69, 139, 4, 4, INK);
    for (unsigned i = 0; i < 32; i++) {
        int offset = (int)((i * 73u) % 25u) - 12;
        if (i * 4u < reverb)
            view_rect(pixels, 82 + i * 4, 141 + offset, 1, 1, CHROME_BLUE);
    }
    int gx = view_picnic(pixels, 8, 172, "GLIDE", &picnic_S, DIM) + 6;
    view_picnic(pixels, gx, 168, glide[patch->glide_mode], &picnic_M, patch->glide_mode ? CHROME_BLUE : INK);

}

static void view_tel_global_stage(uint16_t *pixels, const struct panel *panel)
{
    static const char *const roots[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char *const scales[] = {"OFF", "MAJOR", "MINOR", "DORIAN", "LYDIAN", "MAJOR PENT", "MINOR PENT"};
    static const char *const pedals[] = {"OFF", "LOW", "HIGH"};
    static const uint16_t sharp = (1u << 1) | (1u << 3) | (1u << 6) | (1u << 8) | (1u << 10);
    view_picnic(pixels, 8, 23, "KEY SCALE", &picnic_M, INK);
    unsigned white = 0;
    for (unsigned key = 0; key < 25; key++) {
        unsigned pitch = key % 12u;
        if (sharp & (1u << pitch)) continue;
        int x = 8 + 15 * white++;
        unsigned member = panel->scale_type && scale_contains(panel, pitch);
        view_rect(pixels, x, 48, 14, 38, member ? CHROME_BLUE : CHROME_CREAM);
        if (pitch == panel->scale_root) view_rect(pixels, x + 5, 78, 4, 4, CHROME_RED);
    }
    white = 0;
    for (unsigned key = 0; key < 25; key++) {
        unsigned pitch = key % 12u;
        if (!(sharp & (1u << pitch))) { white++; continue; }
        int x = 8 + 15 * white - 5;
        unsigned member = panel->scale_type && scale_contains(panel, pitch);
        view_rect(pixels, x, 48, 9, 24, member ? TEL_FILL : TEL_STAGE);
        if (pitch == panel->scale_root) view_rect(pixels, x + 3, 65, 4, 4, CHROME_RED);
    }
    /* The scale named big under the keys; the five settings as tiles, value in Medium. */
    char scale[16];
    unsigned at = 0;
    for (const char *c = roots[panel->scale_root]; *c; c++) scale[at++] = *c;
    scale[at++] = ' ';
    for (const char *c = scales[panel->scale_type]; *c && at < 15u; c++) scale[at++] = *c;
    scale[at] = 0;
    view_picnic(pixels, 8, 89, panel->scale_type ? scale : "SCALE OFF", &picnic_M, panel->scale_type ? CHROME_BLUE : DIM);
    char keys[3] = {(char)('0' + (panel->midi_channel + 1u) / 10u), (char)('0' + (panel->midi_channel + 1u) % 10u), 0};
    char pads[3] = {(char)('0' + (panel->pad_midi_channel + 1u) / 10u), (char)('0' + (panel->pad_midi_channel + 1u) % 10u), 0};
    char profile[2] = {(char)('1' + panel->profile), 0};
    const char *label[5] = {"KEYS CH", "PADS CH", "USB OUT", "PROFILE", "PEDAL"};
    const char *value[5] = {keys, pads, panel->midi_tx ? "ON" : "OFF", profile, pedals[panel->pedal_mode]};
    for (unsigned i = 0; i < 5; i++) {
        int x = 8 + (int)(i % 2u) * 114, y = 112 + (int)(i / 2u) * 23;
        view_rect(pixels, x, y, 110, 21, TEL_PLOT);
        view_small(pixels, x + 4, y + 7, label[i], DIM);
        uint16_t ink = (i == 2 && panel->midi_tx) || (i == 4 && panel->pedal_down) ? CHROME_BLUE : INK;
        view_picnic(pixels, x + 106 - view_picnic_width(value[i], &picnic_M), y + 1, value[i], &picnic_M, ink);
    }
}

/* Assign: the control being mapped big (KNOB 03), the panel picture with a line to it, and
 * where it goes, big. Saving state on one line. */
static void view_tel_assign_stage(uint16_t *pixels, const struct panel *panel)
{
    static const char *const sources[] = {"KNOB", "FADER", "PAD"};
    unsigned source = panel->assign_source;
    unsigned slot = panel->assign_slot;
    view_label_number(pixels, 8, 20, sources[source], slot + 1u, DIM, CHROME_BLUE);
    char info[16] = "PRE 1";
    info[4] = (char)('1' + panel->profile);
    if (source != MAP_PAD) {
        unsigned bank = source == MAP_KNOB ? panel->knob_bank : panel->fader_bank;
        static const char tmpl[] = "BANK 1 PRE 1";
        for (unsigned i = 0; i < sizeof tmpl; i++) info[i] = tmpl[i];
        info[5] = (char)('1' + bank); info[11] = (char)('1' + panel->profile);
    }
    view_small(pixels, VIEW_W - 8 - view_small_width(info), 34, info, DIM);
    const int dy = 18;
    view_rect(pixels, 8, 40 + dy, 224, 62, TEL_PLOT);
    for (unsigned k = 0; k < 8; k++) {
        int col = 18 + 16 * k;
        unsigned selected = source == MAP_KNOB && slot == k;
        view_rect(pixels, col + 2, 46 + dy, 6, 10, selected ? CHROME_BLUE : TEL_GRID);
        view_rect(pixels, col, 48 + dy, 10, 6, selected ? CHROME_BLUE : TEL_GRID);
    }
    for (unsigned f = 0; f < 4; f++)
        view_rect(pixels, 153 + 17 * f, 50 + dy, 13, 4, source == MAP_FADER && slot == f ? CHROME_BLUE : INK);
    for (unsigned pad = 0; pad < TG_PADS; pad++)
        view_rect(pixels, 18 + 25 * (pad % 8u), 64 + dy + 16 * (pad / 8u), 18, 12,
                  source == MAP_PAD && slot == pad ? CHROME_BLUE : TEL_GRID);
    /* Where it goes: the destination's name, as big as it fits. */
    char note[6];
    const char *dest;
    if (source == MAP_PAD) dest = jukebox_note_name(note, panel->pad_note[slot]);
    else {
        unsigned control = source == MAP_FADER ? panel->fader_map[panel->fader_bank][slot] :
                                                 panel->knob_map[panel->knob_bank][slot];
        dest = control_label[control];
    }
    view_picnic(pixels, 8, 128, "TO", &picnic_S, DIM);
    jukebox_big(pixels, 26, 116, VIEW_W - 34, dest, INK);
    const char *status = panel->library_status == LIB_UNAVAILABLE ? "STORAGE UNAVAILABLE" :
                         panel->library_status == LIB_WAIT_STOP ? "STOP TO SAVE" :
                         panel->library_status == LIB_WAIT_QUIET ? "WAITING FOR SILENCE" :
                         panel->library_status == LIB_FAILED ? "SAVE FAILED: REC RETRY" :
                         panel_layout_edited(panel) ? "LAYOUT EDITED" :
                         panel->library_dirty ? "LAYOUT NOT SAVED" :
                         panel->library_status == LIB_SAVED ? "LAYOUT SAVED" : NULL;
    if (status) view_status_line(pixels, 162, status, panel_layout_edited(panel) || panel->library_dirty ? CORAL : DIM);
}

static const char *view_tel_library_name(const struct panel *panel, unsigned slot)
{
    if (!library_occupied(panel, slot)) return NULL;
    switch (panel->library_kind) {
    case LIB_FACTORY: return tg_presets[slot].name;
    case LIB_IMPORTED: return library_imported(panel, slot)->sound.name;
    case LIB_SOUNDS: return user_library.patches[slot].name;
    default: return user_library.phrases[slot].name;
    }
}

static void view_tel_library_stage(uint16_t *pixels, const struct panel *panel)
{
    static const char *const factory[] = {"ESSENTIALS", "BASS", "LEAD", "MOTION", "1982 FACTORY", "1982 FACTORY", "1982+PATCHWORK"};
    unsigned imported = panel->library_kind == LIB_IMPORTED;
    const char *title = panel->library_kind == LIB_FACTORY ? factory[panel->preset_bank] :
                        panel->library_kind == LIB_SOUNDS ? "YOUR SOUNDS" : imported ?
                        tg_imported_category_name(panel->import_category) : "YOUR PHRASES";
    /* A jukebox over the bank: the shelf and page, the sound before, the chosen one big, the one
     * after, and the sixteen pads as a small map (lit: chosen, bright: filled). */
    {
        char page[24];
        unsigned at = 0;
        for (const char *c = title; *c && at < 14u; c++) page[at++] = *c;
        page[at++] = ' ';
        unsigned bank = panel->preset_bank + 1u;
        if (bank >= 10u) page[at++] = (char)('0' + bank / 10u);
        page[at++] = (char)('0' + bank % 10u);
        if (imported) {
            unsigned pages = (tg_imported_match_count(panel->import_category) + TG_PADS - 1u) / TG_PADS;
            page[at++] = '/';
            if (pages >= 10u) page[at++] = (char)('0' + pages / 10u);
            page[at++] = (char)('0' + pages % 10u);
        }
        page[at] = 0;
        view_status_line(pixels, 20, page, DIM);
        if (imported) view_small(pixels, VIEW_W - 8 - view_small_width("K8 FAMILY"), 25, "K8 FAMILY", CHROME_BLUE);
    }
    unsigned base = panel->preset_bank * TG_PADS;
    unsigned centre = panel->library_kind == LIB_FACTORY ? panel->preset : panel->library_slot;
    if (centre < base || centre >= base + TG_PADS) centre = base;
    if (!panel->save_mode) {
        for (int d = -1; d <= 1; d++) {
            int slot = (int)centre + d;
            if (slot < (int)base || slot >= (int)(base + TG_PADS)) continue;
            const char *name = view_tel_library_name(panel, (unsigned)slot);
            if (!d) {
                view_rect(pixels, 0, 64, VIEW_W, 46, library_selected(panel, (unsigned)slot) ? CHROME_CREAM : TEL_FILL);
                jukebox_big(pixels, 8, 69, VIEW_W - 16, name ? name : "EMPTY", TEL_STAGE);
            } else {
                char caps[32];
                const char *text = jukebox_caps(name ? name : "-", caps);
                view_picnic(pixels, 8, d < 0 ? 42 : 114, text, view_picnic_width(text, &picnic_M) <= VIEW_W - 16 ? &picnic_M : &picnic_S, DIM);
            }
        }
    }
    for (unsigned pad = 0; pad < TG_PADS; pad++) {
        unsigned slot = base + pad;
        int x = 8 + (int)(pad % 8u) * 28, y = 138 + (int)(pad / 8u) * 10;
        uint16_t c = library_selected(panel, slot) ? CHROME_BLUE : slot == centre ? TEL_FILL :
                     view_tel_library_name(panel, slot) ? TEL_GRID : TEL_PLOT;
        unsigned glow = pad_hit_glow(panel, pad);
        if (glow) c = picnic_mix(c, CHROME_CREAM, glow * 3u);
        view_rect(pixels, x, y, 26, 8, c);
    }
    if (panel->save_mode) {
        view_status_line(pixels, 42, panel->save_mode == SAVE_PICK ? "CHOOSE A PAD" :
                         library_occupied(panel, panel->library_slot) ? "REC REPLACES IT" : "REC STORES IT", CORAL);
        view_rect(pixels, 0, 64, VIEW_W, 46, TEL_FILL);
        jukebox_big(pixels, 8, 69, VIEW_W - 16, panel->save_name, INK);
        int cx = 8 + view_picnic_width(jukebox_caps(panel->save_name, (char[32]){0}), &picnic_M) * 0;
        (void)cx;
        view_rect(pixels, 8 + panel->name_cursor * 11, 106, 10, 3, CHROME_BLUE);
        view_small(pixels, 8, 118, "K1 LETTER   K2 CURSOR", DIM);
        return;
    }
    const char *name = panel_sound_name(panel);
    if (panel->library_kind != LIB_FACTORY && !panel->compare_active)
        name = view_tel_library_name(panel, panel->library_slot);
    unsigned long_name = 0;
    int status_y = 162;
    const char *status = panel->library_status == LIB_UNAVAILABLE ? "STORAGE UNAVAILABLE" :
                         panel->library_status == LIB_WAIT_STOP ? "STOP / RELEASE TO SAVE" :
                         panel->library_status == LIB_WAIT_QUIET ? "WAITING FOR SILENCE" :
                         panel->library_status == LIB_FAILED ? "SAVE FAILED" :
                         panel->library_status == LIB_EMPTY ? "EMPTY - KEPT CURRENT" :
                         panel->library_dirty || panel->library_status == LIB_SESSION ? "SESSION ONLY - NOT SAVED" :
                         panel->library_status == LIB_SAVED ? "SAVED" :
                         panel->compare_active ? "COMPARING: LIVE" : NULL;
    if (panel->library_kind == LIB_PHRASES && panel->library_status == LIB_NORMAL &&
        panel->telegram_library == panel->library_slot + 1u) {
        status = panel->telegram_cue == panel->telegram_applied ? "PHRASE LOADED" :
                 conductor_clock_lost(panel) ? "PHRASE CLOCK LOST" :
                 conductor_running(panel) ? "PHRASE NEXT BAR" : "PHRASE LOADING";
    }
    if (imported && name && !panel->compare_active && panel->library_status == LIB_NORMAL) {
        unsigned flags = library_imported(panel, panel->library_slot)->flags;
        if (!library_selected(panel, panel->library_slot)) {
            int col = view_small(pixels, 8, status_y, "PAD", DIM) + 6;
            col = view_small_num(pixels, col, status_y, panel->library_slot % TG_PADS + 1u, 2, CHROME_BLUE) + 6;
            view_small(pixels, col, status_y, "TO LOAD", DIM);
            status = NULL;
        } else status = flags & TG_IMPORT_FM1 ? "LOADED - FM1 DRAFT" :
                        flags & TG_IMPORT_STUDIO ? "LOADED - STUDIO DRAFT" :
                        flags & TG_IMPORT_QUIET ? "LOADED - QUIET OR SLOW" :
                        flags & (TG_IMPORT_ADAPTED | TG_IMPORT_GAPS) ? "LOADED - ADAPTED MONO" : "LOADED - REC TO COPY";
    }
    if (status) view_status_line(pixels, status_y, status, panel->library_status == LIB_FAILED ? CORAL : DIM);
    (void)long_name; (void)name;
}

static unsigned view_tel_stage(uint16_t *pixels, const struct panel *panel, unsigned head, unsigned note)
{
    view_rect(pixels, 0, 18, VIEW_W, 166, TEL_STAGE);
    switch (panel->page) {
    case PAGE_TONE: view_tel_play_stage(pixels, panel, note); break;
    case PAGE_STEPS: view_tel_sequence_stage(pixels, panel, head); break;
    case PAGE_MANUAL: case PAGE_FILTER_ENV: view_tel_shape_stage(pixels, panel); break;
    case PAGE_GLOBAL: view_tel_global_stage(pixels, panel); break;
    case PAGE_ASSIGN: view_tel_assign_stage(pixels, panel); break;
    case PAGE_PATCH: view_tel_library_stage(pixels, panel); break;
    default: return 0;
    }
    return 1;
}
#endif

static void view_telegram(uint16_t *pixels, const struct panel *panel, unsigned head, unsigned note,
                          const char *const *rate_name, const char *step_help)
{
    static const char *const page_name[] = {"PLAY", "SEQUENCE", "LIBRARY", "SHAPE", "ASSIGN", "FX", "GLOBAL", "VCF ENV", "ARP"};
    static const char *const bank_name[] = {"ESSENTIALS", "BASS", "LEAD", "MOTION", "1982 FACTORY", "1982 FACTORY", "1982+PATCHWORK"};
    static const char *const shape_name[] = {"SOURCE MIXER", "VCF", "ENVELOPE", "MODULATOR"};
    static const char *const arp_name[] = {"ARP OFF", "ARP UP", "ARP DOWN", "ARP EXCL",
        "ARP INCL", "ARP RANDOM", "ARP ORDER", "ARP REPEAT"};
    const tg_controls *sound = &panel->sound;
#ifdef ELITE_DRUMS808
    if (panel->repeat_edit || panel->page == PAGE_ARP) {
        view_rect(pixels, 0, 18, VIEW_W, 166, TEL_STAGE);
        view_tel_rhythm_stage(pixels, panel, rate_name, note);
        return;
    }
    if (panel->page == PAGE_FX && !panel->focus_control) {
        view_rect(pixels, 0, 18, VIEW_W, 166, TEL_STAGE);
        view_tel_fx_stage(pixels, panel);
        return;
    }
#else
    if (panel->repeat_edit) {
        view_title(pixels, 10, 55, "NOTE REPEAT", INK);
        view_text(pixels, 10, 91, panel->repeat_on ? "ON" : "OFF", 2, LIME);
        view_text(pixels, 10, 135, "K1 RATE", 1, DIM);
        view_text(pixels, 88, 135, "K2 GATE", 1, DIM);
        view_text(pixels, 166, 135, "K3 SWING", 1, DIM);
        view_text(pixels, 10, 157, rate_name[sound->repeat_rate], 1, SKY);
        view_num(pixels, 88, 157, sound->repeat_gate ? sound->repeat_gate : 75, 3, SKY);
        view_num(pixels, 166, 157, 50u + sound->repeat_swing, 2, SKY);
        view_text(pixels, 10, 187, panel->repeat_latch ? "K4 LATCH ON" : "K4 LATCH OFF", 1, DIM);
        view_text(pixels, 10, 215, "OR PLAY A RATE KEY", 1, DIM);
        return;
    }
    if (panel->page == PAGE_ARP) {
        view_arp_editor(pixels, panel, rate_name[sound->arp_rate]);
        return;
    }
#endif
    if (panel->arp_focus) {
        view_title(pixels, 10, 55, "ARPEGGIATOR", INK);
        view_text(pixels, 10, 99, arp_name[sound->arp], 2, LIME);
        view_text(pixels, 10, 151, "RATE", 1, DIM);
        view_text(pixels, 90, 151, "OCTAVES", 1, DIM);
        view_text(pixels, 168, 151, "GATE %", 1, DIM);
        view_text(pixels, 10, 179, rate_name[sound->arp_rate], 1, SKY);
        view_num(pixels, 90, 179, sound->arp_octaves + 1u, 1, SKY);
        view_num(pixels, 168, 179, sound->arp_gate ? sound->arp_gate : 75, 3, SKY);
        view_text(pixels, 10, 214, "SW", 1, DIM);
        view_num(pixels, 34, 214, 50u + sound->arp_swing, 2, SKY);
        view_text(pixels, 70, 214, panel->arp_latch ? "LATCH" : "LIVE", 1, panel->arp_latch ? LIME : DIM);
        if (sound->clock.source == TG_CLOCK_USB) {
            view_text(pixels, 148, 214, "USB SYNC", 1, SKY);
        } else {
            view_num(pixels, 148, 214, tg_arp_tempo(sound), 3, SKY);
            view_text(pixels, 175, 214, "BPM", 1, DIM);
        }
        return;
    }
#ifndef ELITE_DRUMS808
    if (panel->focus_control) {
        view_control_focus(pixels, panel);
        return;
    }
#endif
#ifdef ELITE_DRUMS808
    if (view_tel_stage(pixels, panel, head, note)) return;
    if (panel->page == PAGE_STEPS) view_edit_pattern(pixels, panel);
    else
#endif
    {
        view_text(pixels, 10, 31, page_name[panel->page], 1, SKY);
        if (panel->page == PAGE_TONE || panel->page == PAGE_MANUAL) {
            unsigned bank = panel->knob_bank;
            unsigned custom = memcmp(panel->knob_map[bank], tg_default_knob_map[bank], sizeof panel->knob_map[bank]) != 0;
            view_text(pixels, 56, 31, bank ? (custom ? "2 USER" : "2 MOD") : (custom ? "1 USER" : "1 TONE"), 1, LIME);
        }
        view_text(pixels, 110, 31, sound->clock.source == TG_CLOCK_USB ? "USB" : "INT", 1, DIM);
        if (panel->page == PAGE_GLOBAL || panel->page == PAGE_ASSIGN) {
            view_text(pixels, 153, 31, panel->page == PAGE_GLOBAL ? "K6 PRE" : "PRE", 1, DIM);
            view_num(pixels, 210, 31, panel->profile + 1u, 1, SKY);
        } else if (panel->repeat_on && performance_page(panel->page)) {
            view_text(pixels, 166, 31, "REPEAT", 1, LIME);
        } else if (sound->transpose && panel->page != PAGE_PATCH) {
            view_text(pixels, 166, 31, sound->transpose < 0 ? "TR -" : "TR +", 1, CORAL);
            view_num(pixels, 198, 31, (unsigned)(sound->transpose < 0 ? -sound->transpose : sound->transpose), 2, CORAL);
        }
    }
    if (panel->page == PAGE_TONE) {
        view_short_text(pixels, 10, 51, panel_sound_name(panel), 17, INK);
        if (panel_sound_edited(panel)) view_text(pixels, 152, 51, "*", 1, CORAL);
        view_note(pixels, 176, 46, note, 2, LIME);
        view_wave(pixels, &sound->patch, note != 0);
        for (unsigned k = 0; k < 8; k++) {
            unsigned p = panel->knob_map[panel->knob_bank][k];
            unsigned value = control_value(sound, p);
            view_dial(pixels, 10 + (int)(k % 4u) * 56, 98 + (int)(k / 4u) * 49,
                      k, p == CTRL_TEMPO && sound->clock.source == TG_CLOCK_USB ? "USBCLK" :
                      p == CTRL_PWM_SOURCE ? "PW SRC" : p == TG_LFO_RATE ? "LFO RT" : p == TG_VIBRATO ? "VCO" :
                      p == TG_FILTER_LFO ? "F LFO" : control_label[p], p, value, p == CTRL_TEMPO ? 300 : (control_switch(p)) ? 1 : control_five_positions(p) ? 4 : 127, p == TG_DELAY || p == TG_REVERB ? CORAL : LIME,
                      p != CTRL_TEMPO || sound->clock.source != TG_CLOCK_USB || sound->clock.period_us);
        }
        if (panel->key_transpose) {
            int shift = panel->key_transpose;
            view_text(pixels, 10, 202, shift < 0 ? "KEY -" : "KEY +", 1, LIME);
            view_num(pixels, 50, 202, (unsigned)(shift < 0 ? -shift : shift), 2, LIME);
        } else {
            view_text(pixels, 10, 202, panel->pad_keys ? "PADS: NOTES" : sound->arp ? arp_name[sound->arp] : "PADS: SOUNDS", 1, DIM);
        }
        if (panel->hold) view_text(pixels, 105, 202, "HOLD", 1, LIME);
        if (sound->arp || sound->arp_rate)
            view_text(pixels, 137, 202, rate_name[sound->arp_rate], 1, SKY);
        if (sound->clock.source == TG_CLOCK_USB && !sound->clock.period_us) view_text(pixels, 180, 202, "---", 1, DIM);
        else view_num(pixels, 180, 202, control_value(sound, CTRL_TEMPO), 3, INK);
        view_text(pixels, 207, 202, "BPM", 1, DIM);
    } else if (panel->page == PAGE_GLOBAL) {
        static const char *const roots[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        static const char *const scales[] = {"OFF", "MAJOR", "MINOR", "DORIAN", "LYDIAN", "MAJOR PENT", "MINOR PENT"};
        static const char *const pedal[] = {"K7 PED OFF", "K7 PED LOW", "K7 PED HIGH"};
        view_title(pixels, 10, 56, "KEY SCALE", INK);
        view_text(pixels, 140, 57, pedal[panel->pedal_mode], 1, panel->pedal_down ? LIME : DIM);
        view_text(pixels, 10, 85, "K1 ROOT", 1, DIM);
        view_text(pixels, 10, 106, roots[panel->scale_root], 2, SKY);
        view_text(pixels, 74, 85, "K2 TYPE", 1, DIM);
        view_text(pixels, 74, 106, scales[panel->scale_type], 2, LIME);
        for (unsigned pitch = 0; pitch < SCALE_ROOTS; pitch++) {
            int x = 10 + (int)pitch * 18;
            unsigned root = pitch == panel->scale_root;
            unsigned member = panel->scale_type && scale_contains(panel, pitch);
            unsigned color = root ? SKY : member ? LIME : CARD;
            view_rect(pixels, x, 153, 16, 29, color);
            view_text(pixels, x, 160, roots[pitch], 1, root || member ? PAPER : INK);
        }
        view_text(pixels, 10, 192, "K3 KEY CH", 1, DIM);
        view_text(pixels, 95, 192, "K4 PAD CH", 1, DIM);
        view_text(pixels, 180, 192, "K5 TX", 1, DIM);
        view_num(pixels, 10, 216, panel->midi_channel + 1u, 2, SKY);
        view_num(pixels, 95, 216, panel->pad_midi_channel + 1u, 2, SKY);
        view_text(pixels, 180, 216, panel->midi_tx ? "ON" : "OFF", 1, panel->midi_tx ? LIME : DIM);
        return;
    } else if (panel->page == PAGE_FX) {
        static const char *const glide[] = {"OFF", "AUTO", "ON"};
        view_title(pixels, 10, 55, "EFFECTS", INK);
        for (unsigned k = 0; k < sizeof fx_map; k++) {
            unsigned control = fx_map[k];
            view_dial(pixels, 10 + (int)k * 74, 96, k, control_label[control], control,
                      control_value(sound, control), 127, CORAL, 1);
        }
        view_text(pixels, 10, 165, "K4 GLIDE", 1, DIM);
        view_text(pixels, 110, 165, glide[sound->patch.glide_mode], 1, LIME);
        view_text(pixels, 10, 203, "FX BACK   BT PLAY", 1, INK);
        return;
    } else if (panel->page == PAGE_FILTER_ENV) {
        static const char *const modes[] = {"SHARED", "OPEN", "CLOSE"};
        static const char *const stages[] = {"ATTACK", "DECAY", "SUST", "REL"};
        view_title(pixels, 10, 55, "FILTER ENV", INK);
        view_text(pixels, 10, 78, "K1 MODE", 1, DIM);
        view_text(pixels, 74, 78, modes[sound->patch.filter_env_mode], 1, LIME);
        view_text(pixels, 151, 78, "K2 AMT", 1, DIM);
        view_num(pixels, 204, 78, sound->patch.value[TG_ENV], 3, SKY);
        view_text(pixels, 10, 99, "K3 TONE", 1, DIM);
        view_num(pixels, 74, 99, sound->patch.accent_tone, 3, SKY);
        view_text(pixels, 130, 99, "K4 LEVEL", 1, DIM);
        view_num(pixels, 204, 99, sound->patch.accent_level, 3, SKY);
        for (unsigned i = 0; i < TG_ADSR_STAGES; i++) {
            int x = 12 + (int)i * 56;
            unsigned value = tg_filter_adsr_value(&sound->patch, i);
            int level = 170 - (int)(value * 47u / 127u);
            view_rect(pixels, x + 20, 123, 5, 47, EDGE);
            view_rect(pixels, x + 20, level, 5, 172 - level, SKY);
            view_rect(pixels, x + 10, level - 2, 25, 5, fader_caught & (1u << i) ? LIME : INK);
            view_num(pixels, x + 10, 179, value, 3, INK);
            view_text(pixels, x, 197, stages[i], 1, DIM);
        }
        view_text(pixels, 10, 222, "PARA BACK  BT PLAY", 1, DIM);
        return;
    } else if (panel->page == PAGE_MANUAL) {
        view_title(pixels, 10, panel->fader_bank == 1 ? 54 : 58, shape_name[panel->fader_bank], INK);
        if (panel->fader_bank == 1) {
            view_text(pixels, 10, 71, "VOLTAGE-CONTROLLED FILTER", 1, DIM);
            view_text(pixels, 10, 205, "PARA FILTER ENV", 1, SKY);
        }
        if (panel->fader_bank == 0) {
            static const char *const sub_wave[] = {"-1 OCT SQUARE", "-2 OCT SQUARE", "-2 OCT PULSE"};
            view_text(pixels, 10, 77, "K8 SUB WAVE", 1, DIM);
            view_text(pixels, 10, 93, sub_wave[sound->patch.sub_mode], 1, LIME);
        } else if (panel->fader_bank == 2) {
            static const char *const trigger[] = {"GATE+TRIG", "GATE", "LFO"};
            view_text(pixels, 10, 77, "K7 ENV TRIG", 1, DIM);
            view_text(pixels, 10, 93, trigger[sound->patch.trigger], 1, LIME);
            view_text(pixels, 146, 77, "K8 VCA", 1, DIM);
            view_text(pixels, 146, 93, sound->patch.amp_gate ? "GATE" : "ENV", 1, SKY);
        } else if (panel->fader_bank == 3) {
            static const char *const range[] = {"16'", "8'", "4'", "2'"};
            static const char *const wave[] = {"TRI", "SQUARE", "S&H", "NOISE"};
            view_text(pixels, 10, 77, "K5 VCO", 1, DIM);
            view_num(pixels, 10, 93, sound->patch.value[TG_VIBRATO], 3, SKY);
            view_text(pixels, 66, 77, "K6 PWM", 1, DIM);
            view_text(pixels, 66, 93, pwm_source_name[sound->patch.pwm], 1, SKY);
            view_text(pixels, 122, 77, "K7 LFO", 1, DIM);
            view_text(pixels, 122, 93, wave[sound->patch.lfo_wave], 1, LIME);
            view_text(pixels, 178, 77, "K8 RNG", 1, DIM);
            view_text(pixels, 178, 93, range[sound->patch.range], 1, LIME);
        }
        for (unsigned f = 0; f < 4; f++) {
            unsigned p = panel->fader_map[panel->fader_bank][f];
            unsigned value = control_value(sound, p);
            unsigned max = p == CTRL_TEMPO ? 300 : (control_switch(p)) ? 1 : control_five_positions(p) ? 4 : 127;
            int x = 13 + (int)f * 56;
            int top = panel->fader_bank == 1 ? 93 : 115;
            int level = 158 - (int)((value < max ? value : max) * (158u - (unsigned)top) / max);
            view_rect(pixels, x + 20, top - 2, 5, 162 - top, EDGE);
            view_rect(pixels, x + 20, level, 5, 160 - level, SKY);
            view_rect(pixels, x + 10, level - 2, 25, 5, fader_caught & (1u << f) ? LIME : INK);
            if (p == CTRL_TEMPO && sound->clock.source == TG_CLOCK_USB && !sound->clock.period_us)
                view_text(pixels, x + 10, 165, "---", 1, DIM);
            else if(control_switch(p)) view_text(pixels,x+10,165,value ? "ON" : "OFF",1,INK);
            else if(p == CTRL_PWM_SOURCE) view_text(pixels, x+10, 165, pwm_source_name[value], 1, SKY);
            else if(control_octave(p)) view_octave(pixels,x+10,165,value,INK);
            else view_num(pixels, x + 10, 165, value, 3, INK);
            char name[7] = {0};
            const char *label = p == CTRL_OSC_OCTAVE ? "OSC OC" : p == CTRL_KEY_OCTAVE ? "KEY OC" :
                                p == TG_LFO_RATE ? "LFO RT" : p == TG_FILTER_LFO ? "VCF MD" :
                                p == TG_SUSTAIN ? "SUST" : p == TG_RELEASE ? "REL" :
                                p == CTRL_TEMPO && sound->clock.source == TG_CLOCK_USB ? "USBCLK" : control_label[p];
            for (unsigned j = 0; j < sizeof name - 1 && label[j]; j++) name[j] = label[j];
            view_text(pixels, x, 187, name, 1, DIM);
        }
        if (panel->fader_bank == 3) {
            int cents = sound->tune_cents;
            unsigned magnitude = (unsigned)(cents < 0 ? -cents : cents);
            char tune[] = {cents < 0 ? '-' : '+', (char)('0' + magnitude / 10),
                           (char)('0' + magnitude % 10), 'C', 0};
            view_text(pixels, 10, 205, "K4 TUNE", 1, DIM);
            view_text(pixels, 79, 205, tune, 1, LIME);
        } else if (panel->fader_bank != 1) {
            view_text(pixels, 10, 205, arp_name[sound->arp], 1, DIM);
            if (panel->hold) view_text(pixels, 98, 205, "HOLD", 1, LIME);
        }
        if (sound->patch.filter_low) view_text(pixels, 142, 205, "LOW", 1, SKY);
        if (sound->patch.env_wide) view_text(pixels, 174, 205, "ENV X2", 1, CORAL);
    } else if (panel->page == PAGE_ASSIGN) {
        static const char *const source[] = {"KNOB", "FADER", "PAD"};
        view_text(pixels, 10, 55, source[panel->assign_source], 2, INK);
        view_num(pixels, 125, 61, panel->assign_slot + 1u, 2, LIME);
        if (panel->assign_source != MAP_PAD) {
            view_text(pixels, 174, 55, "BANK", 1, DIM);
            view_num(pixels, 192, 70, (panel->assign_source == MAP_KNOB ? panel->knob_bank : panel->fader_bank) + 1u, 1, SKY);
        }
        if (panel->assign_source == MAP_PAD) {
            view_note(pixels, 10, 111, panel->pad_note[panel->assign_slot], 2, LIME);
            view_text(pixels, 10, 160, "PLAY A KEY TO ASSIGN", 1, DIM);
            view_text(pixels, 10, 184, "< > MOVE BY SEMITONE", 1, INK);
        } else {
            unsigned p = panel->assign_source == MAP_FADER ? panel->fader_map[panel->fader_bank][panel->assign_slot] :
                                                             panel->knob_map[panel->knob_bank][panel->assign_slot];
            view_text(pixels, 10, 111, control_label[p], 2, LIME);
            view_text(pixels, 10, 160, "MOVE CONTROL OR TAP PAD", 1, DIM);
            view_text(pixels, 10, 184, "< > CHANGE DESTINATION", 1, INK);
        }
        const char *status = panel->library_status == LIB_UNAVAILABLE ? "STORAGE UNAVAILABLE" :
                             panel->library_status == LIB_WAIT_STOP ? "STOP / RELEASE TO SAVE" :
                             panel->library_status == LIB_WAIT_QUIET ? "WAITING FOR SILENCE" :
                             panel->library_status == LIB_FAILED ? "SAVE FAILED - REC RETRY" :
                             panel_layout_edited(panel) ? "LAYOUT EDITED" :
                             panel->library_dirty ? "LAYOUT NOT SAVED" :
                             panel->library_status == LIB_SAVED ? "LAYOUT SAVED" : "REC SAVES THIS PROFILE";
        view_text(pixels, 10, 205, status, 1, panel_layout_edited(panel) || panel->library_dirty ? CORAL : DIM);
    } else {
        unsigned base = step_page_base(panel, head);
        unsigned length = tg_pattern_length(sound);
        unsigned patch = panel->page == PAGE_PATCH;
        if (patch) {
            const char *title = panel->library_kind == LIB_FACTORY ? bank_name[panel->preset_bank] :
                                panel->library_kind == LIB_SOUNDS ? "YOUR SOUNDS" : panel->library_kind == LIB_IMPORTED ?
                                (panel->import_category == TG_IMPORT_ALL ? "SOUND BANK" : tg_imported_category_name(panel->import_category)) : "YOUR PHRASES";
            view_title(pixels, 10, 58, title, INK);
            if (panel->library_kind == LIB_IMPORTED) {
                unsigned count = tg_imported_match_count(panel->import_category);
                unsigned pages = (count + TG_PADS - 1u) / TG_PADS;
                view_text(pixels, 145, 31, "PAGE", 1, DIM);
                view_num(pixels, 183, 31, panel->preset_bank + 1u, 2, INK);
                view_text(pixels, 199, 31, "/", 1, DIM);
                view_num(pixels, 207, 31, pages, 2, INK);
            } else {
                view_text(pixels, 170, 31, "BANK", 1, DIM);
                view_num(pixels, 206, 31, panel->preset_bank + 1u, 2, INK);
            }
            if (panel->library_kind == LIB_IMPORTED) {
                view_text(pixels, 10, 77, panel->import_category == TG_IMPORT_ALL ? "ALL" :
                          panel->import_category == TG_IMPORT_SEQUENCE ? "SOUNDS ONLY" : "SOUND BANK", 1, DIM);
                view_num(pixels, 198, 77, tg_imported_match_count(panel->import_category), 3, DIM);
            }
            if (panel->save_mode) {
                view_text(pixels, 10, 162, panel->save_mode == SAVE_PICK ? "CHOOSE A PAD TO STORE" :
                          library_occupied(panel, panel->library_slot) ? "REC REPLACES THIS SLOT" : "REC STORES THIS SLOT", 1, CORAL);
                view_text(pixels, 10, 183, panel->save_name, 1, LIME);
                view_rect(pixels, 10 + panel->name_cursor * 8, 200, 7, 2, LIME);
                view_text(pixels, 10, 204, "K1 LETTER  K2 CURSOR", 1, DIM);
            } else {
                const char *name = panel_sound_name(panel);
                if (panel->library_kind != LIB_FACTORY && !panel->compare_active) {
                    unsigned slot = panel->library_slot;
                    name = !library_occupied(panel, slot) ? "EMPTY SLOT" : panel->library_kind == LIB_IMPORTED ?
                           library_imported(panel, slot)->sound.name : panel->library_kind == LIB_SOUNDS ?
                           user_library.patches[slot].name : user_library.phrases[slot].name;
                }
                unsigned name_lines = view_patch_name(pixels, name);
                const char *status = panel->library_status == LIB_UNAVAILABLE ? "STORAGE UNAVAILABLE" :
                                     panel->library_status == LIB_WAIT_STOP ? "STOP / RELEASE TO SAVE" :
                                     panel->library_status == LIB_WAIT_QUIET ? "WAITING FOR SILENCE" :
                                     panel->library_status == LIB_FAILED ? "SAVE FAILED" :
                                     panel->library_status == LIB_EMPTY ? "EMPTY - KEPT CURRENT" :
                                     panel->library_dirty || panel->library_status == LIB_SESSION ? "SESSION ONLY - NOT SAVED" :
                                     panel->library_status == LIB_SAVED ? "SAVED" :
                                     panel->compare_active ? "COMPARING: LIVE" : NULL;
#ifdef ELITE_DRUMS808
                if (panel->library_kind == LIB_PHRASES && panel->library_status == LIB_NORMAL &&
                    panel->telegram_library == panel->library_slot + 1u) {
                    status = panel->telegram_cue == panel->telegram_applied ? "PHRASE LOADED" :
                             conductor_clock_lost(panel) ? "PHRASE CLOCK LOST" :
                             conductor_running(panel) ? "PHRASE NEXT BAR" : "PHRASE LOADING";
                }
#endif
                if (panel->library_kind == LIB_IMPORTED && !panel->compare_active &&
                    library_occupied(panel, panel->library_slot)) {
                    unsigned flags = library_imported(panel, panel->library_slot)->flags;
                    if (!library_selected(panel, panel->library_slot)) {
                        int y = name_lines == 2 ? 201 : 184;
                        view_text(pixels, 10, y, "PAD", 1, DIM);
                        view_num(pixels, 42, y, panel->library_slot % TG_PADS + 1, 2, SKY);
                        status = "TO LOAD";
                        view_text(pixels, 66, y, status, 1, DIM);
                        status = 0;
                    } else {
                        status = flags & TG_IMPORT_FM1 ? "LOADED - FM1 DRAFT" :
                                 flags & TG_IMPORT_STUDIO ? "LOADED - STUDIO DRAFT" :
                                 flags & TG_IMPORT_QUIET ? "LOADED - QUIET OR SLOW" :
                                 flags & (TG_IMPORT_ADAPTED | TG_IMPORT_GAPS) ?
                                 "LOADED - ADAPTED MONO" : "LOADED - REC TO COPY";
                    }
                }
                if (status) view_text(pixels, 10, name_lines == 2 ? 201 : 184, status, 1, panel->library_dirty || panel->library_status == LIB_FAILED ? CORAL : DIM);
                if (name_lines == 1) view_text(pixels, 10, 204,
                    panel->compare_available && panel->library_kind != LIB_PHRASES ? "CHORD+< COMPARE" :
                    panel->library_kind == LIB_IMPORTED ? "PATCH FACTORY  PARA NEXT" : "PARA NEXT COLLECTION", 1, DIM);
            }
        } else {
            view_text(pixels, 10, 54, "STEP", 1, DIM);
            view_num(pixels, 51, 54, panel->cursor + 1u, 3, INK);
            view_text(pixels, 86, 54, "G", 1, DIM);
            view_num(pixels, 100, 54, tg_phrase_gate(sound), 3, INK);
            view_text(pixels, 124, 54, "%", 1, DIM);
            view_text(pixels, 139, 54, "LENGTH", 1, DIM);
            view_num(pixels, 200, 54, length, 3, INK);
            view_text(pixels, 10, 72, "K6 GATE    K7 LENGTH", 1, DIM);
#ifdef ELITE_DRUMS808
            if (panel->page == PAGE_STEPS) {
                view_text(pixels, 172, 72, sound->clock.source == TG_CLOCK_USB ? "USB" : "INT", 1, DIM);
                if (sound->transpose) {
                    view_text(pixels, 205, 72, sound->transpose < 0 ? "-" : "+", 1, CORAL);
                    unsigned shift = (unsigned)(sound->transpose < 0 ? -sound->transpose : sound->transpose);
                    view_num(pixels, 213, 72, shift, 2, CORAL);
                }
            }
#endif
            unsigned entry = sound->steps[panel->cursor];
            if (entry & 127u) view_note(pixels, 10, 173, entry & 127u, 2, LIME);
            else view_text(pixels, 10, 181, "REST", 1, LIME);
            if (!panel->record && !sound->running)
                view_text(pixels, 10, 156, "CHORD+REC NEW PHRASE", 1, DIM);
            if (panel->record) {
                view_text(pixels, 88, 181, panel->record == REC_NEW ? "NEW  KEY OR MIDI" : "REC  KEY OR MIDI", 1, CORAL);
            } else if (panel->cursor >= length) {
                view_text(pixels, 88, 181, "OUTSIDE LOOP", 1, DIM);
            } else if (entry & 127u) {
                view_text(pixels, 88, 181, entry & TG_TIE ? "TIE" : "NOTE", 1, SKY);
                view_text(pixels, 160, 181, "K8 PITCH", 1, DIM);
            } else {
                view_text(pixels, 88, 181, "REC THEN KEY", 1, DIM);
            }
        }
        if (!patch && panel->record) {
            view_text(pixels, 10, 203, "OVERLAP TIE  NOTE RPT REST", 1, DIM);
        } else if (!patch) {
            const char *hint = panel->phrase_undo == UNDO_SHIFT ? "SHIFTED   CHORD+< UNDO" :
                               panel->phrase_undo == UNDO_NEW ? "NEW TAKE  CHORD+< UNDO" :
                               panel->phrase_undo ? "CHORD+< UNDO   +> SHIFT" : "CHORD+> SHIFT ONE STEP";
            view_text(pixels, 10, 203, hint, 1, panel->phrase_undo ? SKY : DIM);
        }
        for (unsigned pad = 0; pad < TG_PADS; pad++) {
            unsigned index = base + pad;
            int x = 10 + (int)(pad % 8u) * 28, y = 91 + (int)(pad / 8u) * 35;
            if (!patch && index >= TG_STEPS) continue;
            unsigned selected = patch ? library_selected(panel, panel->preset_bank * TG_PADS + pad) : index == panel->cursor;
            unsigned entry = patch ? 0 : sound->steps[index];
            uint16_t color = !patch && index == head ? SKY : selected ? LIME :
                             patch ? (library_occupied(panel, panel->preset_bank * TG_PADS + pad) ? CARD : PAPER) : index < length ? CARD : PAPER;
            view_rect(pixels, x, y, 25, 29, color);
            view_num(pixels, x + 4, y + 4, pad + 1u, 2, color == CARD || color == PAPER ? INK : PAPER);
            if (patch && panel->library_kind == LIB_IMPORTED && !selected &&
                panel->library_slot == index && library_occupied(panel, index))
                view_rect(pixels, x + 3, y + 24, 19, 2, SKY);
            if (entry && index < length)
                view_rect(pixels, x + 5, y + 23, entry & TG_TIE ? 15 : 6, 2,
                          color == CARD ? LIME : PAPER);
        }
    }
    view_rect(pixels, 10, 221, 220, 1, EDGE);
    view_text(pixels, 10, 223, panel->page == PAGE_ASSIGN ? "REC SAVE LAYOUT  BT DONE" :
             panel->page == PAGE_PATCH ? (panel->save_mode ? "< > BANK   BT CANCEL" :
                 panel->library_kind == LIB_IMPORTED ? "K7 NAME  K8 FAMILY  < >" :
                 panel->library_kind == LIB_FACTORY && tg_imported_count() ? "PATCH SOUND BANK  < >" : "PADS RECALL   < > BANK") :
             panel->page == PAGE_STEPS ? (panel->record ? "K8 STEP   BT HOME" : step_help) :
             panel->page == PAGE_TONE ? "PATCH LIBRARY   SEQ STEPS" : "BT HOME  HOLD BT ASSIGN", 1, DIM);
}

#ifdef ELITE_SYNTH_PRESETS
#ifdef ELITE_ACID_DUETS
/* Acid Duets: one BB1 group per page. Left, the parts as duets (pads 1-8);
 * right, the same parts as this synth's half alone (pads 9-16). */
static void view_acid_duets(uint16_t *pixels, const struct panel *panel)
{
    unsigned count, start = duet_group_start(panel->synth_preset_page, &count);
    unsigned page = panel->synth_preset_page + 1u, pages = duet_groups();
    char position[] = {(char)('0' + page / 10u), (char)('0' + page % 10u),
                       '/', (char)('0' + pages / 10u), (char)('0' + pages % 10u), 0};
    view_picnic(pixels, 8, 22, factory_synth_duets[start].group, &picnic_M, CHROME_CREAM);
    view_small(pixels, VIEW_W - 8 - view_small_width(position), 26, position, INK);
    view_small(pixels, 4, 41, "DUET", DIM);
    view_small(pixels, 122, 41, panel->synth_preset_dillo ? "BASS ONLY" : "LEAD ONLY", DIM);
    uint16_t own = panel->synth_preset_dillo ? CHROME_303 : CHROME_ELITE;
    for (unsigned part = 0; part < count; part++) {
        unsigned index = start + part + 1u, selected = index == panel->synth_duet_selected;
        const char *name = factory_dillo_phrases[factory_synth_duets[start + part].bass].name;
        int y = 54 + (int)part * 14;
        view_rect(pixels, 4, y, selected ? 114 : 3, 12, selected ? CHROME_CREAM : EDGE);
        view_small(pixels, 10, y + 1, name, selected ? PAPER : INK);
        view_rect(pixels, 122, y, 3, 12, own);
        view_small(pixels, 128, y + 1, name, DIM);
    }
    view_small(pixels, 8, 167, "< > OR K1 GROUP  CHORD KEEPS SOUND", DIM);
}
#endif

/* A phrase row as a pitch contour: each step a column whose height follows the pitch,
 * so the line's shape reads at a glance. */
static void synth_contour(uint16_t *pixels, unsigned dillo, unsigned phrase, int y, int height, uint16_t on, uint16_t hold)
{
    uint8_t lane[DRUM_STEPS], pitch[DRUM_STEPS];
    synth_phrase_row(dillo, phrase, lane, pitch);
    unsigned lo = 127, hi = 0;
    for (unsigned s = 0; s < DRUM_STEPS; s++)
        if (lane[s]) { if (pitch[s] < lo) lo = pitch[s]; if (pitch[s] > hi) hi = pitch[s]; }
    for (unsigned s = 0; s < DRUM_STEPS; s++) {
        int x = 10 + (int)s * 14;
        view_rect(pixels, x, y + height - 2, 12, 2, VIEW_RGB(47, 40, 35));
        if (!lane[s]) continue;
        int h = hi > lo ? 6 + (int)((pitch[s] - lo) * (unsigned)(height - 8) / (hi - lo)) : height / 2;
        view_rect(pixels, x, y + height - h, 12, lane[s] == LANE_ACCENT ? 6 : 4, lane[s] == LANE_HOLD ? hold : on);
        if (lane[s] == LANE_HOLD && s) view_rect(pixels, x - 2, y + height - h, 2, 4, hold);
    }
}

static void view_synth_presets(uint16_t *pixels, const struct panel *panel)
{
    unsigned duets = synth_preset_duets(panel), dillo = panel->synth_preset_dillo;
    uint16_t accent = dillo ? SKY : LIME, ink = CHROME_CREAM;
    uint16_t hold = VIEW_RGB(107, 98, 91);
    unsigned selected = synth_selected(panel);
    if (!selected) return;
    unsigned index = selected - 1u;
    const char *source = duets ? "ACID DUETS" : synth_preset_group(panel)->name;
#ifdef ELITE_ACID_DUETS
    if (duets) source = factory_synth_duets[index].group;
#endif
    /* The phrases this entry plays: one, or a duet's bass and lead. */
    unsigned bass = dillo ? index : 0, lead = dillo ? 0 : index, has_bass = dillo, has_lead = !dillo;
#ifdef ELITE_ACID_DUETS
    if (duets) { bass = factory_synth_duets[index].bass; lead = factory_synth_duets[index].lead; has_bass = has_lead = 1; }
#endif

    if (panel->synth_info == 0) {
        /* Three titles: before, the lit one, after (on this page). */
        unsigned count = synth_page_count(panel), cursor = panel->synth_cursor < count ? panel->synth_cursor : count - 1u;
        for (int d = -1; d <= 1; d++) {
            int pad = (int)cursor + d;
            if (pad < 0 || pad >= (int)count) continue;
            unsigned at = duets ? 0 : synth_preset_at(panel, (unsigned)pad);
#ifdef ELITE_ACID_DUETS
            if (duets) at = duet_at(panel, (unsigned)pad);
#endif
            const char *name = synth_entry_name(panel, at - 1u);
            if (!d) {
                uint16_t lit = CHROME_CREAM;
                for (unsigned pad = 0; pad < TG_PADS; pad++)
                    if (pad_hit_glow(panel, pad)) lit = picnic_mix(lit, CHROME_808_ORANGE, pad_hit_glow(panel, pad) * 2u);
                view_rect(pixels, 0, 46, VIEW_W, 46, lit);
                jukebox_big(pixels, 8, 51, VIEW_W - 16, name, PAPER);
            } else {
                char caps[32];
                const char *text = jukebox_caps(name, caps);
                const picnic_face *face = view_picnic_width(text, &picnic_M) <= VIEW_W - 16 ? &picnic_M : &picnic_S;
                view_picnic(pixels, 8, (d < 0 ? 22 : 96) + (face == &picnic_M ? 0 : 4), text, face, DIM);
            }
        }
        if (has_bass) synth_contour(pixels, 1, bass, has_lead ? 120 : 124, has_lead ? 24 : 40, SKY, hold);
        if (has_lead) synth_contour(pixels, 0, lead, has_bass ? 148 : 124, has_bass ? 24 : 40, LIME, hold);
        return;
    }

    jukebox_big(pixels, 8, 22, VIEW_W - 16, synth_entry_name(panel, index), ink);
    if (panel->synth_info == 1) {
        char caps[32];
        const char *src = jukebox_caps(source, caps);
        view_picnic(pixels, 8, 74, src, view_picnic_width(src, &picnic_M) <= VIEW_W - 16 ? &picnic_M : &picnic_S, accent);
        const char *what = duets ? "PADS 1-8: BOTH" : dillo ? "303 PATTERN" : "101 PATTERN";
        view_picnic(pixels, 8, 102, what, &picnic_M, DIM);
        if (has_bass) synth_contour(pixels, 1, bass, has_lead ? 128 : 132, has_lead ? 22 : 36, SKY, hold);
        if (has_lead) synth_contour(pixels, 0, lead, has_bass ? 152 : 132, has_bass ? 22 : 36, LIME, hold);
        return;
    }

    struct synth_notes notes = {0, 0, 0, 0, 0};
    if (has_bass) synth_phrase_notes(1, bass, &notes);
    unsigned bass_on_one = notes.on_one, slides = notes.slides;
    if (has_lead) synth_phrase_notes(0, lead, &notes);
    jukebox_keys(pixels, 64, 58, notes.used, notes.root, CHROME_TEL);
    const char *tip = !notes.used ? "NO NOTES" : slides >= 3u ? "SLIDES CARRY IT" :
                      has_bass && !bass_on_one ? "THE ONE IS OPEN" : notes.notes <= 5u ? "PLAY THE GAPS" : "HOLD LONG NOTES";
    jukebox_root_tip(pixels, 126, notes.used, notes.root, tip, ink, CHROME_RED);
}
#endif
