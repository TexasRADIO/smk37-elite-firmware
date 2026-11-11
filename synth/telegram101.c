/* SPDX-License-Identifier: GPL-3.0-only */
#include "telegram101.h"

#include <string.h>

#define U7(x) ((x) * (1.0f / 127.0f))

enum { ATTACK, DECAY, RELEASE };

static float clamp(float value, float lo, float hi)
{
    return value < lo ? lo : value > hi ? hi : value;
}

/* Effective articulation model measured through public reference audio/PWM.
 * See research/TAL-ARTICULATION-MODEL-20261106.md. The shared envelope remains
 * linear; only the amplifier has the small closing offset. */
static float envelope_release_flow(float envelope)
{
    static const float flow[33] = {
        0.0325318641f, 0.231205707f, 0.406625482f, 0.544036802f, 0.660361149f,
        0.761968871f, 0.857022731f, 0.943019273f, 1.0154978f, 1.10065472f,
        1.16694783f, 1.24522124f, 1.3101349f, 1.3906283f, 1.44915338f,
        1.53443972f, 1.59444141f, 1.67274629f, 1.7554232f, 1.81159664f,
        1.90264516f, 1.98736002f, 2.04738971f, 2.13756071f, 2.23551348f,
        2.30519309f, 2.38107532f, 2.46744331f, 2.54836932f, 2.62929533f,
        2.71022134f, 2.79114734f, 2.87207335f
    };
    float position = clamp(envelope, 0, 1) * 32;
    unsigned i = (unsigned)position;
    if (i >= 32) return flow[32];
    return flow[i] + (flow[i + 1] - flow[i]) * (position - i);
}

static inline float envelope_tick(float level, uint8_t *stage, float attack,
                                  float decay, float sustain, float release)
{
    if (*stage == ATTACK) {
        level += (1.6f - level) * attack;
        if (level >= 1.0f) {
            level = 1.0f;
            *stage = DECAY;
        }
    } else if (*stage == DECAY) {
        /* The measured envelope falls faster near the peak. */
        float above_knee = level - 0.88125f;
        float rate = decay * (0.970599f + 32.9404f * (above_knee > 0 ? above_knee : 0));
        level += (sustain - level) * rate;
    } else if (level > 0) {
        level = clamp(level - envelope_release_flow(level) * release, 0, 1);
    }
    return level;
}

static float pitch_increment(unsigned note)
{
    static const float chromatic[] = {
        8.175799f, 8.661957f, 9.177024f, 9.722718f, 10.300861f, 10.913382f,
        11.562326f, 12.249857f, 12.978271f, 13.750000f, 14.567618f, 15.433853f
    };
    return chromatic[note % 12] * (float)(1u << (note / 12)) / TG_RATE;
}

#include "filter101.inc"

static int lowest_key(uint64_t down)
{
    for (unsigned key = 0; key < TG_KEYS; key++)
        if (down & (UINT64_C(1) << key))
            return (int)key;
    return -1;
}

uint64_t tg_latch_scan(tg_latch *latch, uint64_t down, unsigned enabled)
{
    down &= (UINT64_C(1) << TG_KEYS) - 1;
    if (!enabled || !latch->enabled || (!latch->down && down))
        latch->held = down;
    else
        latch->held |= down;
    latch->down = down;
    latch->enabled = enabled != 0;
    return latch->held;
}

int tg_keyboard_scan(tg_keyboard *keys, uint64_t down)
{
    int pressed = lowest_key(down & ~keys->down);
    keys->down = down;
    if (pressed >= 0)
        keys->selected = (uint8_t)pressed;
    else if (!(down & (UINT64_C(1) << keys->selected))) {
        int low = lowest_key(down);
        keys->selected = (uint8_t)(low < 0 ? 0 : low);
    }
    return pressed;
}

int tg_keyboard_note(const tg_keyboard *keys, unsigned trigger)
{
    if (!keys->down)
        return -1;
    return trigger == TG_GATE_TRIG ? keys->selected : lowest_key(keys->down);
}

/* Coefficient preparation runs on patch changes, outside the sample loop. */
static float control_scale(unsigned control, float minimum, float octaves)
{
    float exponent = U7(control) * octaves;
    unsigned whole = (unsigned)exponent;
    float x = (exponent - (float)whole) * 0.69314718f;
    float fraction = 1.0f + x * (1.0f + x * (0.5f + x * (0.16666667f + x * 0.04166667f)));
    return minimum * (float)(1u << whole) * fraction;
}

static float ranged_pitch(const tg_voice *voice, unsigned note)
{
    static const float ratio[] = {0.5f, 1.0f, 2.0f, 4.0f};
    static const float offset[] = {.0625f,.125f,.25f,.5f,1,2,4,8,16};
    int oct = voice->controls.patch.osc_octave + voice->controls.patch.key_octave;
    return pitch_increment(note == TG_NOTE_ZERO ? 0 : note) * ratio[voice->controls.patch.range] * offset[oct+4];
}

/* Continuous pitch conversion used only during a glide. Six Taylor terms on
 * [0, ln(2)) keep error below 0.02 cents; integral octaves use exact powers of 2.
 * No logarithm, pow, table search or variable-length loop runs in the audio path. */
static float continuous_pitch(float note)
{
    float low_scale = 1.0f;
    if (note < -12.0f) { note += 48.0f; low_scale = .0625f; }
    float octave = (note + 12.0f) * (1.0f / 12.0f);
    if (octave < 0) octave = 0;
    unsigned whole = (unsigned)octave;
    float x = (octave - whole) * 0.69314718056f;
    float fraction = 1.0f + x * (1.0f + x * (0.5f + x * (0.16666666667f +
        x * (0.04166666667f + x * (0.00833333333f + x * 0.00138888889f)))));
    return (4.0878995f / TG_RATE) * (float)(1u << whole) * fraction * low_scale;
}
static float ranged_note(const tg_voice *voice, unsigned note)
{
    return (float)((int)(note == TG_NOTE_ZERO ? 0 : note) +
                   ((int)voice->controls.patch.range - 1 + voice->controls.patch.osc_octave +
                    voice->controls.patch.key_octave) * 12);
}
static void snap_pitch(tg_voice *voice)
{
    voice->glide_offset = 0;
    voice->pitch = voice->target_pitch;
}

