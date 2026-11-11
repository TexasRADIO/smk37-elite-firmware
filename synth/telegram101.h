/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TELEGRAM101_H
#define TELEGRAM101_H

#include <stdint.h>
#include "clock101.h"

enum {
    TG_RATE = 44100, TG_PADS = 16, TG_STEPS = 100, TG_KEYS = 37, TG_ARP_NOTES = 128,
    TG_PAGES = (TG_STEPS + TG_PADS - 1) / TG_PADS,
    TG_PRESET_BANKS = 7, TG_PRESETS = TG_PRESET_BANKS * TG_PADS
};
enum tg_parameter {
    TG_SAW, TG_PULSE, TG_SUB, TG_NOISE,
    TG_CUTOFF, TG_RESONANCE, TG_ENV, TG_TRACK,
    TG_ATTACK, TG_DECAY, TG_SUSTAIN, TG_RELEASE,
    TG_WIDTH, TG_PWM, TG_LFO_RATE, TG_FILTER_LFO, TG_GLIDE, TG_VIBRATO,
    TG_DELAY, TG_REVERB, TG_BEND_PITCH, TG_BEND_FILTER, TG_MOD_PITCH, TG_MOD_FILTER, TG_PARAMETERS
};
/* Normalized VCF position covers log2(2000) octaves. Direct and wheel
 * modulation can each move +/- 0.6 positions (about 6.58 octaves). */
#define TG_FILTER_MOD_SPAN 0.6f
/* Preserve the depth of version-1 patches, whose span was 0.28. */
#define TG_FILTER_MOD_FROM_V1(value) (((unsigned)(value) * 7u + 7u) / 15u)

enum { TG_GATE_TRIG, TG_GATE, TG_LFO_TRIG };
enum { TG_FILTER_ENV_SHARED, TG_FILTER_ENV_OPEN, TG_FILTER_ENV_CLOSE };
enum { TG_ADSR_STAGES = 4 };
enum { TG_PWM_MANUAL, TG_PWM_LFO, TG_PWM_ENV, TG_PWM_SNH, TG_PWM_SUB, TG_PWM_SOURCE_COUNT };
enum { TG_LFO_TRI, TG_LFO_SQUARE, TG_LFO_RANDOM, TG_LFO_NOISE };
enum { TG_RANGE_16, TG_RANGE_8, TG_RANGE_4, TG_RANGE_2 };
enum { TG_TUNE_LIMIT = 50 };
enum { TG_GLIDE_OFF, TG_GLIDE_AUTO, TG_GLIDE_ON };
enum {
    TG_ARP_OFF, TG_ARP_UP, TG_ARP_DOWN, TG_ARP_BOUNCE,
    TG_ARP_INCLUSIVE, TG_ARP_RANDOM, TG_ARP_ORDER, TG_ARP_REPEAT,
};
enum { TG_ARP_RATE_COUNT = 8 };
enum { TG_REST = 0, TG_TIE = 1u << 7 };
/* Live/arp-note token only: keep 0 for gate-off while representing MIDI note 0. */
enum { TG_NOTE_ZERO = 128 };

typedef struct {
    uint8_t value[TG_PARAMETERS];
    uint8_t trigger, pwm, sub_mode, amp_gate, glide_mode, range, lfo_wave;
    uint8_t env_wide; /* 0: original depth; 1: twice the filter ENV span. */
    int8_t osc_octave, key_octave; /* -2..+2; only key octave enters VCF tracking. */
    uint8_t lfo_retrigger; /* Restart modulation on a new note. */
    uint8_t filter_idle_hold; /* Retain the VCF state after a quiet idle interval. */
    uint8_t filter_low; /* Optional 3.6 Hz base range; zero preserves 10 Hz range. */
    uint8_t drive; /* Optional post-VCA overdrive before master and sends. */
    uint8_t filter_env_mode;
    uint8_t filter_adsr[TG_ADSR_STAGES]; /* 0 follows the amp setting, 1..128 store an independent value. */
    uint8_t accent_tone, accent_level; /* Independent velocity-transient amounts, 0..127. */
} tg_patch;

static inline unsigned tg_filter_adsr_value(const tg_patch *patch, unsigned stage)
{
    return patch->filter_adsr[stage] ? patch->filter_adsr[stage] - 1u :
                                      patch->value[TG_ATTACK + stage];
}

typedef struct {
    const char *name;
    tg_patch patch;
} tg_preset;
extern const tg_preset tg_presets[TG_PRESETS];

typedef struct {
    uint64_t down;
    uint8_t selected;
} tg_keyboard;
/* Hold accumulates a chord during one physical gesture. Once every key is
 * released, the next gesture replaces it. Disabling Hold returns raw keys. */
typedef struct {
    uint64_t down, held;
    uint8_t enabled;
} tg_latch;
uint64_t tg_latch_scan(tg_latch *, uint64_t down, unsigned enabled);

