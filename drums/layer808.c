/* SPDX-License-Identifier: GPL-3.0-only */
#include "layer808.h"
#include <string.h>
#include "../conductor/dry_bus.h"

#ifdef ELITE_808_RAM
#include "fm1_cc.h"
__attribute__((noinline))
int cr_v_bb_drum808_render_q23(drum808_t *voice, int32_t *pcm, int count, float gain)
{
    return FM1_FAR(bb_drum808_render_q23)(voice, pcm, count, gain);
}
#define drum_render_q23 cr_v_bb_drum808_render_q23
#else
#define drum_render_q23 bb_drum808_render_q23
#endif

enum { DRUM_RENDER_SPAN = 128 };

const drum_pattern drum_demo = {
    .hits = {[D8_BD] = 0x4901, [D8_SD] = 0x1010, [D8_CH] = 0x5555, [D8_OH] = 0x0880},
    .accents = {[D8_BD] = 0x0001, [D8_SD] = 0x1010, [D8_CH] = 0x1111}
};

const drum_pattern drum_demo_b = {
    .hits = {[D8_RS] = 0x0202, [D8_CB] = 0x0400, [D8_LT] = 0x8000},
    .accents = {[D8_RS] = 0x0200, [D8_LT] = 0x8000}
};

/* Parameter indices in the pinned 808 engine, in panel knob order. */
static const uint8_t kit_param[D8_NUM][DRUM_KIT_PARAMS] = {
    [D8_BD] = {3, 2, 0}, [D8_SD] = {3, 4, 0},
    [D8_LT] = {1, 2, 0}, [D8_MT] = {1, 2, 0}, [D8_HT] = {1, 2, 0},
    [D8_RS] = {1, 2, 0}, [D8_CP] = {1, 2, 0}, [D8_CB] = {1, 2, 0},
    [D8_CY] = {2, 1, 0}, [D8_OH] = {2, 1, 0}, [D8_CH] = {1, 2, 0}
};

void drum_kit_default(drum_kit *kit)
{
    for (unsigned track = 0; track < D8_NUM; track++)
        for (unsigned param = 0; param < DRUM_KIT_PARAMS; param++)
            kit->value[track][param] = drum808_param(track, kit_param[track][param])->def;
    kit->alternate = 0;
}

/* Engine parameter index of each switching track's Sound switch. */
static unsigned switch_param(unsigned track)
{
    return track == D8_CP ? 4u : 3u;
}

unsigned drum_track_alternate_bit(unsigned track)
{
    switch (track) {
    case D8_LT: return 1u;
    case D8_MT: return 2u;
    case D8_HT: return 4u;
    case D8_RS: return 8u;
    case D8_CP: return 16u;
    default: return 0;
    }
}

static const uint8_t sound_track[DRUM_SOUNDS] = {
    [D8S_BD] = D8_BD, [D8S_SD] = D8_SD, [D8S_LT] = D8_LT, [D8S_MT] = D8_MT, [D8S_HT] = D8_HT,
    [D8S_LC] = D8_LT, [D8S_MC] = D8_MT, [D8S_HC] = D8_HT, [D8S_RS] = D8_RS, [D8S_CL] = D8_RS,
    [D8S_MA] = D8_CP, [D8S_CP] = D8_CP, [D8S_CB] = D8_CB, [D8S_CH] = D8_CH, [D8S_OH] = D8_OH, [D8S_CY] = D8_CY};

static int sound_alternate(unsigned sound)
{
    return sound == D8S_LC || sound == D8S_MC || sound == D8S_HC || sound == D8S_CL || sound == D8S_MA;
}

unsigned drum_sound_track(unsigned sound)
{
    return sound < DRUM_SOUNDS ? sound_track[sound] : D8_BD;
}

unsigned drum_kit_sound(const drum_kit *kit, unsigned track)
{
    static const uint8_t normal[D8_NUM] = {D8S_BD, D8S_SD, D8S_LT, D8S_MT, D8S_HT, D8S_RS, D8S_CP,
                                           D8S_CB, D8S_CY, D8S_OH, D8S_CH};
    static const uint8_t alternate[D8_NUM] = {[D8_LT] = D8S_LC, [D8_MT] = D8S_MC, [D8_HT] = D8S_HC,
                                              [D8_RS] = D8S_CL, [D8_CP] = D8S_MA};
    if (track >= D8_NUM) return D8S_BD;
    return kit->alternate & drum_track_alternate_bit(track) ? alternate[track] : normal[track];
}