static float random_lfo_sample(tg_voice *voice)
{
    uint32_t state = voice->lfo_rng;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    voice->lfo_rng = state;
    return (float)(state >> 16) * (1.0f / 32768.0f) - 1.0f;
}

static void set_note(tg_voice *voice, unsigned note, unsigned envelope_trigger,
                     unsigned note_on)
{
    if (!note) {
        voice->gate = 0;
        voice->stage = voice->filter_stage = RELEASE;
        voice->sounding = 0;
        return;
    }
    if (note > TG_NOTE_ZERO)
        return;
    if (note != voice->sounding) {
        float current = voice->target_note + voice->glide_offset;
        voice->target_note = ranged_note(voice, note);
        voice->target_pitch = ranged_pitch(voice, note);
        if (voice->restart_pitch || !voice->pitch || voice->controls.patch.glide_mode == TG_GLIDE_OFF ||
            !voice->controls.patch.value[TG_GLIDE] ||
            (voice->controls.patch.glide_mode == TG_GLIDE_AUTO && !voice->gate))
            snap_pitch(voice);
        else voice->glide_offset = current - voice->target_note;
    }
    if (voice->controls.patch.lfo_retrigger && (!voice->gate || note_on)) {
        voice->lfo_phase = 0;
        if (voice->controls.patch.lfo_wave == TG_LFO_RANDOM)
            voice->held_random = random_lfo_sample(voice);
    }
    if (!voice->gate || envelope_trigger) {
        voice->stage = voice->filter_stage = ATTACK;
        if (!voice->gate && voice->controls.patch.trigger == TG_LFO_TRIG)
            voice->lfo_phase = 0;
    }
    voice->restart_pitch = 0;
    voice->filter_note = (uint8_t)(note == TG_NOTE_ZERO ? 0 : note);
    voice->gate = 1;
    voice->sounding = (uint8_t)note;
}

static void strike_note(tg_voice *voice, unsigned note, unsigned envelope_trigger,
                        unsigned note_on, unsigned velocity)
{
    const tg_patch *patch = &voice->controls.patch;
    if (note && (note_on || !voice->gate) && (patch->accent_tone || patch->accent_level))
        voice->accent_target = U7(velocity);
    set_note(voice, note, envelope_trigger, note_on);
}

static void repeat_strike(tg_voice *voice)
{
    const tg_controls *c = &voice->controls;
    /* A roll is a series of strikes even when the gates meet. */
    if (voice->gate) set_note(voice, 0, 0, 0);
    strike_note(voice, c->live_note, 1, 1, c->live_velocity);
}

unsigned tg_pattern_length(const tg_controls *controls)
{
    unsigned length = controls->length;
    return length == 0 ? TG_PADS : length > TG_STEPS ? TG_STEPS : length;
}

/* The existing step counter advances at the start of each six-pulse group. */
static unsigned clock_offset(unsigned phase)
{
    return phase ? TG_CLOCKS_PER_STEP - phase : 0;
}

static unsigned clock_grid_phase(const tg_clock_snapshot *clock, unsigned division)
{
    uint32_t groups = (clock->steps - clock->base) % division;
    return ((clock->seek + groups + division) * TG_CLOCKS_PER_STEP -
            clock_offset(clock->phase)) % division;
}

static unsigned arp_odd_pulse(unsigned division, unsigned swing)
{
    /* USB MIDI supplies 24 ticks per quarter. Keep swing on that grid. */
    return division + (2u * division * swing + 50u) / 100u;
}

static uint32_t arp_grid_edges(uint32_t pulses, unsigned period, unsigned odd)
{
    unsigned phase = pulses % period;
    return (pulses / period) * 2u + (phase > 0) + (phase > odd);
}

static unsigned arp_usb_elapsed(const tg_clock_snapshot *clock, unsigned period)
{
    return (clock_grid_phase(clock, period) + period - 1u) % period;
}

static unsigned arp_cycle(const tg_controls *c)
{
    unsigned count = c->arp_count * (1u + c->arp_octaves);
    if (count < 2 || c->arp == TG_ARP_REPEAT) return 1;
    if (c->arp == TG_ARP_BOUNCE) return 2u * count - 2u;
    if (c->arp == TG_ARP_INCLUSIVE) return 2u * count;
    return count;
}

static void arp_advance(tg_voice *voice, uint32_t steps)
{
    const tg_controls *c = &voice->controls;
    if (c->arp == TG_ARP_RANDOM) voice->arp_step += steps;
    else {
        unsigned cycle = arp_cycle(c);
        voice->arp_step = (voice->arp_step + steps % cycle) % cycle;
    }
}

static unsigned arp_note(tg_voice *voice, unsigned *velocity)
{
    const tg_controls *c = &voice->controls;
    unsigned count = c->arp_count * (1u + c->arp_octaves);
    unsigned index = voice->arp_step;
    arp_advance(voice, 1);
    if (c->arp == TG_ARP_REPEAT) {
        *velocity = c->live_note ? c->live_velocity : c->arp_velocity[0];
        return c->live_note ? c->live_note : c->arp_notes[0];
    }
    if (c->arp == TG_ARP_RANDOM) {
        /* Hash the clock position so coalesced edges and seeks skip directly.
         * Keep the sound engine's noise and modulation sequences independent. */
        uint32_t random = index + UINT32_C(0x9e3779b9);
        random = (random ^ (random >> 16)) * UINT32_C(0x7feb352d);
        random = (random ^ (random >> 15)) * UINT32_C(0x846ca68b);
        index = (random ^ (random >> 16)) % count;
    } else {
        index %= arp_cycle(c);
        if (c->arp == TG_ARP_DOWN) index = count - 1u - index;
        else if (index >= count) {
            unsigned turn = c->arp == TG_ARP_INCLUSIVE ? 2u * count - 1u : 2u * count - 2u;
            index = turn - index;
        }
    }
    *velocity = c->arp_velocity[index % c->arp_count];
    if (index < c->arp_count) return c->arp_notes[index];
    unsigned note = c->arp_notes[index % c->arp_count];
    unsigned pitch = note == TG_NOTE_ZERO ? 0 : note;
    pitch += 12u * (index / c->arp_count);
    while (pitch > 127u) pitch -= 12u;
    return pitch;
}