/* Returns the lowest new press from this matrix scan, or -1. */
int tg_keyboard_scan(tg_keyboard *, uint64_t down);
int tg_keyboard_note(const tg_keyboard *, unsigned trigger);

typedef struct {
    tg_patch patch;
    tg_clock_snapshot clock;
    uint32_t silence_clock_steps, clock_age_us;
    uint8_t steps[TG_STEPS], arp_notes[TG_ARP_NOTES], arp_count, length;
    uint8_t live_note, live_trigger, running, arp, master;
    int16_t bend;
    int8_t transpose; /* Sequence-only semitones; applied at the next step. */
    int8_t tune_cents; /* Session-wide oscillator trim. */
    uint8_t mod;
    uint16_t tempo, arp_tempo; /* 0 follows the phrase tempo. */
    uint32_t silence; /* One-shot All Sound Off generation, not patch data. */
    uint8_t sound_epoch; /* Changes on idle-hold sound recall. */
    uint8_t phrase_epoch; /* Changes on saved phrase recall. */
    uint8_t phrase_gate; /* Session-wide percentage, zero uses 75%. */
    uint8_t arp_rate; /* 0 uses legacy 1/16, 1..8 follow the printed RATE row. */
    uint8_t arp_swing; /* Percentage points above an even 50:50 pair. */
    uint8_t arp_octaves, arp_gate; /* Added octaves and gate percent; 0 gate uses 75%. */
    uint8_t silence_clock_phase;
    uint8_t silence_keep_live; /* A fresh note followed the command in this batch. */
    uint8_t repeat_pad, repeat_rate, repeat_gate, repeat_swing;
    uint8_t live_velocity, arp_velocity[TG_ARP_NOTES];
    uint8_t live_break; /* Generation of a MIDI all-up edge before the current live note. */
    uint8_t phrase_muted; /* The phrase advances without sounding. */
    uint8_t step_swing; /* Internal shared grid, 0..60 (straight through 80:20). */
} tg_controls;

static inline unsigned tg_rate_pulses(unsigned rate)
{
    static const uint8_t pulses[] = {6, 24, 16, 12, 8, 6, 4, 3, 2};
    return pulses[rate <= TG_ARP_RATE_COUNT ? rate : 0];
}

static inline unsigned tg_phrase_gate(const tg_controls *c)
{
    return c->phrase_gate ? c->phrase_gate : 75u;
}

static inline unsigned tg_step_delay(unsigned period, unsigned swing)
{
    return swing ? (period * swing + 50u) / 100u : 0;
}

static inline unsigned tg_step_interval(unsigned period, unsigned delay, unsigned step)
{
    return step & 1u ? period - delay : period + delay;
}

static inline unsigned tg_step_position(unsigned period, unsigned delay, unsigned step)
{
    return step * period + (step & 1u ? delay : 0);
}

static inline unsigned tg_arp_tempo(const tg_controls *c)
{
    return c->arp_tempo ? c->arp_tempo : c->tempo;
}

static inline unsigned tg_arp_active(const tg_controls *c)
{
    return c->arp && c->arp_count && (c->arp_count > 1 || c->arp_octaves || c->arp == TG_ARP_REPEAT);
}

typedef struct {
    tg_controls controls;
    float phase, lfo_phase, pitch, target_pitch;
    float target_note, glide_offset; /* Semitone target and decaying residual, never accumulated absolute pitch. */
    float envelope, gate_level, ladder[4], smooth[TG_PARAMETERS], master;
    float accent, accent_target;
    float held_random, lfo_noise_lp, pwm_hold, pwm_slew, output_dc, drive;
    float filter_envelope, filter_attack, filter_decay, filter_sustain, filter_release;
    uint16_t filter_idle_frames;
    uint8_t filter_note, blocked_live, clock_tied, filter_started;
    float attack, decay, release, glide, lfo_rate, bend_ratio;
    uint32_t rng, lfo_rng, remaining, gate_remaining, arp_step;
    uint8_t gate, stage, sub_cycle, sounding, step, play_step, clocking, control_phase;
    uint32_t repeat_remaining, repeat_gate_remaining, arp_remaining;
    uint8_t phrase_launch, arp_odd, filter_stage, repeat_odd, phrase_gate, restart_pitch;
    uint8_t step_odd;
    uint8_t phrase_with_arp, internal_live_arp; /* Conductor keeps the phrase clock running under the arp. */
} tg_voice;

unsigned tg_pattern_length(const tg_controls *);

void tg_init(tg_voice *, const tg_controls *);
void tg_apply(tg_voice *, const tg_controls *);
/* Conductor has already located the latest USB step within the cued phrase. */
void tg_apply_usb_launch(tg_voice *, const tg_controls *, unsigned offset);
int tg_render(tg_voice *, int32_t *stereo, unsigned frames);

#endif
