/* SPDX-License-Identifier: GPL-3.0-only */
#include "launch.h"
#include <string.h>
#include "../vendor/smk37/fm1_block.h"

#ifdef ELITE_RENDER_PROFILE
#include "fm1_time.h"
#define RENDER_STAMP() fm1_ticks()
#define RENDER_ACCUMULATE(lane, start) (launch->layer_ticks[lane] += fm1_ticks() - (start))
#else
#define RENDER_STAMP() 0u
#define RENDER_ACCUMULATE(lane, start) ((void)(start))
#endif

void conductor_init(conductor_launch *launch)
{
    memset(&launch->active, 0, sizeof launch->active);
    launch->clock = (tg_clock_snapshot){0};
    launch->consumed_publication = 0;
    launch->telegram_bar = launch->rhythm_bar = launch->rhythm_cue_offset = 0;
    launch->rhythm_cues = launch->late_usb_step = 0;
    launch->kit_changes = 0;
    launch->drums_b_update = 0;
    dillo_layer_init(&launch->dillo);
    drum_layer_init(&launch->drums);
    drum_layer_init(&launch->drums_b);
}

#ifdef ELITE_PERCUSSION_727
int conductor_set_percussion_kit(conductor_launch *launch, const percussion_bank *bank, unsigned kit)
{
    _Static_assert((unsigned)P727_TRACKS == (unsigned)D8_NUM, "Percussion reuses B's tracks");
    if (!bank || kit >= P727_KITS) return 0;
    drum_layer *layer = &launch->drums_b;
    percussion_init(&layer->percussion, bank);
    memcpy(layer->percussion_sounds, percussion_factory_kits[kit], D8_NUM);
    layer->repeat_tail = layer->audition_tail = 0;
    return 1;
}

int conductor_set_percussion_sound(conductor_launch *launch, unsigned track, unsigned sound)
{
    drum_layer *layer = &launch->drums_b;
    if (!layer->percussion.bank || track >= D8_NUM || (sound >= P727_SOUNDS && sound != P727_SILENT)) return 0;
    layer->percussion_sounds[track] = (uint8_t)sound;
    layer->percussion.active &= (uint16_t)~(1u << track);
    return 1;
}
#endif

void conductor_prepare_kits(conductor_patterns *next, const conductor_patterns *published,
                            drum_kit_values buffers[2][2])
{
    const drum_kit_values *current = published->kit_values;
    unsigned changed_a = !current || memcmp(&next->kit, &published->kit, sizeof next->kit);
    unsigned changed_b = !current || memcmp(&next->drums_b.kit, &published->drums_b.kit, sizeof next->drums_b.kit);
    if (!changed_a && !changed_b) {
        next->kit_values = current;
        return;
    }
    drum_kit_values *staged = buffers[current == buffers[0]];
    if (current) {
        staged[0] = current[0];
        staged[1] = current[1];
    }
    if (changed_a) drum_kit_resolve(&next->kit, current ? &published->kit : NULL, &staged[0]);
    if (changed_b) drum_kit_resolve(&next->drums_b.kit, current ? &published->drums_b.kit : NULL, &staged[1]);
    next->kit_values = staged;
}

/* USB transport delivers a clock snapshot at the start of an audio block.
 * Advance through coalesced pulses but only sound the latest step. */
typedef struct {
    uint32_t events, pulses;
    unsigned next, last, pulse, detach;
} usb_grid;

static usb_grid usb_grid_read(const tg_clock_snapshot *old, const tg_clock_snapshot *clock)
{
    unsigned reset = old->source != TG_CLOCK_USB || old->epoch != clock->epoch;
    uint32_t events = clock->steps - (reset ? clock->base : old->steps);
    unsigned next = (clock->seek + (clock->steps - clock->base)) % DRUM_STEPS;
    unsigned last = (next + DRUM_STEPS - 1u) % DRUM_STEPS;
    unsigned within_step = (clock->phase + TG_CLOCKS_PER_STEP - 1u) % TG_CLOCKS_PER_STEP;
    unsigned before = !reset && old->phase ? TG_CLOCKS_PER_STEP - old->phase : 0;
    unsigned after = clock->phase ? TG_CLOCKS_PER_STEP - clock->phase : 0;
    return (usb_grid){.events = events, .pulses = events * TG_CLOCKS_PER_STEP + before - after,
        .next = next, .last = last,
        .pulse = last * TG_CLOCKS_PER_STEP + within_step,
        .detach = reset || old->gate_breaks != clock->gate_breaks};
}