static void clock_step(tg_voice *voice, unsigned arpeggiating, unsigned emit);
static uint32_t external_gate_remaining(const tg_voice *voice);

static void repeat_usb(tg_voice *voice, const tg_clock_snapshot *old, unsigned restart)
{
    const tg_controls *c = &voice->controls;
    const tg_clock_snapshot *clock = &c->clock;
    if (clock->state != TG_CLOCK_RUNNING) {
        if (voice->gate) set_note(voice, 0, 0, 0);
        voice->repeat_gate_remaining = 0;
        return;
    }
    unsigned division = tg_rate_pulses(c->repeat_rate);
    unsigned pair = 2u * division, odd = arp_odd_pulse(division, c->repeat_swing);
    unsigned reset = old->source != TG_CLOCK_USB || old->epoch != clock->epoch;
    uint32_t groups = clock->steps - (reset ? clock->base : old->steps);
    uint32_t pulses = groups * TG_CLOCKS_PER_STEP + (reset ? 0 : clock_offset(old->phase)) -
                      clock_offset(clock->phase);
    unsigned before = reset ? clock->seek * TG_CLOCKS_PER_STEP % pair : clock_grid_phase(old, pair);
    unsigned edge = arp_grid_edges(before + pulses, pair, odd) != arp_grid_edges(before, pair, odd);
    if (!edge && (!pulses || !voice->gate || restart)) return;

    unsigned elapsed = arp_usb_elapsed(clock, pair);
    unsigned interval = odd;
    if (elapsed >= odd) {
        elapsed -= odd;
        interval = pair - odd;
    }
    unsigned gate = interval * (c->repeat_gate ? c->repeat_gate : 75u);
    unsigned remaining = gate > 100u * elapsed ? gate - 100u * elapsed : 0;
    unsigned frames = clock->period_us ?
        ((clock->period_us * remaining / 100u) * 441u + 5000u) / 10000u :
        remaining ? TG_RATE / 2u : 0;
    unsigned age = c->clock_age_us > TG_CLOCK_TIMEOUT_US ? TG_CLOCK_TIMEOUT_US : c->clock_age_us;
    unsigned elapsed_frames = age * 441u / 10000u;
    voice->repeat_gate_remaining = frames > elapsed_frames ? frames - elapsed_frames : 0;
    if (edge && voice->repeat_gate_remaining) repeat_strike(voice);
}

static int phrase_ties_across(const tg_controls *c, unsigned step, uint32_t skipped, unsigned length)
{
    if (skipped > length) skipped = length;
    for (uint32_t i = 0; i < skipped; ++i) {
        unsigned entry = c->steps[step];
        if (!(entry & TG_TIE) || !(entry & 127u)) return 0;
        if (++step == length) step = 0;
    }
    return 1;
}