int drum_kit_select_sound(drum_kit *kit, unsigned sound)
{
    /* 8W8's factory pots for the five alternate sounds, in kit order: tune, decay, level. */
    static const uint8_t factory[DRUM_SOUNDS][DRUM_KIT_PARAMS] = {
        [D8S_LC] = {64, 58, 64}, [D8S_MC] = {64, 36, 64}, [D8S_HC] = {64, 34, 64},
        [D8S_CL] = {64, 64, 64}, [D8S_MA] = {64, 51, 64}};
    if (sound >= DRUM_SOUNDS) return 0;
    unsigned track = sound_track[sound], bit = drum_track_alternate_bit(track);
    if (!bit || !(kit->alternate & bit) == !sound_alternate(sound)) return 0;
    kit->alternate ^= (uint8_t)bit;
    for (unsigned param = 0; param < DRUM_KIT_PARAMS; param++)
        kit->value[track][param] = sound_alternate(sound) ? factory[sound][param] :
                                   drum808_param(track, kit_param[track][param])->def;
    return 1;
}

static int trigger_changed(const drum_kit *kit, const drum_kit *previous, unsigned track)
{
    /* Rim decay is passed to its envelope directly when it triggers. */
    return !previous || kit->value[track][DRUM_TUNE] != previous->value[track][DRUM_TUNE] ||
        (track != D8_RS && kit->value[track][DRUM_DECAY] != previous->value[track][DRUM_DECAY]);
}

void drum_kit_resolve(const drum_kit *kit, const drum_kit *previous, drum_kit_values *values)
{
    for (unsigned track = 0; track < D8_NUM; track++) {
        for (unsigned param = 0; param < DRUM_KIT_PARAMS; param++) {
            uint8_t raw = kit->value[track][param];
            if (!previous || raw != previous->value[track][param])
                values->value[track][param] = bb_drum808_kit_value(track, kit_param[track][param], raw);
        }
        if (trigger_changed(kit, previous, track))
            bb_drum808_resolve_trigger(&values->tuning, track, values->value[track][DRUM_TUNE],
                                       kit->value[track][DRUM_DECAY]);
    }
}

unsigned drum_layer_apply_kit(drum_layer *layer, const drum_kit *kit, const drum_kit_values *values)
{
#ifdef ELITE_PERCUSSION_727
    if (layer->percussion.bank) return 0;
#endif
    if (!memcmp(&layer->kit, kit, sizeof *kit)) return 0;
    for (unsigned track = 0; track < D8_NUM; track++) {
        unsigned bit = drum_track_alternate_bit(track);
        unsigned alternate = !!(kit->alternate & bit);
        unsigned switched = alternate != !!(layer->kit.alternate & bit);
        if (switched) drum808_set(&layer->voice, track, (int)switch_param(track), (int)alternate);
        for (unsigned param = 0; param < DRUM_KIT_PARAMS; param++) {
            uint8_t value = kit->value[track][param];
            if (!switched && layer->kit.value[track][param] == value) continue;
            /* An alternate sound's pots are set directly; its hits resolve their own tuning. */
            if (alternate)
                drum808_set(&layer->voice, track, kit_param[track][param], value);
            else if (values)
                bb_drum808_set_kit_value(&layer->voice, track, kit_param[track][param], value,
                                        values->value[track][param]);
            else
                drum808_set(&layer->voice, track, kit_param[track][param], value);
        }
        if (!values && trigger_changed(kit, &layer->kit, track)) {
            float tune = bb_drum808_kit_value(track, kit_param[track][DRUM_TUNE], kit->value[track][DRUM_TUNE]);
            bb_drum808_resolve_trigger(&layer->tuning, track, tune, kit->value[track][DRUM_DECAY]);
        }
    }
    if (values) layer->tuning = values->tuning;
    layer->kit = *kit;
    return 1;
}