static unsigned usb_roll_edge(unsigned pulse, uint32_t pulses, const drum_roll *roll)
{
    if (!pulses) return DRUM_ROLL_NO_EDGE;
    unsigned interval = tg_rate_pulses(roll->rate);
    unsigned odd = drum_roll_odd(interval, roll->swing);
    unsigned elapsed = pulse % (2u * interval);
    if (elapsed >= odd) elapsed -= odd;
    /* Keep the newest roll's order relative to the last sixteenth. */
    if (pulses <= elapsed) return DRUM_ROLL_NO_EDGE;
    unsigned step_age = pulse % TG_CLOCKS_PER_STEP;
    if (elapsed > step_age) return DRUM_ROLL_BEFORE_STEP;
    return elapsed < step_age ? DRUM_ROLL_AFTER_STEP : DRUM_ROLL_ON_STEP;
}

static unsigned usb_launch_now(const tg_clock_snapshot *clock, const usb_grid *grid)
{
    return clock->state == TG_CLOCK_STOPPED ||
           (clock->state == TG_CLOCK_RUNNING && grid->events > grid->last);
}

/* Called after crossing a downbeat. Count from the first crossed bar
 * so a short phrase can keep cycling even if multiple bars elapsed. */
static uint32_t usb_launch_age(const usb_grid *grid)
{
    uint32_t prior_bars = (grid->events - 1u - grid->last) / DRUM_STEPS;
    return prior_bars * DRUM_STEPS + grid->last;
}

/* Both render paths use the same transport phase before the 808 advances. */
static unsigned bar_offset(const conductor_launch *launch, const tg_controls *controls, unsigned edge)
{
    if (!controls->running || controls->clock.source != TG_CLOCK_INTERNAL) return 0;
    unsigned tempo = controls->tempo < 30 ? 30 : controls->tempo;
    unsigned period = (TG_RATE * 15u + tempo / 2u) / tempo;
    unsigned next = launch->clock.source == TG_CLOCK_USB ? 0 : launch->drums.step;
    unsigned delay = tg_step_delay(period, controls->step_swing);
    return next ? edge + DRUM_STEPS * period - tg_step_position(period, delay, next) : edge;
}

static void apply_controls(tg_voice *voice, const tg_controls *controls)
{
    if (!voice->phrase_with_arp || memcmp(&voice->controls, controls, sizeof *controls)) {
        voice->phrase_with_arp = 1;
        tg_apply(voice, controls);
    }
}

static int render_telegram(tg_voice *voice, int32_t *pcm, unsigned frames)
{
    while (frames) {
        unsigned count = frames < FM1_BLOCK_FRAMES ? frames : FM1_BLOCK_FRAMES;
        if (tg_render(voice, pcm, count)) return -1;
        frames -= count;
        pcm += 2u * count;
    }
    return 0;
}

static void acknowledge_telegram(conductor_patterns *active, const conductor_patterns *requested)
{
    active->telegram_cue = requested->telegram_cue;
    active->telegram_slot = requested->telegram_slot;
    active->telegram_library = requested->telegram_library;
}