static void apply_controls(tg_voice *voice, const tg_controls *controls, int launch_offset)
{
    tg_clock_snapshot old_clock = voice->controls.clock;
    unsigned external = controls->clock.source == TG_CLOCK_USB;
    unsigned clock_break = external && old_clock.gate_breaks != controls->clock.gate_breaks;
    unsigned silence = controls->silence != voice->controls.silence;
    unsigned old_live = voice->controls.live_note;
    unsigned old_repeat = voice->controls.repeat_pad;
    unsigned live_trigger = controls->live_trigger != voice->controls.live_trigger;
    unsigned live_break = controls->live_break != voice->controls.live_break;
    unsigned range_changed = controls->patch.range != voice->controls.patch.range ||
        controls->patch.osc_octave != voice->controls.patch.osc_octave ||
        controls->patch.key_octave != voice->controls.patch.key_octave;
    unsigned old_arp = voice->controls.arp;
    unsigned arp_was_active = tg_arp_active(&voice->controls);
    unsigned was_running = voice->controls.running;
    unsigned phrase_recalled = controls->phrase_epoch != voice->controls.phrase_epoch;
    const tg_patch *old_patch = &voice->controls.patch;
    const tg_patch *patch = &controls->patch;
    if (memcmp(old_patch, patch, sizeof *patch)) {
        const uint8_t *p = patch->value;
        if (patch->lfo_wave == TG_LFO_NOISE && old_patch->lfo_wave != TG_LFO_NOISE)
            voice->lfo_noise_lp = 0;
        /* Attack is time to peak. Decay/release are full-level VCA fall95
         * times; low-level release closes sooner. Glide retains its own law. */
        voice->attack = 0.980829253f / (TG_RATE * control_scale(p[TG_ATTACK], 0.0015f, 11.38082f));
        voice->decay = 2.67360553f / (TG_RATE * control_scale(p[TG_DECAY], 0.002f, 12.28771f));
        voice->release = 0.676890282f / (TG_RATE * control_scale(p[TG_RELEASE], 0.002f, 12.28771f));
        float glide_rate = 2.9957323f / (TG_RATE * control_scale(p[TG_GLIDE], 0.001f, 12.28771f));
        /* 1-exp(-rate), evaluated without subtracting nearly equal floats. */
        voice->glide = p[TG_GLIDE] ? glide_rate * (1.0f - glide_rate * (0.5f -
            glide_rate * (0.16666666667f - glide_rate * (0.04166666667f -
            glide_rate * 0.00833333333f)))) : 1.0f;
        voice->lfo_rate = control_scale(p[TG_LFO_RATE], 0.1f, 8.22882f) / TG_RATE;
        if (patch->filter_env_mode != TG_FILTER_ENV_SHARED) {
            unsigned a = tg_filter_adsr_value(patch, 0), d = tg_filter_adsr_value(patch, 1);
            unsigned s = tg_filter_adsr_value(patch, 2), r = tg_filter_adsr_value(patch, 3);
            unsigned enabling = old_patch->filter_env_mode == TG_FILTER_ENV_SHARED ||
                                old_patch->filter_env_mode > TG_FILTER_ENV_CLOSE;
            if (enabling) {
                voice->filter_envelope = voice->envelope;
                voice->filter_stage = voice->stage;
            }
            if (enabling || a != tg_filter_adsr_value(old_patch, 0))
                voice->filter_attack = 0.980829253f / (TG_RATE * control_scale(a, 0.0015f, 11.38082f));
            if (enabling || d != tg_filter_adsr_value(old_patch, 1))
                voice->filter_decay = 2.67360553f / (TG_RATE * control_scale(d, 0.002f, 12.28771f));
            voice->filter_sustain = U7(s);
            if (enabling || r != tg_filter_adsr_value(old_patch, 3))
                voice->filter_release = 0.676890282f / (TG_RATE * control_scale(r, 0.002f, 12.28771f));
        }
    }
    float travel = controls->bend * (controls->bend < 0 ? 1.0f / 512.0f : 1.0f / 511.0f);
    float shift = clamp(travel, -1.0f, 1.0f) * U7(controls->patch.value[TG_BEND_PITCH]) *
                  (4.0f * 0.05776227f);
    /* exp(shift) over +/-4 semitones, outside the per-sample loop. */
    voice->bend_ratio = 1.0f + shift * (1.0f + shift * (0.5f + shift *
        (0.16666667f + shift * (0.04166667f + shift * 0.008333333f))));
    if (controls->tune_cents) {
        float cents = clamp(controls->tune_cents, -TG_TUNE_LIMIT, TG_TUNE_LIMIT);
        float x = cents * 0.00057762265f; /* ln(2) / 1200 */
        voice->bend_ratio *= 1.0f + x * (1.0f + x * (0.5f + x / 6.0f));
    }
    if (voice->blocked_live && (controls->live_note != voice->blocked_live || live_trigger))
        voice->blocked_live = 0;
    if (silence) {
        voice->gate = voice->sounding = voice->clocking = 0;
        voice->restart_pitch = 1; /* A fresh note starts at pitch without resetting the free-running VCO. */
        voice->stage = voice->filter_stage = RELEASE;
        voice->filter_envelope = voice->envelope = voice->gate_level = voice->output_dc = 0;
        voice->accent = voice->accent_target = 0;
        voice->blocked_live = controls->silence_keep_live ? 0 : controls->live_note;
    }
    if (controls->patch.filter_idle_hold) {
        unsigned recall = controls->sound_epoch != voice->controls.sound_epoch;
        unsigned silent = !voice->gate &&
            (voice->controls.patch.amp_gate ? voice->gate_level <= 0.0001f :
                                               voice->envelope <= 0.020098f);
        if (silence || (recall && silent)) {
            memset(voice->ladder, 0, sizeof voice->ladder);
            voice->filter_idle_frames = voice->filter_started = 0;
        } else if (voice->controls.patch.filter_idle_hold != 1) {
            voice->filter_idle_frames = 0;
            voice->filter_started = voice->gate;
        }
    }
    unsigned arp_active = tg_arp_active(controls);
    unsigned internal_arp = !external && controls->running && voice->phrase_with_arp && arp_active;
    unsigned arp_clock = (external || !controls->running || internal_arp) && arp_active;
    unsigned internal_handoff = voice->internal_live_arp != internal_arp;
    unsigned arp_handoff = internal_handoff || (external && old_clock.source == TG_CLOCK_USB &&
                           voice->phrase_with_arp && arp_clock != arp_was_active);
    voice->internal_live_arp = (uint8_t)internal_arp;
    if (internal_arp && internal_handoff) {
        voice->arp_remaining = 0;
        voice->arp_step = 0;
        voice->arp_odd = 0;
    }
    voice->controls = *controls;
    if ((clock_break || arp_handoff || (controls->phrase_muted && !arp_clock)) && voice->clocking) {
        set_note(voice, 0, 0, 0);
        voice->clocking = 0;
        voice->gate_remaining = 0;
    }
    if (phrase_recalled || launch_offset >= 0) {
        voice->step = launch_offset < 0 ? 0 : (uint8_t)launch_offset;
        voice->play_step = TG_STEPS;
        voice->phrase_launch = 1;
    }
    if (range_changed && voice->pitch) {
        /* The oscillator keeps its last note through release, so the
         * range switches still act on it with the gate closed. */
        unsigned note = voice->filter_note ? voice->filter_note : TG_NOTE_ZERO;
        voice->target_note = ranged_note(voice, note);
        voice->target_pitch = ranged_pitch(voice, note);
        snap_pitch(voice);
    }
    if (controls->patch.glide_mode == TG_GLIDE_OFF || !controls->patch.value[TG_GLIDE])
        snap_pitch(voice);
    if (controls->arp != old_arp) voice->arp_step = 0;
    if (!external && ((controls->running && !was_running) ||
        (arp_clock && !internal_arp && (was_running || !arp_was_active)) ||
        (old_clock.source == TG_CLOCK_USB && (controls->running || arp_clock)))) {
        voice->remaining = 0;
        voice->gate_remaining = 0;
        voice->step = 0;
        voice->play_step = TG_STEPS;
        voice->step_odd = 0;
        voice->arp_step = 0;
        voice->arp_odd = 0;
    }
    unsigned repeat = controls->repeat_pad && controls->live_note;
    unsigned repeat_restart = !old_repeat || controls->live_note != old_live || live_trigger ||
                              old_clock.source != controls->clock.source ||
                              (external && old_clock.epoch != controls->clock.epoch) || clock_break || silence;
    if (repeat) {
        if (repeat_restart) {
            set_note(voice, 0, 0, 0);
            voice->repeat_remaining = voice->repeat_gate_remaining = voice->repeat_odd = 0;
        }
        voice->clocking = 0;
    } else if (controls->live_note && !arp_clock && !voice->blocked_live) {
        unsigned changed = controls->live_note != old_live;
        /* Taking over from the clock is a fresh key strike. Overlap
         * between live notes still follows the selected trigger mode. */
        if (voice->clocking || (live_break && old_live))
            set_note(voice, 0, 0, 0);
        if (changed || live_trigger || old_repeat || !voice->gate || voice->clocking)
            strike_note(voice, controls->live_note,
                        live_trigger && controls->patch.trigger == TG_GATE_TRIG,
                        live_trigger, controls->live_velocity);
        voice->clocking = 0;
    } else if (arp_clock ? !voice->clocking : (!controls->running || old_live)) {
        /* Release direct play as the arp takes over and waits for its edge. */
        set_note(voice, 0, 0, 0);
        voice->clocking = 0;
    }
    if (external) {
        const tg_clock_snapshot *clock = &controls->clock;
        unsigned arp = tg_arp_active(controls);
        unsigned division = arp && (controls->arp_rate || controls->arp_swing) ?
                            tg_rate_pulses(controls->arp_rate) : 0;
        unsigned pair = 2u * division, odd = arp_odd_pulse(division, controls->arp_swing);
        uint32_t previous = old_clock.steps;
        unsigned reset = old_clock.source != TG_CLOCK_USB || old_clock.epoch != clock->epoch;
        if (reset) {
            previous = clock->base;
            if (launch_offset < 0) voice->step = (uint8_t)(clock->seek % tg_pattern_length(controls));
            unsigned arp_position = division ?
                arp_grid_edges(clock->seek * TG_CLOCKS_PER_STEP, pair, odd) : clock->seek;
            voice->arp_step = 0;
            arp_advance(voice, arp_position);
            voice->play_step = TG_STEPS;
            if (voice->clocking) set_note(voice, 0, 0, 0);
            voice->clocking = 0;
        }
        uint32_t events = clock->steps - previous;
        if (events && (!controls->arp || voice->phrase_with_arp)) {
            unsigned length = tg_pattern_length(controls);
            if (voice->step >= length) voice->step = 0;
            /* A recall uses the latest edge. A seek keeps its song position. */
            if (launch_offset < 0 && (!phrase_recalled || reset)) {
                if (!arp && events > 1u && voice->clocking && voice->gate &&
                    !phrase_ties_across(controls, voice->step, events - 1u, length)) {
                    set_note(voice, 0, 0, 0);
                    voice->clocking = 0;
                }
                voice->step = (uint8_t)((voice->step + (events - 1u) % length) % length);
            }
            if (arp) {
                /* Only the arp sounds, but the next phrase step stays in time. */
                voice->play_step = voice->step;
                voice->step = (uint8_t)((voice->step + 1u) % length);
                voice->phrase_launch = 0;
            }
        }
        if (division) {
            unsigned before = reset ? (clock->seek * TG_CLOCKS_PER_STEP) % pair :
                                      clock_grid_phase(&old_clock, pair);
            uint32_t pulses = events * TG_CLOCKS_PER_STEP +
                (reset ? 0 : clock_offset(old_clock.phase)) - clock_offset(clock->phase);
            events = arp_grid_edges(before + pulses, pair, odd) - arp_grid_edges(before, pair, odd);
        }
        unsigned advance = events && (!controls->arp || arp || voice->phrase_with_arp);
        if (advance) {
            /* Skip obsolete steps, never render a burst of caught-up notes. */
            if (arp) arp_advance(voice, events - 1u);
            unsigned after_silence = clock->steps != controls->silence_clock_steps;
            if (silence && division) {
                uint32_t pulses = (clock->steps - controls->silence_clock_steps) * TG_CLOCKS_PER_STEP +
                    clock_offset(controls->silence_clock_phase) - clock_offset(clock->phase);
                unsigned elapsed = arp_usb_elapsed(clock, pair);
                if (elapsed >= odd) elapsed -= odd;
                after_silence = pulses > elapsed;
            }
            unsigned emit = clock->state == TG_CLOCK_RUNNING && (!silence || after_silence);
            clock_step(voice, arp, emit);
        }
        if (clock->state != TG_CLOCK_RUNNING && voice->clocking) {
            set_note(voice, 0, 0, 0);
            voice->clocking = 0;
        }
        if (!advance && voice->clocking &&
            (reset || old_clock.steps != clock->steps || old_clock.phase != clock->phase))
            voice->gate_remaining = external_gate_remaining(voice);
        if (repeat && !voice->blocked_live)
            repeat_usb(voice, &old_clock, repeat_restart);
    }
}