void drum_layer_init(drum_layer *layer)
{
    memset(layer, 0, sizeof *layer);
    drum808_init(&layer->voice);
    drum_kit_default(&layer->kit);
    for (unsigned track = 0; track < D8_NUM; track++) {
        float tune = bb_drum808_kit_value(track, kit_param[track][DRUM_TUNE], layer->kit.value[track][DRUM_TUNE]);
        bb_drum808_resolve_trigger(&layer->tuning, track, tune, layer->kit.value[track][DRUM_DECAY]);
    }
    layer->head = DRUM_STEPS;
}

void drum_layer_clear(drum_layer *layer)
{
#ifdef ELITE_PERCUSSION_727
    if (layer->percussion.bank) {
        layer->percussion.active = 0;
        return;
    }
#endif
    memset(layer->voice.lane, 0, sizeof layer->voice.lane);
}

static int32_t mix_q23(int32_t existing, int32_t drum)
{
    int32_t sum = existing + drum;
    if (sum > 8388607) return 8388607;
    if (sum < -8388607) return -8388607;
    return sum;
}

/* Repeat uses the same 24-pulse divisions as the printed RATE keys.
 * Round each event from the bar origin so triplets cannot accumulate drift. */
static unsigned repeat_wait(unsigned phase, unsigned pair, unsigned odd)
{
    unsigned position = phase ? phase * 6u - 3u : 0;
    unsigned within = position % pair;
    unsigned target = position - within;
    if (within > odd) target += pair;
    else if (within) target += odd;
    return (target + 3u) / 6u - phase;
}

static void trigger_voice(drum_layer *layer, unsigned track, float velocity)
{
#ifdef ELITE_PERCUSSION_727
    if (layer->percussion.bank) {
        if (track < D8_NUM)
            percussion_trigger(&layer->percussion, track, layer->percussion_sounds[track],
                               (unsigned)(velocity * 127.0f + 0.5f));
        return;
    }
#endif
    bb_drum808_trigger_prepared(&layer->voice, (int)track, velocity, &layer->tuning);
}

static void trigger_voices(drum_layer *layer, const drum_pattern *pattern,
                           unsigned step, unsigned tick, unsigned repeats)
{
    unsigned mask = 1u << step;
    for (unsigned track = 0; track < D8_NUM; track++) {
        unsigned accent = tick && pattern && (pattern->accents[track] & mask);
        if (repeats & (1u << track)) {
            float velocity = accent ? 1.0f : layer->roll.velocity[track] * (1.0f / 127.0f);
            trigger_voice(layer, track, velocity);
        } else if (tick && pattern && (pattern->hits[track] & mask)) {
            trigger_voice(layer, track, accent ? 1.0f : D8_VEL_NORMAL);
        }
    }
}

static void trigger_usb_voices(drum_layer *layer, const drum_pattern *pattern,
                               unsigned step, unsigned tick, unsigned repeats, unsigned roll_edge)
{
    if (!tick || !pattern || !repeats || roll_edge == DRUM_ROLL_ON_STEP) {
        trigger_voices(layer, pattern, step, tick, repeats);
    } else if (roll_edge == DRUM_ROLL_BEFORE_STEP) {
        /* Drop a stale roll on the same voice and apply the two remaining
         * groups in clock order for the shared metal tuning and hat choke. */
        unsigned mask = 1u << step;
        for (unsigned track = 0; track < D8_NUM; track++)
            if (pattern->hits[track] & mask) repeats &= ~(1u << track);
        if (repeats) trigger_voices(layer, NULL, step, 0, repeats);
        trigger_voices(layer, pattern, step, 1, 0);
    } else {
        drum_pattern step_pattern = *pattern;
        for (unsigned track = 0; track < D8_NUM; track++)
            if (repeats & (1u << track)) step_pattern.hits[track] = step_pattern.accents[track] = 0;
        trigger_voices(layer, &step_pattern, step, 1, 0);
        trigger_voices(layer, NULL, step, 0, repeats);
    }
    if (repeats) layer->repeat_tail = 1;
}