int conductor_render_telegram(conductor_launch *launch, tg_voice *voice, int32_t *pcm, unsigned frames,
                              const tg_controls *requested, const conductor_patterns *patterns, unsigned *edge)
{
    launch->telegram_bar = 0;
    unsigned pending = launch->active.telegram_cue != patterns->telegram_cue;
    unsigned stable = voice->phrase_with_arp && patterns->publication &&
                      launch->consumed_publication == patterns->publication;
    if (!pending && stable) {
        *edge = voice->remaining;
        return render_telegram(voice, pcm, frames);
    }
    unsigned usb = requested->clock.source == TG_CLOCK_USB;
    usb_grid grid = {.events = 0};
    if (usb) grid = usb_grid_read(&launch->clock, &requested->clock);
    if (pending && (usb ? !usb_launch_now(&requested->clock, &grid) : requested->running)) {
        /* Sound, keys and tempo remain live while only the phrase is queued. */
        if (!stable) {
            tg_controls controls = *requested;
            memcpy(controls.steps, voice->controls.steps, sizeof controls.steps);
            controls.length = voice->controls.length;
            controls.phrase_gate = voice->controls.phrase_gate;
            controls.phrase_epoch = voice->controls.phrase_epoch;
            controls.transpose = voice->controls.transpose;
#ifdef ELITE_BAND_PRESETS
            if (patterns->band_outgoing_tempo) {
                controls.tempo = patterns->band_outgoing_tempo;
                controls.step_swing = patterns->band_outgoing_swing;
                controls.phrase_muted = voice->controls.phrase_muted;
            }
            if (patterns->band_outgoing_tempo || (patterns->studio_cues & CUE_TELEGRAM)) {
                controls.patch = voice->controls.patch;
                controls.sound_epoch = voice->controls.sound_epoch;
            }
#endif
            apply_controls(voice, &controls);
        }
    } else if (usb && pending) {
        if (requested->clock.state == TG_CLOCK_RUNNING) {
            voice->phrase_with_arp = 1;
            tg_apply_usb_launch(voice, requested, usb_launch_age(&grid));
            launch->telegram_bar = 1;
        } else apply_controls(voice, requested);
        acknowledge_telegram(&launch->active, patterns);
    } else {
        apply_controls(voice, requested);
        if (!pending) acknowledge_telegram(&launch->active, patterns);
    }
    *edge = voice->remaining;
    if (usb) return render_telegram(voice, pcm, frames);
    if (pending) {
        const tg_controls *timing = requested;
#ifdef ELITE_BAND_PRESETS
        if (patterns->band_outgoing_tempo) timing = &voice->controls;
#endif
        unsigned before_bar = bar_offset(launch, timing, *edge);
        if (before_bar < frames) {
            if (render_telegram(voice, pcm, before_bar)) return -1;
            apply_controls(voice, requested);
            if (requested->clock.source == TG_CLOCK_INTERNAL) {
                voice->step = 0;
                voice->step_odd = 0;
                voice->play_step = TG_STEPS;
                voice->phrase_launch = 1;
                if (requested->running) launch->telegram_bar = (uint16_t)(before_bar + 1u);
            }
            acknowledge_telegram(&launch->active, patterns);
            pcm += 2u * before_bar;
            frames -= before_bar;
        }
    }
    return render_telegram(voice, pcm, frames);
}

int conductor_drums_b_active(const conductor_launch *launch, const conductor_patterns *requested)
{
    return (!requested->drums_b.muted && (requested->drums_b.enabled || launch->active.drums_b.enabled)) ||
           requested->drums_b.repeat ||
           requested->drum_auditions[1].generation != launch->drums_b.audition_seen ||
           drum_layer_active(&launch->drums_b);
}

static int mix_layers(conductor_launch *launch, int32_t *bass, int32_t *drums, int32_t *drums_b, unsigned frames,
                      const tg_controls *controls, unsigned edge)
{
    const conductor_patterns *active = &launch->active;
    /* The drum transport advances even when its part is muted. */
    if (!launch->dillo.running && active->dillo_on)
        launch->dillo.step = active->dillo.repeat_short ?
                             launch->drums.step % dillo_phrase_grid(&active->dillo) : launch->drums.step;
    uint32_t stamp = RENDER_STAMP();
    if (dillo_layer_mix_grid(&launch->dillo, bass, frames, controls, edge, launch->drums.step,
                            active->dillo_on ? &active->dillo : NULL)) return -1;
    RENDER_ACCUMULATE(MIX_DILLO, stamp);
    if (drums_b) {
        launch->drums_b.step = launch->drums.step;
        stamp = RENDER_STAMP();
        if (drum_layer_mix(&launch->drums_b, drums_b, frames, controls, edge,
                           active->drums_b.enabled && !active->drums_b.muted ? &active->drums_b.pattern : NULL)) return -1;
        RENDER_ACCUMULATE(MIX_808_B, stamp);
    }
    stamp = RENDER_STAMP();
    int result = drum_layer_mix(&launch->drums, drums, frames, controls, edge,
                                active->drums_on ? &active->drums : NULL);
    RENDER_ACCUMULATE(MIX_808, stamp);
    return result;
}

static void acknowledge_drums_a(conductor_patterns *active, const conductor_patterns *requested)
{
    active->drums = requested->drums;
    active->drums_on = requested->drums_on;
    active->drums_slot = requested->drums_slot;
    active->drums_cue = requested->drums_cue;
    active->drum_init = (active->drum_init & ~1u) | ((requested->drum_init & 1u) || requested->drums_on);
}