void tg_apply(tg_voice *voice, const tg_controls *controls)
{
    apply_controls(voice, controls, -1);
}

void tg_apply_usb_launch(tg_voice *voice, const tg_controls *controls, unsigned offset)
{
    apply_controls(voice, controls, (int)(offset % tg_pattern_length(controls)));
}

void tg_init(tg_voice *voice, const tg_controls *controls)
{
    memset(voice, 0, sizeof *voice);
    memset(&voice->controls.patch, 0xff, sizeof voice->controls.patch);
    voice->rng = 1;
    voice->lfo_rng = UINT32_C(0x9e3779b9);
    voice->play_step = TG_STEPS;
    voice->stage = voice->filter_stage = RELEASE;
    voice->drive = U7(controls->patch.drive);
    voice->pwm_hold = voice->pwm_slew = 0.5f;
    for (unsigned p = 0; p < TG_PARAMETERS; p++)
        voice->smooth[p] = U7(controls->patch.value[p]);
    tg_apply(voice, controls);
}

static void clock_step(tg_voice *voice, unsigned arpeggiating, unsigned emit)
{
    const tg_controls *c = &voice->controls;
    unsigned note, tie = 0, continues = 0, velocity = 0;
    if (arpeggiating) note = arp_note(voice, &velocity);
    else {
        unsigned length = tg_pattern_length(c);
        if (voice->step >= length)
            voice->step = 0;
        voice->play_step = voice->step;
        unsigned entry = c->steps[voice->step];
        voice->step = (voice->step + 1u) % length;
        tie = voice->phrase_launch ? 0 : entry & TG_TIE;
        voice->phrase_launch = 0;
        note = entry & 127u;
        if (note) {
            int shifted = (int)note + c->transpose;
            note = (unsigned)(shifted < 1 ? 1 : shifted > 127 ? 127 : shifted);
        }
        continues = c->steps[voice->step] & TG_TIE;
    }
    unsigned tempo = arpeggiating ? tg_arp_tempo(c) : c->tempo;
    if (tempo < 30) tempo = 30;
    unsigned interval;
    if (arpeggiating && c->arp_rate) {
        unsigned divisor = tempo * 24u;
        interval = (TG_RATE * 60u * tg_rate_pulses(c->arp_rate) + divisor / 2u) / divisor;
    } else interval = (TG_RATE * 15u + tempo / 2u) / tempo;
    if (arpeggiating) {
        unsigned delay = (interval * 2u * c->arp_swing + 50u) / 100u;
        interval = voice->arp_odd ? interval - delay : interval + delay;
        voice->arp_odd ^= 1u;
    } else if (c->clock.source == TG_CLOCK_INTERNAL) {
        interval = tg_step_interval(interval, tg_step_delay(interval, c->step_swing), voice->step_odd);
        voice->step_odd ^= 1u;
    }
    if (voice->internal_live_arp && arpeggiating) voice->arp_remaining = interval;
    else voice->remaining = interval;
    /* The phrase keeps its position and launch state while the live arp owns the gate. */
    if (voice->internal_live_arp && !arpeggiating) return;
    voice->phrase_gate = (uint8_t)tg_phrase_gate(c);
    unsigned gate = arpeggiating ? (c->arp_gate ? c->arp_gate : 75u) : voice->phrase_gate;
    voice->clock_tied = (uint8_t)!!continues;
    unsigned external = c->clock.source == TG_CLOCK_USB;
    voice->gate_remaining = external ? external_gate_remaining(voice) :
        continues ? interval : interval * gate / 100u;
    if (emit && !c->repeat_pad && (arpeggiating || (!c->phrase_muted && !c->live_note))) {
        voice->blocked_live = 0;
        /* Coalesced USB ticks may place this step beyond its audible gate. */
        if (external && !voice->gate_remaining) {
            set_note(voice, 0, 0, 0);
            voice->clocking = 0;
        } else {
            /* Arp notes and detached phrase steps start their own gates,
             * including at full gate or when a USB clock edge arrives early. */
            if (!tie && voice->clocking && voice->gate)
                set_note(voice, 0, 0, 0);
            strike_note(voice, note, !tie, !tie, velocity);
            voice->clocking = 1;
        }
    }
}