static int render_span(drum_layer *layer, int32_t *stereo, unsigned count, float gain)
{
#ifdef ELITE_PERCUSSION_727
    if (layer->percussion.bank) {
        int32_t dry[DRUM_RENDER_SPAN];
        percussion_render(&layer->percussion, dry, count);
        const float scale = gain / 8388607.0f;
        for (unsigned i = 0; i < count; ++i) {
            int32_t sample = (int32_t)(dry[i] * scale);
            unsigned at = CONDUCTOR_DRY_STRIDE * i;
            stereo[at] = mix_q23(stereo[at], sample);
            if (CONDUCTOR_DRY_STRIDE == 2u) stereo[at + 1] = mix_q23(stereo[at + 1], sample);
        }
        return 0;
    }
#endif
#if CONDUCTOR_DRY_STRIDE == 1u
    return drum_render_q23(&layer->voice, stereo, (int)count, gain);
#else
    float dry[DRUM_RENDER_SPAN];
    memset(dry, 0, count * sizeof *dry);
    bb_drum808_render_dry(&layer->voice, dry, (int)count);
    for (unsigned i = 0; i < count; i++) {
        if (!(dry[i] >= -16.0f && dry[i] <= 16.0f)) return -1;
        int32_t drum = (int32_t)(dry[i] * gain);
        unsigned at = CONDUCTOR_DRY_STRIDE * i;
        stereo[at] = mix_q23(stereo[at], drum);
        if (CONDUCTOR_DRY_STRIDE == 2u)
            stereo[at + 1u] = mix_q23(stereo[at + 1u], drum);
    }
    return 0;
#endif
}

static unsigned held_rolls(drum_layer *layer, uint32_t silence)
{
    unsigned held = 0;
    for (unsigned track = 0; track < D8_NUM; track++)
        if (layer->roll.velocity[track]) held |= 1u << track;
    layer->repeat_blocked &= (uint16_t)held;
    if (layer->silence != silence) layer->repeat_blocked = (uint16_t)held;
    return held & ~layer->repeat_blocked;
}

/* Foreground publishes a strike generation. Consume it once, even if a
 * Start or panic makes the preview obsolete before this audio block. */
static void apply_audition(drum_layer *layer, unsigned stopped)
{
    drum_audition strike = layer->audition;
    if (strike.generation == layer->audition_seen) return;
    layer->audition_seen = strike.generation;
    if (stopped) {
        trigger_voice(layer, strike.track, strike.accent ? 1.0f : D8_VEL_NORMAL);
        layer->audition_tail = 1;
    }
}

int drum_layer_mix(drum_layer *layer, int32_t *stereo, unsigned frames,
                   const tg_controls *controls, unsigned next_edge, const drum_pattern *pattern)
{
    unsigned internal = controls->clock.source == TG_CLOCK_INTERNAL;
    unsigned running = internal && controls->running;
    if (!running) {
        layer->step = 0;
        layer->head = DRUM_STEPS;
    }
    unsigned held = held_rolls(layer, controls->silence);
    unsigned panic = layer->silence != controls->silence;
    if (panic) layer->repeat_tail = layer->audition_tail = 0;
    unsigned rolling = internal ? held : 0;
    if ((!internal || panic || (!pattern && !rolling && !layer->repeat_tail && !layer->audition_tail)) &&
        drum_layer_active(layer))
        drum_layer_clear(layer);
    apply_audition(layer, internal && !running && !panic);
    layer->silence = controls->silence;
    if (!rolling || running) layer->repeat_phase = 0;
    if (!running && !rolling && !drum_layer_active(layer)) {
        layer->repeat_tail = layer->audition_tail = 0;
        return 0;
    }

    unsigned tempo = controls->tempo < 30 ? 30 : controls->tempo;
    unsigned period = (TG_RATE * 15u + tempo / 2u) / tempo;
    unsigned bar = period * DRUM_STEPS;
    unsigned delay = tg_step_delay(period, controls->step_swing);
    unsigned interval = rolling ? period * tg_rate_pulses(layer->roll.rate) : 0;
    unsigned odd = rolling ? drum_roll_odd(interval, layer->roll.swing) : 0;
    unsigned phase = 0, wait = 0, hold = 0;
    if (rolling) {
        /* During a tempo change the current sixteenth can still be longer
         * than the new grid. Rejoin from the start of that interval. */
        unsigned previous = (layer->step + DRUM_STEPS - 1u) % DRUM_STEPS;
        unsigned previous_interval = tg_step_interval(period, delay, previous);
        if (running && next_edge > previous_interval) hold = wait = next_edge - previous_interval;
        phase = running ? (tg_step_position(period, delay, layer->step) + bar - (next_edge - wait)) % bar :
                          layer->repeat_phase % bar;
        wait += repeat_wait(phase, 2u * interval, odd);
    }
    float gain = controls->master * (0.24f * 8388607.0f / 127.0f);
    while (frames) {
        unsigned tick = running && !next_edge;
        unsigned repeats = rolling && !wait ? rolling : 0;
        if (tick || repeats) {
            trigger_voices(layer, pattern, layer->step, tick, repeats);
            if (tick) {
                layer->head = layer->step;
                next_edge = tg_step_interval(period, delay, layer->step);
                layer->step = (uint8_t)((layer->step + 1u) % DRUM_STEPS);
            }
            if (repeats) {
                layer->repeat_tail = 1;
                wait = 1u + repeat_wait((phase + 1u) % bar, 2u * interval, odd);
            }
        }
        unsigned active = (pattern || layer->repeat_tail || layer->audition_tail) && drum_layer_active(layer);
        unsigned count = active && frames > DRUM_RENDER_SPAN ? DRUM_RENDER_SPAN : frames;
        if (running && count > next_edge) count = next_edge;
        if (rolling && count > wait) count = wait;
        if (active && render_span(layer, stereo, count, gain)) return -1;
        stereo += CONDUCTOR_DRY_STRIDE * count;
        frames -= count;
        if (rolling) {
            wait -= count;
            unsigned extra = count < hold ? count : hold;
            hold -= extra;
            phase += count - extra;
            if (phase >= bar) phase -= bar;
        }
        if (running) next_edge -= count;
    }
    if (rolling && !running) layer->repeat_phase = phase;
    if ((layer->repeat_tail || layer->audition_tail) && !drum_layer_active(layer))
        layer->repeat_tail = layer->audition_tail = 0;
    return 0;
}