static void acknowledge_drums_b(conductor_launch *launch, const conductor_patterns *requested)
{
    conductor_patterns *active = &launch->active;
#ifdef ELITE_PERCUSSION_727
    unsigned kit = requested->drums_b.percussion_kit;
    drum_layer *layer = &launch->drums_b;
    if (kit && kit <= P727_KITS && layer->percussion.bank &&
        (active->drums_b.cue != requested->drums_b.cue || active->drums_b.percussion_kit != kit) &&
        memcmp(layer->percussion_sounds, percussion_factory_kits[kit - 1u], D8_NUM))
        conductor_set_percussion_kit(launch, layer->percussion.bank, kit - 1u);
#endif
    active->drums_b = requested->drums_b;
    active->drum_init = (active->drum_init & ~2u) |
        (((requested->drum_init & 2u) || requested->drums_b.enabled) << 1);
}

static inline int apply_rhythm_sounds(conductor_launch *launch, const conductor_patterns *requested,
                                      unsigned lanes)
{
    if ((lanes & CUE_DILLO) && !dillo_layer_sound(&launch->dillo, &requested->dillo_sound)) return -1;
    if ((lanes & CUE_DRUM_A) && requested->has_kit)
        launch->kit_changes |= drum_layer_apply_kit(&launch->drums, &requested->kit, requested->kit_values);
    return 0;
}

static int mix_usb(conductor_launch *launch, int32_t *bass, int32_t *drums, int32_t *drums_b, unsigned frames,
                   const tg_controls *controls, const conductor_patterns *requested,
                   unsigned dillo_pending, unsigned drums_pending, unsigned b_pending, unsigned deferred_sounds)
{
    conductor_patterns *active = &launch->active;
    const tg_clock_snapshot *clock = &controls->clock;
    usb_grid grid = usb_grid_read(&launch->clock, clock);
    unsigned commit = usb_launch_now(clock, &grid);
    if (commit && deferred_sounds && apply_rhythm_sounds(launch, requested, deferred_sounds)) return -1;
    if (commit && dillo_pending) {
        active->dillo = requested->dillo;
        active->dillo_on = requested->dillo_on;
        active->dillo_slot = requested->dillo_slot;
        active->dillo_cue = requested->dillo_cue;
    }
    if (commit && drums_pending) acknowledge_drums_a(active, requested);
    if (commit && b_pending) acknowledge_drums_b(launch, requested);
    if (commit && clock->state == TG_CLOCK_RUNNING &&
        (dillo_pending || drums_pending || b_pending || launch->telegram_bar)) {
        launch->rhythm_cues = (dillo_pending ? CUE_DILLO : 0) | (drums_pending ? CUE_DRUM_A : 0) |
                              (b_pending ? CUE_DRUM_B : 0);
        launch->rhythm_cue_offset = launch->rhythm_cues != 0;
        launch->rhythm_bar = launch->rhythm_cues == CUE_RHYTHM;
        uint32_t age = usb_launch_age(&grid);
        launch->late_usb_step = (uint8_t)(age > UINT8_MAX ? UINT8_MAX : age);
    }
    uint32_t roll_pulses = grid.pulses;
    unsigned roll_pulse = grid.pulse;
    /* Stop breaks synth gates. Drum tails survive Stop and Continue,
     * including when the audio owner sees only the combined snapshot. */
    unsigned drums_detach = launch->clock.source != TG_CLOCK_USB ||
                            launch->clock.epoch != clock->epoch ||
                            launch->clock.losses != clock->losses;
    if (clock->state == TG_CLOCK_STOPPED) {
        unsigned before = grid.detach || launch->clock.state != TG_CLOCK_STOPPED ?
                          0 : launch->clock.stopped_pulses;
        unsigned after = clock->stopped_pulses;
        roll_pulses = after + (after < before ? TG_STOPPED_PULSE_WRAP : 0u) - before;
        roll_pulse = (after ? after : TG_STOPPED_PULSE_WRAP) - 1u;
    }
    unsigned dillo_next = grid.next;
    if (active->dillo.repeat_short) {
        unsigned length = dillo_phrase_grid(&active->dillo);
        if (commit && dillo_pending)
            dillo_next = clock->state == TG_CLOCK_RUNNING ? (usb_launch_age(&grid) + 1u) % length : 0;
        else if (launch->clock.source != TG_CLOCK_USB || launch->clock.epoch != clock->epoch)
            dillo_next = (clock->seek + clock->steps - clock->base) % length;
        else if (grid.events)
            dillo_next = (launch->dillo.step + grid.events) % length;
        else
            dillo_next = launch->dillo.step;
    }
    launch->clock = *clock;
    unsigned panic = launch->dillo.silence != controls->silence;
    unsigned tick = grid.events && (!panic || clock->steps != controls->silence_clock_steps);
    unsigned roll = usb_roll_edge(roll_pulse, roll_pulses, &requested->drum_keys);
    unsigned tempo = controls->tempo < 30 ? 30 : controls->tempo;
    unsigned period = clock->period_us ? (clock->period_us * 2646u + 5000u) / 10000u :
                                         (TG_RATE * 15u + tempo / 2u) / tempo;
    unsigned age = controls->clock_age_us > TG_CLOCK_TIMEOUT_US ? TG_CLOCK_TIMEOUT_US : controls->clock_age_us;
    unsigned elapsed = (grid.pulse % TG_CLOCKS_PER_STEP) * period / TG_CLOCKS_PER_STEP +
                       age * 441u / 10000u;
    unsigned dillo_detach = grid.detach || (commit && dillo_pending);
    uint32_t stamp = RENDER_STAMP();
    if (dillo_layer_mix_usb(&launch->dillo, bass, frames, controls,
                            active->dillo_on ? &active->dillo : NULL, dillo_next, tick ? grid.events : 0,
                            dillo_detach, period, elapsed)) return -1;
    RENDER_ACCUMULATE(MIX_DILLO, stamp);
    if (drums_b) {
        unsigned b_roll = usb_roll_edge(roll_pulse, roll_pulses, &requested->drums_b.roll);
        stamp = RENDER_STAMP();
        if (drum_layer_mix_usb(&launch->drums_b, drums_b, frames, controls,
                              active->drums_b.enabled && !active->drums_b.muted ? &active->drums_b.pattern : NULL,
                              grid.next, tick, b_roll, drums_detach)) return -1;
        RENDER_ACCUMULATE(MIX_808_B, stamp);
    }
    stamp = RENDER_STAMP();
    int result = drum_layer_mix_usb(&launch->drums, drums, frames, controls,
                                    active->drums_on ? &active->drums : NULL, grid.next, tick,
                                    roll, drums_detach);
    RENDER_ACCUMULATE(MIX_808, stamp);
    return result;
}