static uint32_t external_gate_remaining(const tg_voice *voice)
{
    const tg_clock_snapshot *clock = &voice->controls.clock;
    if (voice->clock_tied) return UINT32_MAX;
    const tg_controls *c = &voice->controls;
    uint32_t pulse_us = clock->period_us;
    if (!pulse_us && voice->phrase_with_arp) {
        /* Conductor can play a downbeat before the second USB pulse.
         * Until tempo is measured, use the same BPM as its rhythm parts. */
        unsigned tempo = c->tempo < 30u ? 30u : c->tempo;
        pulse_us = (2500000u + tempo / 2u) / tempo;
    }
    uint32_t frames;
    if (tg_arp_active(c) && (c->arp_rate || c->arp_gate || c->arp_swing)) {
        unsigned division = tg_rate_pulses(c->arp_rate);
        unsigned pair = 2u * division, odd = arp_odd_pulse(division, c->arp_swing);
        unsigned elapsed = arp_usb_elapsed(clock, pair);
        unsigned interval = odd;
        if (elapsed >= odd) {
            elapsed -= odd;
            interval = pair - odd;
        }
        unsigned gate = interval * (c->arp_gate ? c->arp_gate : 75u);
        unsigned remaining = gate > 100u * elapsed ? gate - 100u * elapsed : 0;
        frames = pulse_us ?
            ((pulse_us * remaining / 100u) * 441u + 5000u) / 10000u :
            remaining ? TG_RATE / 2u : 0;
    } else {
        unsigned elapsed = clock->phase ? clock->phase - 1u : TG_CLOCKS_PER_STEP - 1u;
        unsigned gate = (tg_arp_active(c) ? 75u : voice->phrase_gate) * TG_CLOCKS_PER_STEP;
        unsigned remaining = gate > 100u * elapsed ? gate - 100u * elapsed : 0;
        uint32_t scaled_us = pulse_us * remaining;
        /* Convert hundredths of a microsecond to 44.1 kHz frames with 32-bit math. */
        frames = pulse_us ? (scaled_us / 1000000u) * 441u +
            ((scaled_us % 1000000u) * 441u + 500000u) / 1000000u : remaining ? TG_RATE / 2u : 0;
    }
    uint32_t age = voice->controls.clock_age_us;
    if (age > TG_CLOCK_TIMEOUT_US) age = TG_CLOCK_TIMEOUT_US;
    unsigned elapsed = age * 441u / 10000u;
    return frames > elapsed ? frames - elapsed : 0;
}

static void clock_tick(tg_voice *voice)
{
    const tg_controls *c = &voice->controls;
    if (c->clock.source == TG_CLOCK_USB) {
        if (voice->clocking && !voice->gate_remaining && voice->gate) set_note(voice, 0, 0, 0);
        if (voice->gate_remaining && voice->gate_remaining != UINT32_MAX) voice->gate_remaining--;
        return;
    }
    unsigned arpeggiating = !c->running && tg_arp_active(c);
    if (!c->running && !arpeggiating) return;
    if (!voice->remaining) clock_step(voice, arpeggiating, 1);
    if (voice->internal_live_arp) {
        if (!voice->arp_remaining) clock_step(voice, 1, 1);
        voice->arp_remaining--;
    }
    if (voice->clocking && !voice->gate_remaining && voice->gate) set_note(voice, 0, 0, 0);
    if (voice->gate_remaining) voice->gate_remaining--;
    voice->remaining--;
}

