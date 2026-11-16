#ifndef BB_CORE_H
#define BB_CORE_H
#include <stdint.h>
#include <stddef.h>
#include "bb_drum_sound.h"
#include "bb_fx_settings.h"

/* Portable musical state: no OS, graphics, allocation or audio-driver dependency. */
#define BB_MACHINES 4
#define BB_VOICES 16
#define BB_STEPS 64
#define BB_SYNTHS 2
#define BB_DILLO_PATCHES 8
#define BB_TELEGRAM_PATCHES 8
typedef enum { BB_DRUMS, BB_DILLO, BB_TELEGRAM } bb_page;
/* Mixer ownership is independent of sound recipes and note patterns. */
typedef enum { BB_MIX_DRUMS, BB_MIX_DILLO, BB_MIX_TELEGRAM, BB_MIX_CHANNELS } bb_mix_channel;
typedef enum { BB_GATE, BB_ACCENT, BB_SLIDE } bb_edit;
/* Portable parameter IDs. Commands carry explicit synth and parameter targets;
 * page selection never redirects an already queued control. */
typedef enum {
    BB_PARAM_CUTOFF, BB_PARAM_RESONANCE, BB_PARAM_ENVELOPE, BB_PARAM_DECAY,
    BB_PARAM_ACCENT, BB_PARAM_DRIVE, BB_PARAM_WAVE, BB_PARAM_SLIDE_MS,
    BB_PARAM_SQUARE, BB_PARAM_SAW, BB_PARAM_SUB, BB_PARAM_MODULATION,
    BB_PARAM_GLIDE, BB_PARAM_LFO_RATE
} bb_synth_parameter;
typedef struct { uint16_t hits, accents; } bb_drum_step;
typedef struct { uint8_t note, gate, accent, slide; } bb_note;
typedef struct {
    bb_drum_step steps[BB_STEPS];
    uint8_t length, next, kit, enabled, muted, level;
    bb_drum_sound sound;
    uint32_t sound_revision;
} bb_machine;
typedef struct {
    uint16_t cutoff_hz, resonance_x100;
    uint8_t wave, envelope, decay, accent, drive, slide_ms, attack_ms, tracking;
} bb_dillo_patch;
typedef struct {
    uint16_t cutoff_hz, resonance_x100;
    uint8_t square, saw, sub, envelope, modulation, glide;
    uint16_t lfo_rate_x100, attack_ms, release_ms, glide_ms;
} bb_telegram_patch;
typedef struct {
    bb_note notes[BB_STEPS];
    uint8_t length, next, muted;
} bb_sequence;
typedef struct {
    uint64_t frame;
    uint8_t machine, kit, voice, step, velocity;
} bb_hit;
typedef void (*bb_hit_fn)(void *, const bb_hit *);
typedef struct {
    uint64_t frame;
    uint8_t synth, step;
    bb_note note;
} bb_note_event;
typedef void (*bb_note_fn)(void *, const bb_note_event *);
/* Render exactly frames samples after all events at this span's first sample.
   Callbacks must not mutate/re-enter the core. Spans never cross a clock tick.
   Stopped transport still renders tails, but does not advance sequence time. */
typedef void (*bb_render_fn)(void *, uint32_t frames);
typedef struct bb_core {
    bb_machine drums[BB_MACHINES];
    bb_sequence synths[BB_SYNTHS];
    bb_dillo_patch dillo;
    bb_telegram_patch telegram;
    bb_fx_settings effects;
    uint8_t band_level[BB_MIX_CHANNELS]; /* 0..127, post-voice/send input */
    uint64_t frame;
    uint32_t sample_rate, phase, ticks; /* global emitted sixteenths; first tick = 1 */
    uint16_t bpm;
    uint8_t running, pending_tick, selected_machine, selected_voice;
    uint8_t swing, active_swing, swing_odd; /* 50=straight, 75=3:1; latch each pair */
    bb_page page;
    bb_edit edit;
} bb_core;
/* Optional audio-owner hook immediately before a sixteenth's events. May
 * publish validated musical state, including tempo and sequence lengths.
 * Never alter frame/phase/ticks/pending_tick/swing_odd/running/sample_rate,
 * re-enter the core, or perform storage work. A phrase launch may reset the
 * musical sequence cursors and update active_swing at an even tick.
 * Existing callers retain the no-hook path. */
typedef void (*bb_before_tick_fn)(void *,bb_core *);
typedef struct { const char *name; bb_dillo_patch sound; } bb_named_dillo;
extern const bb_named_dillo bb_dillo_bank[BB_DILLO_PATCHES];
typedef struct { const char *name; bb_telegram_patch sound; } bb_named_telegram;
extern const bb_named_telegram bb_telegram_bank[BB_TELEGRAM_PATCHES];
int bb_telegram_patch_valid(const bb_telegram_patch *);
int bb_init(bb_core *, uint32_t sample_rate);
int bb_set_tempo(bb_core *, unsigned bpm);
int bb_set_swing(bb_core *, unsigned percent);
int bb_set_length(bb_core *, unsigned machine, unsigned length);
int bb_set_synth_length(bb_core *, unsigned synth, unsigned length);
int bb_synth_parameter_limits(unsigned synth,unsigned parameter,unsigned *minimum,unsigned *maximum);
unsigned bb_synth_parameter_get(const bb_core *,unsigned synth,unsigned parameter);
int bb_synth_parameter_set(bb_core *,unsigned synth,unsigned parameter,unsigned value);
int bb_fx_parameter_limits(unsigned parameter,unsigned *minimum,unsigned *maximum);
unsigned bb_fx_parameter_get(const bb_core *,unsigned parameter);
int bb_fx_parameter_set(bb_core *,unsigned parameter,unsigned value);
int bb_set_enabled(bb_core *, unsigned machine, int enabled);
int bb_select_drum(bb_core *, unsigned machine, unsigned voice);
int bb_set_page(bb_core *, bb_page);
int bb_set_edit(bb_core *, bb_edit);
int bb_press_step(bb_core *, unsigned step, unsigned note);
int bb_recall_dillo(bb_core *, unsigned patch);
int bb_recall_drum(bb_core *,unsigned machine,unsigned kit);
int bb_set_drum_parameter(bb_core *,unsigned machine,unsigned track,unsigned parameter,unsigned value);
int bb_recall_telegram(bb_core *, const bb_telegram_patch *);
void bb_start(bb_core *);
void bb_stop(bb_core *);
void bb_process(bb_core *, uint32_t frames, bb_hit_fn, bb_note_fn, void *context);
void bb_process_render(bb_core *, uint32_t frames, bb_hit_fn, bb_note_fn,
                       bb_render_fn, void *context);
void bb_process_render_hook(bb_core *,uint32_t frames,bb_hit_fn,bb_note_fn,
                            bb_render_fn,bb_before_tick_fn,void *context);
#endif