int conductor_mix(conductor_launch *launch, int32_t *bass, int32_t *drums, int32_t *drums_b, unsigned frames,
                  const tg_controls *controls, unsigned edge, const conductor_patterns *requested)
{
    conductor_patterns *active = &launch->active;
    launch->rhythm_bar = launch->rhythm_cue_offset = 0;
    launch->rhythm_cues = launch->late_usb_step = 0;
    launch->kit_changes = 0;
    unsigned b_pending = active->drums_b.cue != requested->drums_b.cue;
    unsigned dillo_pending = active->dillo_cue != requested->dillo_cue;
    unsigned drums_pending = active->drums_cue != requested->drums_cue;
    const unsigned rhythm_sounds = CUE_DILLO | CUE_DRUM_A;
    unsigned band_pending = 0, deferred_sounds = 0;
#ifdef ELITE_BAND_PRESETS
    band_pending = requested->band_outgoing_tempo && (dillo_pending || drums_pending);
    deferred_sounds = band_pending ? rhythm_sounds : dillo_pending ? requested->studio_cues & CUE_DILLO : 0;
#endif
    if (!requested->publication || launch->consumed_publication != requested->publication) {
        launch->dillo.requested_keys = requested->dillo_live;
        launch->dillo.articulation = requested->dillo_articulation;
        if (apply_rhythm_sounds(launch, requested, rhythm_sounds & ~deferred_sounds)) return -1;
        launch->drums.roll = requested->drum_keys;
        launch->drums.audition = requested->drum_auditions[0];
        launch->drums_b_update = 1;
        /* Apply the live enables and mutes to the outgoing patterns while
         * replacements wait. Pattern edits can keep their cue generation. */
        if (!b_pending) acknowledge_drums_b(launch, requested);
        else {
            active->drums_b.enabled = (active->drum_init & 2u) && requested->drums_b.enabled;
            active->drums_b.muted = requested->drums_b.muted;
        }
        if (!dillo_pending) {
            active->dillo_on = requested->dillo_on;
            active->dillo = requested->dillo;
            active->dillo_slot = requested->dillo_slot;
        } else if (!band_pending) active->dillo_on = active->dillo.length && requested->dillo_on;
        if (!drums_pending) acknowledge_drums_a(active, requested);
        else if (!band_pending) active->drums_on = (active->drum_init & 1u) && requested->drums_on;
        launch->consumed_publication = requested->publication;
    }
    if (!drums_b) {
        launch->drums_b.repeat_phase = 0;
        launch->drums_b.repeat_blocked = 0;
    } else if (launch->drums_b_update) {
        launch->drums_b.roll = requested->drums_b.roll;
        launch->drums_b.audition = requested->drum_auditions[1];
        launch->kit_changes |= drum_layer_apply_kit(&launch->drums_b, &requested->drums_b.kit,
            requested->kit_values ? requested->kit_values + 1 : NULL) << 1;
        launch->drums_b_update = 0;
    }
    if (controls->clock.source == TG_CLOCK_USB)
        return mix_usb(launch, bass, drums, drums_b, frames, controls, requested,
                       dillo_pending, drums_pending, b_pending, deferred_sounds);
    if (launch->clock.source == TG_CLOCK_USB) {
        launch->clock = controls->clock;
        launch->dillo.step = launch->drums.step = 0;
        launch->dillo.running = 0;
        if (!launch->dillo.live_tail) dillo_phrase_release(&launch->dillo.phrase, &launch->dillo.voice);
        drum_layer_clear(&launch->drums);
        launch->drums.repeat_tail = launch->drums.audition_tail = 0;
        drum_layer_clear(&launch->drums_b);
        launch->drums_b.repeat_tail = launch->drums_b.audition_tail = 0;
    }
    if (!(dillo_pending || drums_pending || b_pending))
        return mix_layers(launch, bass, drums, drums_b, frames, controls, edge);

    unsigned before_bar = bar_offset(launch, controls, edge);
#ifdef ELITE_BAND_PRESETS
    tg_controls outgoing;
    const tg_controls *before = controls;
    if (band_pending) {
        outgoing = *controls;
        outgoing.tempo = requested->band_outgoing_tempo;
        outgoing.step_swing = requested->band_outgoing_swing;
        before = &outgoing;
        before_bar = controls->running ? (launch->telegram_bar ? launch->telegram_bar - 1u : frames) : 0;
    }
#else
    const tg_controls *before = controls;
#endif
    if (before_bar >= frames) return mix_layers(launch, bass, drums, drums_b, frames, before, edge);
    if (before_bar && mix_layers(launch, bass, drums, drums_b, before_bar, before, edge)) return -1;
    if (deferred_sounds && apply_rhythm_sounds(launch, requested, deferred_sounds)) return -1;

    if (dillo_pending) {
        if (!launch->dillo.requested_keys.note)
            dillo_phrase_release(&launch->dillo.phrase, &launch->dillo.voice);
        launch->dillo.step = 0;
    }
    active->dillo = requested->dillo;
    active->dillo_cue = requested->dillo_cue;
    active->dillo_slot = requested->dillo_slot;
    active->dillo_on = requested->dillo_on;
    acknowledge_drums_a(active, requested);
    acknowledge_drums_b(launch, requested);
    if (controls->running) {
        launch->rhythm_cues = (dillo_pending ? CUE_DILLO : 0) | (drums_pending ? CUE_DRUM_A : 0) |
                              (b_pending ? CUE_DRUM_B : 0);
        launch->rhythm_cue_offset = (uint16_t)(before_bar + 1u);
        if (launch->rhythm_cues == CUE_RHYTHM) launch->rhythm_bar = launch->rhythm_cue_offset;
    }
    unsigned dry_offset = CONDUCTOR_DRY_STRIDE * before_bar;
    return mix_layers(launch, bass + dry_offset, drums + dry_offset,
                      drums_b ? drums_b + dry_offset : NULL, frames - before_bar, controls, 0);
}