static void repeat_tick(tg_voice *voice)
{
    const tg_controls *c = &voice->controls;
    if (!c->live_note || voice->blocked_live) return;
    if (c->clock.source != TG_CLOCK_USB) {
        if (!voice->repeat_remaining) {
            unsigned tempo = c->tempo < 30 ? 30 : c->tempo;
            unsigned divisor = tempo * 24u;
            unsigned interval = (TG_RATE * 60u * tg_rate_pulses(c->repeat_rate) + divisor / 2u) / divisor;
            unsigned delay = (interval * 2u * c->repeat_swing + 50u) / 100u;
            voice->repeat_remaining = voice->repeat_odd ? interval - delay : interval + delay;
            voice->repeat_odd ^= 1u;
            voice->repeat_gate_remaining = voice->repeat_remaining * (c->repeat_gate ? c->repeat_gate : 75u) / 100u;
            repeat_strike(voice);
        }
        voice->repeat_remaining--;
    }
    if (voice->repeat_gate_remaining) voice->repeat_gate_remaining--;
    else if (voice->gate) set_note(voice, 0, 0, 0);
}

/* Polynomial edge correction for the two VCO waves and the divided sub. */
static float edge(float phase, float increment, float inverse)
{
    if (phase < increment) {
        float x = phase * inverse - 1.0f;
        return -x * x;
    }
    if (phase > 1.0f - increment) {
        float x = (phase - 1.0f) * inverse + 1.0f;
        return x * x;
    }
    return 0;
}

static float pulse(float phase, float duty, float increment, float inverse, float leading)
{
    float shifted = phase - duty;
    if (shifted < 0) shifted += 1.0f;
    return (phase < duty ? 1.0f : -1.0f) + leading - edge(shifted, increment, inverse);
}

static float output_limit(float value)
{
    /* Preserve the working signal instead of compressing every source level.
     * Unit slope at the knee; asymptotic bound of +/-1 for finite input.
     * This is output protection, not a model of the SH-101's VCA circuit. */
    float magnitude = value < 0 ? -value : value;
    if (magnitude <= 0.75f) return value;
    float excess = magnitude - 0.75f;
    float limited = 0.75f + excess / (1.0f + 4.0f * excess);
    return value < 0 ? -limited : limited;
}