int drum_layer_mix_usb(drum_layer *layer, int32_t *stereo, unsigned frames,
                       const tg_controls *controls, const drum_pattern *pattern,
                       unsigned next, unsigned tick, unsigned roll_edge, unsigned detach)
{
    unsigned running = controls->clock.state == TG_CLOCK_RUNNING;
    unsigned stopped = controls->clock.state == TG_CLOCK_STOPPED;
    unsigned rolls = held_rolls(layer, controls->silence);
    unsigned panic = layer->silence != controls->silence;
    unsigned tail_transport = running || stopped || controls->clock.state == TG_CLOCK_WAITING;
    unsigned keep_repeat = layer->repeat_tail && tail_transport;
    unsigned keep_pattern = pattern && tail_transport;
    if (detach || panic || (!layer->audition_tail && !keep_repeat && !keep_pattern && !(running && rolls))) {
        if (drum_layer_active(layer)) drum_layer_clear(layer);
        layer->repeat_tail = layer->audition_tail = 0;
    }
    apply_audition(layer, !tg_clock_active(&controls->clock) && !panic);
    layer->silence = controls->silence;
    layer->repeat_phase = 0;
    layer->step = (uint8_t)next;
    if (!running || (detach && !tick)) layer->head = DRUM_STEPS;
    if (running) {
        unsigned step = (next + DRUM_STEPS - 1u) % DRUM_STEPS;
        if (tick) layer->head = (uint8_t)step;
        unsigned repeats = roll_edge ? rolls : 0;
        if (tick || repeats) trigger_usb_voices(layer, pattern, step, tick, repeats, roll_edge);
    } else {
        if (stopped && rolls && roll_edge) trigger_usb_voices(layer, NULL, 0, 0, rolls, roll_edge);
        if (!tail_transport && !layer->audition_tail && !layer->repeat_tail) return 0;
    }
    float gain = controls->master * (0.24f * 8388607.0f / 127.0f);
    while (frames && drum_layer_active(layer)) {
        unsigned count = frames < DRUM_RENDER_SPAN ? frames : DRUM_RENDER_SPAN;
        if (render_span(layer, stereo, count, gain)) return -1;
        stereo += CONDUCTOR_DRY_STRIDE * count;
        frames -= count;
    }
    if (!drum_layer_active(layer)) layer->repeat_tail = layer->audition_tail = 0;
    return 0;
}