int tg_render(tg_voice *voice, int32_t *stereo, unsigned frames)
{
    const tg_controls *c = &voice->controls;
    const tg_patch *patch = &c->patch;
    float *p = voice->smooth;
    float level = U7(c->master) * 0.43f;
    float mod = U7(c->mod);
    float drive = U7(patch->drive);
    float accent_tone = U7(patch->accent_tone) * (3.0f * 0.09119275f);
    float accent_level = U7(patch->accent_level);
    if ((!accent_tone && !accent_level) ||
        (voice->accent < 0.00001f && voice->accent_target < 0.00001f))
        voice->accent = voice->accent_target = 0;
    /* Resonance changes only when the control smoother advances. */
    float k = filter_resonance(p[TG_RESONANCE]);
    float input_gain = filter_input_gain(p[TG_RESONANCE]);
    for (unsigned i = 0; i < frames; i++) {
        clock_tick(voice);
        if (c->repeat_pad) repeat_tick(voice);
        if ((voice->control_phase++ & 15u) == 0) {
            for (unsigned j = 0; j < TG_PARAMETERS; j++)
                p[j] += (U7(patch->value[j]) - p[j]) * 0.10f;
            k = filter_resonance(p[TG_RESONANCE]);
            input_gain = filter_input_gain(p[TG_RESONANCE]);
        }
        voice->master += (level - voice->master) * 0.008f;
        voice->envelope = envelope_tick(voice->envelope, &voice->stage, voice->attack,
                                        voice->decay, p[TG_SUSTAIN], voice->release);
        float filter_envelope = voice->envelope;
        if (patch->filter_env_mode != TG_FILTER_ENV_SHARED) {
            voice->filter_envelope = envelope_tick(voice->filter_envelope, &voice->filter_stage,
                voice->filter_attack, voice->filter_decay, voice->filter_sustain, voice->filter_release);
            filter_envelope = patch->filter_env_mode == TG_FILTER_ENV_CLOSE ?
                              -voice->filter_envelope : voice->filter_envelope;
        }
        voice->gate_level += ((voice->gate ? 1.0f : 0.0f) - voice->gate_level) * 0.03f;
        float amp = patch->amp_gate ? voice->gate_level :
            clamp(voice->envelope * 1.0203582f + (-0.0204077603f), 0, 1);
        uint32_t rng = voice->rng;
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        voice->rng = rng;
        float noise = (float)(rng >> 16) * (1.0f / 32768.0f) - 1.0f;
        float phase = voice->lfo_phase;
        float triangle = phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
        float lfo = triangle;
        if (patch->lfo_retrigger && patch->lfo_wave == TG_LFO_TRI) {
            /* Pitch/filter triangle starts at its rising zero crossing. PWM
             * continues to use the original unshifted triangle. */
            float shifted = phase + 0.25f;
            if (shifted >= 1.0f) shifted -= 1.0f;
            lfo = shifted < 0.5f ? 4.0f * shifted - 1.0f : 3.0f - 4.0f * shifted;
        }
        if (patch->lfo_wave == TG_LFO_SQUARE)
            lfo = phase < 0.5f ? 1.0f : -1.0f;
        else if (patch->lfo_wave == TG_LFO_RANDOM)
            lfo = voice->held_random;
        else if (patch->lfo_wave == TG_LFO_NOISE) {
            /* Standalone reference: 400 Hz low-pass followed by 6x gain.
             * Only the modulation route uses this filtered copy. */
            voice->lfo_noise_lp += (noise - voice->lfo_noise_lp) * 0.05539681f;
            lfo = clamp(6.0f * voice->lfo_noise_lp, -1.0f, 1.0f);
        }
        voice->lfo_phase += voice->lfo_rate;
        if (voice->lfo_phase >= 1.0f) {
            voice->lfo_phase -= 1.0f;
            voice->pwm_hold = (noise + 1.0f) * 0.5f;
            voice->held_random = patch->lfo_retrigger && patch->lfo_wave == TG_LFO_RANDOM
                ? random_lfo_sample(voice) : noise;
            if (voice->gate && patch->trigger == TG_LFO_TRIG)
                voice->stage = voice->filter_stage = ATTACK;
        }
        /* The VCO and VCF continue behind the closed VCA, as on the
         * instrument. Keep resonance and oscillator phase alive between
         * notes, especially for patches using the filter as their source. */
        if (voice->glide_offset != 0) {
            /* Decay a residual instead of accumulating the absolute note: slow
             * high-note glides must not stall at the float's absolute ULP. */
            voice->glide_offset -= voice->glide_offset * voice->glide;
            if (voice->glide_offset > -0.00004f && voice->glide_offset < 0.00004f)
                snap_pitch(voice);
            else voice->pitch = continuous_pitch(voice->target_note + voice->glide_offset);
        }
        float inc = clamp(voice->pitch * voice->bend_ratio *
                          (1.0f + lfo * (p[TG_VIBRATO] * 0.05f + mod * p[TG_MOD_PITCH] * 0.04f)), voice->pitch > 0 ? 0.000001f : 0.0001f, 0.42f);
        float inverse = 1.0f / inc;
        float leading = edge(voice->phase, inc, inverse);
        /* Reinforce the main pulse's first half-cycle. The physical pulse
         * route is inverted before the mixer; this equivalent phase convention
         * keeps our pulse/PWM edge and divided-sub timing unchanged. */
        float saw = 1.0f - 2.0f * voice->phase + leading;
        float divider = patch->sub_mode ? 0.25f : 0.5f;
        unsigned cycle = voice->sub_cycle & (patch->sub_mode ? 3u : 1u);
        float sub_phase = (voice->phase + (float)cycle) * divider;
        /* The divided square selects the width at the start of each main
         * pulse, so a sub edge cannot split a pulse halfway through. */
        float pwm = 0;
        if (patch->pwm == TG_PWM_LFO) pwm = (triangle + 1.0f) * 0.5f;
        else if (patch->pwm == TG_PWM_ENV) pwm = voice->envelope;
        else if (patch->pwm == TG_PWM_SNH) {
            voice->pwm_slew += (voice->pwm_hold - voice->pwm_slew) * 0.02f;
            pwm = voice->pwm_slew;
        } else if (patch->pwm == TG_PWM_SUB)
            pwm = cycle < (patch->sub_mode == 1 ? 2u : 1u) ? 1.0f : 0.0f;
        float width = clamp(0.5f - p[TG_WIDTH] * 0.48f - pwm * p[TG_PWM] * 0.48f, 0.02f, 0.98f);
        float square = pulse(voice->phase, width, inc, inverse, leading);
        float sub = pulse(sub_phase, patch->sub_mode == 2 ? 0.25f : 0.5f,
                          inc * divider, inverse / divider,
                          edge(sub_phase, inc * divider, inverse / divider));
        voice->phase += inc;
        if (voice->phase >= 1.0f) {
            voice->phase -= 1.0f;
            voice->sub_cycle++;
        }
        /* Linear source mixer, <=0.9625 for bounded component waves. Relative
         * saw/sub gains follow measured open-filter reference balance; the
         * fixed quarter scale reserves input headroom for all four sources. */
        float input = 0.25f * (1.2f * saw * p[TG_SAW] + square * p[TG_PULSE] +
                              0.65f * sub * p[TG_SUB] + noise * p[TG_NOISE]);
        /* Full keyboard tracking adds one cutoff octave per played octave.
         * Keep the last pitch CV through the release stage. */
        float key = ((float)voice->filter_note + 12.0f * voice->controls.patch.key_octave - 48.0f) * (0.09119275f / 12.0f);
        float base_cut = c->patch.filter_low ? p[TG_CUTOFF] * (1.0f + TG_FILTER_LOW_SPAN) - TG_FILTER_LOW_SPAN : p[TG_CUTOFF];
        float cut = base_cut + p[TG_ENV] * filter_envelope * (voice->controls.patch.env_wide ? 1.56f : 0.78f) +
                    p[TG_TRACK] * key + lfo * (p[TG_FILTER_LFO] + mod * p[TG_MOD_FILTER]) * TG_FILTER_MOD_SPAN +
                    c->bend * (0.24f / 512.0f) * p[TG_BEND_FILTER];
        if (accent_tone || accent_level) {
            /* 1.5 ms rise smoothing and 150 ms decay to 1/e. A strike replaces the
             * previous transient without stepping the cutoff or gain. */
            voice->accent += (voice->accent_target - voice->accent) * 0.015f;
            voice->accent_target *= 0.99985f;
            cut += accent_tone * voice->accent;
            amp *= 1.0f + accent_level * voice->accent;
        }
        /* Preserve the balance between driven and self-oscillating patches.
         * Internal excitation remains independent of source mixer levels;
         * the filter runs behind the closed VCA, including between notes. */
        input *= input_gain;
        unsigned advance_filter = 1;
        if (patch->filter_idle_hold) {
            if (voice->gate) {
                voice->filter_started = 1;
                voice->filter_idle_frames = 0;
            } else {
                unsigned limit = voice->filter_started ? TG_RATE * 225u / 1000u : 2048u;
                if (voice->filter_idle_frames < limit)
                    voice->filter_idle_frames++;
                else
                    advance_filter = amp > 0.0001f;
            }
        }
        float filtered = advance_filter ?
            filter_tick(voice->ladder, input + noise * 6.76969972286e-10f,
                        patch->filter_low ? filter_low_coefficient(cut) : filter_coefficient(cut), k) : 0;
        if (!(filtered >= -4.0f && filtered <= 4.0f))
            return -1;
        float signal = filtered * amp;
        if (voice->drive != drive)
            voice->drive += clamp(drive - voice->drive, -1.0f / 384.0f, 1.0f / 384.0f);
        if (voice->drive > 0) {
            float driven = output_limit(signal * 6.0f) * 0.42f;
            signal += (driven - signal) * voice->drive;
        }
        signal *= voice->master;
        voice->output_dc += (signal - voice->output_dc) * 0.00114f;
        float output = output_limit((signal - voice->output_dc) * 2.35f);
        if (!(output >= -1.0f && output <= 1.0f))
            return -1;
        int32_t sample = (int32_t)(output * 8388607.0f);
        stereo[2 * i] = stereo[2 * i + 1] = sample;
    }
    return 0;
}
