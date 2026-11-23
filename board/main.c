/* SPDX-License-Identifier: GPL-3.0-only */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "vendor/smk37/fm1_block.h"
#include "synth/telegram101.h"
#include "synth/fx101.h"
#ifdef ELITE_DILLO
#include "dillo/layer303.h"
#endif
#include "fm1_sys.h"
#include "fm1_time.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_lcd_hw.h"
#include "fm1_audio.h"
#include "fm1_timer.h"
#include "smk_input.h"
#include "cr_bootguard.h"

#include "library_region.h"
#ifdef ELITE_PERCUSSION_727
/* The 727 build may also write the three sound-pool windows (board/percussion_load.h), the same
 * windows percussion_flash_range accepts. The driver's FL_PROTECTED net still applies. */
#define FL_RANGE_OK(off, n) (FL_IN(off, n, FL_OTA_LO, FL_OTA_HI) || \
                             FL_IN(off, n, ELITE_LIBRARY_LO, ELITE_LIBRARY_HI) || \
                             FL_IN(off, n, FL_SMP_LO, FL_SMP_HI) || \
                             FL_IN(off, n, ELITE_LIBRARY_HI, FL_OTA_LO) || \
                             FL_IN(off, n, FL_DATA_LO, ELITE_LIBRARY_LO))
#else
#define FL_RANGE_OK(off, n) (FL_IN(off, n, FL_OTA_LO, FL_OTA_HI) || \
                             FL_IN(off, n, ELITE_LIBRARY_LO, ELITE_LIBRARY_HI))
#endif
#include "fm1_flash.h"
#ifdef ELITE_PERCUSSION_727
/* Build 813 shipped without the windows in FL_RANGE_OK, so the first erase was refused: every
 * window's first and last sector must pass the driver's own guard. */
_Static_assert(FL_GUARD(FL_DATA_LO, 0x1000u) && FL_GUARD(ELITE_LIBRARY_LO - 0x1000u, 0x1000u) &&
               FL_GUARD(ELITE_LIBRARY_HI, 0x1000u) && FL_GUARD(FL_OTA_LO - 0x1000u, 0x1000u) &&
               FL_GUARD(FL_SMP_LO, 0x1000u) && FL_GUARD(FL_SMP_HI - 0x1000u, 0x1000u),
               "727 pool windows must be writable by the flash driver");
_Static_assert(!FL_GUARD(FL_APP_HI - 0x1000u, 0x1000u) && !FL_GUARD(FL_CAL_LO, 0x1000u),
               "the 727 build must not open the app or calibration");
#include "percussion_flash.h"
#include "percussion_flash.inc"
static percussion_bank percussion_assets;
static percussion_attack_cache percussion_attacks __attribute__((section(".pool"), aligned(16)));
#endif

#define LCD_BAUD 4
#define FELUCCA_OTA 1
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "lcd.c"

static volatile uint32_t fm1_ms;
#include "usb.c"
#include "synth/clockout101.h"
#include "synth/miditrs101.h"
#include "synth/scope101.h"
#include "synth/perform101.h"
#include "synth/sideman101.h"
#include "board/smk_uart_tx.h"

/* MIDI OUT routing (GLOBAL > MIDI OUT) and clock out, set by the foreground each loop. The audio ISR is a
 * second producer of USB-MIDI packets (clock), so the foreground sends with interrupts held. */
static tg_trs_out trs_out;
static tg_clockout clockout;
/* Telegram's live waveform: the audio ISR writes, the play page reads (see synth/scope101.h). */
static tg_scope scope;
#define TG_SCOPE_LIVE (&scope)
/* Telegram's voice as the focus pictures show it: written after each audio block, read by the display. */
static tg_live live_now;
#define TG_LIVE (&live_now)
/* PERFORM (hold FX + the lowest octave): the foreground publishes what is held; the audio ISR plays it. */
static tg_perform perform;
/* In .bss, not .pool: same internal SRAM, and it leaves the pool room for the 727 layer. */
static int16_t perform_ring[TG_PERF_RING] __attribute__((aligned(16)));
static volatile uint8_t perform_fx_on, perform_filters_on, perform_mutes_on;
static volatile uint8_t midi_route = TG_MIDI_ROUTE_USB, clock_out_on, trs_ready;

static void elite_midi_out_isr(uint32_t packet)
{
    if (midi_route & TG_MIDI_ROUTE_USB) midi_out_event(packet);
    if ((midi_route & TG_MIDI_ROUTE_TRS) && trs_ready) (void)tg_trs_queue(&trs_out, packet);
}

static void elite_midi_out(uint32_t packet)
{
    uint32_t icfg = fm1_icfg();
    fm1_irq_off();
    elite_midi_out_isr(packet);
    fm1_icfg_set(icfg);
}
/* USB only: the Sideman state message is for the browser guide, never the TRS jack. All of its packets
 * go in together or none do (a clock pulse from the audio ISR cannot split it), so a full ring never
 * leaves half a SysEx on the wire. */
static unsigned elite_usb_burst(const uint32_t *packets, unsigned n)
{
    uint32_t icfg = fm1_icfg();
    fm1_irq_off();
    unsigned ok = usb.config && MQ - (mo_w - mo_r) >= n;
    for (unsigned i = 0; ok && i < n; i++) midi_out_event(packets[i]);
    fm1_icfg_set(icfg);
    return ok;
}
#include "board/ota_trace.h"

volatile uint32_t elite_ota_trace __attribute__((section(".noinit")));
static elite_usb_boot boot_diag;

static inline void ota_trace(unsigned phase, uint32_t detail)
{
    elite_ota_trace = elite_ota_checkpoint(phase, detail);
}

#include "ota.c"
#include "font.inc"

enum {
    HALF_WORDS = 4 * FM1_BLOCK_FRAMES,
    HALF_TICKS = 544 * (2 * FM1_BLOCK_FRAMES), /* donor codec clock, TIMER4 ticks per frame */
    COMMIT_TICKS = 120000, TICKS_PER_MS = 24000, UI_CHARS = 27,
    LCD_DEFER_TICKS = 8 * TICKS_PER_MS /* 12-row strip takes 3.84 ms at 12 MHz. */
};

enum audio_fault_reason {
    AUDIO_OK, AUDIO_CADENCE, AUDIO_RENDER, AUDIO_NONFINITE, AUDIO_DEADLINE, AUDIO_OWNERSHIP
};

#include "panel101.inc"
#include "editor_protocol.inc"
#include "pad_feedback.inc"
#include "instrument_view.inc"

static struct bootguard guard __attribute__((section(".noinit"), used));
static tg_fx effects __attribute__((section(".pool"), aligned(16)));
static tg_voice engine __attribute__((section(".pool"), aligned(16)));
#ifdef ELITE_DRUMS808
static conductor_launch launch __attribute__((section(".pool"), aligned(16)));
static conductor_patterns launch_requested;
static drum_kit_values launch_kit_buffers[2][2];
static conductor_cancel_drafts cancel_drafts __attribute__((section(".pool"), aligned(4)));
static conductor_mix_state mixer_audio;
static conductor_mix_targets mix_requested;
static volatile uint32_t conductor_commits, conductor_four_blocks, conductor_lane_blocks[MIX_AUDIO_LANES];
static volatile uint32_t conductor_postcopy_peak;
static elite_usb_kit_commit kit_trace[2];
static elite_usb_cue_commit cue_trace;
#ifdef ELITE_RENDER_PROFILE
static elite_usb_render_profile render_profile;
#endif
static uint8_t committed_kit_slots;
static struct {
    uint32_t count, misaligned_launches, block, slots;
    uint16_t telegram_offset, rhythm_offset;
    uint8_t late_usb_step;
} section_trace;
static int32_t dillo_dry[HALF_WORDS / 2u * CONDUCTOR_DRY_STRIDE] __attribute__((section(".pool"), aligned(16)));
static int32_t drums_dry[HALF_WORDS / 2u * CONDUCTOR_DRY_STRIDE] __attribute__((section(".pool"), aligned(16)));
#if !defined(ELITE_SINGLE_808) || defined(ELITE_PERCUSSION_727)
static int32_t drums_b_dry[HALF_WORDS / 2u * CONDUCTOR_DRY_STRIDE] __attribute__((section(".pool"), aligned(16)));
#endif
static volatile uint32_t dillo_applied, drums_applied, telegram_applied, drums_b_applied;
static volatile uint8_t drums_b_active, drums_b_playing, telegram_library_active;
static volatile uint8_t dillo_active, drums_active, telegram_active, dillo_playing, drums_playing, drums_head = TG_STEPS;
#elif defined(ELITE_DILLO)
static dillo_layer dillo __attribute__((section(".pool"), aligned(16)));
static uint8_t dillo_requested;
static dillo_phrase dillo_requested_pattern;
#endif
#ifdef ELITE_DILLO
static volatile uint8_t dillo_head = TG_STEPS;
#endif
static int32_t dma[2 * HALF_WORDS] __attribute__((aligned(16)));
static int32_t scratch[HALF_WORDS];
static uint16_t lcd_row[UI_CHARS * 8 * 16] __attribute__((section(".pool"), aligned(4)));
static tg_controls requested, applied;
static volatile uint8_t audio_fault, seen_half, play_step = TG_STEPS, playing_note;
static volatile uint8_t audio_quiet_blocks;
static volatile uint32_t audio_signal_blocks;
static uint32_t pad_frames, pad_peak_ticks, lcd_strips, live_strikes;
static uint32_t lcd_transfer_tick, lcd_strip_timeouts;
static unsigned next_strip = VIEW_H;
static uint8_t redraw_view, lcd_strip_pending;
static uint8_t visible_pad_head = TG_STEPS;
static volatile uint32_t audio_interval_ticks, audio_peak_ticks, input_ticks;
static volatile uint32_t fault_control_ticks, fault_render_ticks, fault_input_ticks;
static volatile unsigned fault_pad_hits;
static uint8_t safe_mode, updating;
static uint32_t last_half, last_ms_tick;
static uint8_t previous_half;
static const int8_t pad_map[16] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

extern uint32_t _bss_start[], _bss_end[], _pool_start[], _pool_end[];
extern uint32_t _data_load[], _data_start[], _data_end[], _rt_load[], _rt_start[], _rt_end[];
extern void isr_timer5(void), isr_alnk0(void);

static void audio_stop(void)
{
    fm1_irq_mask(FM1_IRQ_ALNK0);
    fm1_audio_stop();
}

static void fm1_fault(const fm1_crash_t *crash)
{
    (void)crash;
    if (elite_ota_previous(elite_ota_trace, 0))
        elite_ota_trace |= ELITE_OTA_TRACE_FAULT;
    audio_stop();
    fm1_reboot();
}

void fm1_timer5_irq(void)
{
    uint32_t start = fm1_ticks();
    fm1_timer5_ack();
    fm1_input_tick();
    if (trs_ready) {
        int byte = tg_trs_tick(&trs_out, smk_uart1_tx_done());
        if (byte >= 0) smk_uart1_tx_byte((uint32_t)byte);
    }
    input_ticks += fm1_ticks() - start;
}

static inline void audio_fail(enum audio_fault_reason reason)
{
    audio_stop();
    audio_fault = (uint8_t)reason;
}

#ifdef ELITE_DRUMS808
static uint32_t conductor_live_notes(void)
{
    unsigned telegram = engine.gate && engine.sounding == engine.controls.live_note ? engine.controls.live_note : 0;
    unsigned dillo = launch.dillo.voice.voice.gate && launch.dillo.voice.last_note == launch.dillo.keys.note ?
                    launch.dillo.keys.note : 0;
    return telegram | dillo << 8;
}

static inline uint32_t cue_slots(unsigned a, unsigned b, unsigned dillo_slot, unsigned telegram)
{
    return a | b << 8 | dillo_slot << 16 | telegram << 24;
}

static inline void record_cue_commit(unsigned signal_lanes, unsigned rendered_b)
{
    unsigned cues = launch.rhythm_cues | (launch.telegram_bar ? CUE_TELEGRAM : 0);
    if (!cues) return;
    cue_trace = (elite_usb_cue_commit){
        .count = cue_trace.count + 1u, .block = conductor_commits, .lanes = cues,
        .before_slots = cue_slots(drums_active, drums_b_active, dillo_active, telegram_active),
        .after_slots = cue_slots(launch.active.drums_slot, launch.active.drums_b.slot,
                                 launch.active.dillo_slot, launch.active.telegram_slot),
        .telegram_offset = launch.telegram_bar, .rhythm_offset = launch.rhythm_cue_offset,
        .late_usb_step = launch.late_usb_step,
        .context = signal_lanes | (applied.clock.source == TG_CLOCK_USB ? ELITE_CUE_USB : 0),
        .live_notes = conductor_live_notes(),
    };
    unsigned three_part_section = !rendered_b && launch_requested.scene;
#ifdef ELITE_BAND_PRESETS
    three_part_section |= launch_requested.band_outgoing_tempo != 0;
#endif
    if (cues == CUE_ALL || (cues == (CUE_ALL & ~CUE_DRUM_B) && three_part_section)) {
        section_trace.count++;
        section_trace.misaligned_launches += launch.telegram_bar != launch.rhythm_cue_offset || launch.late_usb_step;
        section_trace.block = conductor_commits;
        section_trace.slots = cue_trace.after_slots;
        section_trace.telegram_offset = launch.telegram_bar - 1u;
        section_trace.rhythm_offset = launch.rhythm_cue_offset - 1u;
        section_trace.late_usb_step = launch.late_usb_step;
    }
}

static inline void record_kit_commits(unsigned lanes, uint32_t ticks, unsigned rendered_b)
{
    unsigned mask = rendered_b ? 0xffu : 0x0fu;
    unsigned changed_slots = (launch_requested.kit_slots ^ committed_kit_slots) & mask;
    if (!changed_slots) return;
    committed_kit_slots ^= changed_slots;
    for (unsigned part = 0; part < 2; part++) {
        unsigned shift = part * 4u;
        if (!(changed_slots & (0x0fu << shift)) || !(launch.kit_changes & (1u << part))) continue;
        unsigned usb = applied.clock.source == TG_CLOCK_USB;
        unsigned playing = usb ? applied.clock.state == TG_CLOCK_RUNNING : applied.running;
        unsigned aligned = launch.telegram_bar && launch.telegram_bar == launch.rhythm_bar && !launch.late_usb_step;
        kit_trace[part] = (elite_usb_kit_commit){
            .count = kit_trace[part].count + 1u, .block = conductor_commits, .ticks = ticks,
            .context = lanes | (aligned ? ELITE_KIT_ALIGNED_LAUNCH : 0) |
                (usb ? ELITE_KIT_USB : 0) | (playing ? ELITE_KIT_PLAY : 0) |
                ((committed_kit_slots >> shift & 15u) << ELITE_KIT_SLOT_SHIFT) |
                (conductor_live_notes() << ELITE_KIT_TELEGRAM_SHIFT),
        };
    }
}
#endif

#ifdef ELITE_PERCUSSION_727
/* Reserve time for cached percussion, the mixer and DMA commit on either
 * clock. The independent five-millisecond deadline stays active. */
/* LATIN has four byte-PCM rows and its seven Compás rows share
 * choke groups. Use the shorter reserve only for that exact voice map
 * with verified, cached attacks. A custom or stock kit keeps the full
 * reserve even when its displayed kit name is LATIN. */
enum { DRUMS_B_LATEST_TICKS = COMMIT_TICKS - 1100u * 24u,
       LATIN_LATEST_TICKS = COMMIT_TICKS - 880u * 24u };
static uint32_t audio_block_start;
static unsigned drums_b_fits(void)
{
    const drum_layer *layer = &launch.drums_b;
    unsigned limit = DRUMS_B_LATEST_TICKS;
    if (layer->percussion.bank && layer->percussion.bank->attacks &&
        !memcmp(layer->percussion_sounds, percussion_factory_kits[P727_LATIN], P727_TRACKS))
        limit = LATIN_LATEST_TICKS;
    return fm1_ticks() - audio_block_start < limit;
}
#endif

#ifdef ELITE_DRUMS808
/* The freestanding memset writes one byte per iteration. These three dry
 * buffers are aligned samples and are cleared for every DMA half. */
static inline void clear_dry_lane(int32_t *samples, unsigned count)
{
    for (unsigned i = 0; i < count; i++) samples[i] = 0;
}
#endif

void fm1_alnk0_irq(void)
{
    uint32_t scan_start = input_ticks;
    uint32_t start = fm1_ticks();
#ifdef ELITE_PERCUSSION_727
    audio_block_start = start;
#endif
    uint8_t pending = fm1_audio_pending();
    fm1_audio_ack_aux(pending);
    if (!(pending & FM1_AUDIO_HALF))
        return;
    fm1_audio_ack_half();
    uint32_t half = fm1_audio_free_half();
    uint32_t interval = start - last_half;
    last_half = start;
    if (seen_half) {
        audio_interval_ticks = interval;
        if (half == previous_half || interval > HALF_TICKS + TICKS_PER_MS / 2u ||
            interval < HALF_TICKS - TICKS_PER_MS / 2u) {
            audio_fail(AUDIO_CADENCE);
            return;
        }
    }
    seen_half = 1;
    previous_half = (uint8_t)half;

#ifdef ELITE_DRUMS808
    if (launch.consumed_publication != launch_requested.publication) {
#else
    if (memcmp(&requested, &applied, sizeof applied)) {
#endif
        if (requested.silence != applied.silence) tg_fx_clear(&effects);
        applied = requested;
#ifndef ELITE_DRUMS808
        tg_apply(&engine, &applied);
#endif
    }
    /* Echoes on 1/8 and the 1/4 triplet: the knob's tempo, or the source's beat when following clock
     * (12 pulses an eighth; changes under 0.5% are its jitter and are left alone). */
    if (applied.clock.source == TG_CLOCK_USB && applied.clock.period_us)
        tg_fx_set_eighth(&effects, (applied.clock.period_us * 5292u + 5000u) / 10000u, 5);
    else
        tg_fx_set_tempo(&effects, applied.tempo);
    uint32_t render_start = fm1_ticks();
#ifdef ELITE_DRUMS808
    unsigned edge_before;
    if (conductor_render_telegram(&launch, &engine, scratch, HALF_WORDS / 2u,
                                  &applied, &launch_requested, &edge_before)) {
        audio_fail(AUDIO_NONFINITE);
        return;
    }
#else
#ifdef ELITE_DILLO
    unsigned edge_before = engine.remaining;
#endif
    for (unsigned offset = 0; offset < HALF_WORDS; offset += 2 * FM1_BLOCK_FRAMES) {
        if (tg_render(&engine, scratch + offset, FM1_BLOCK_FRAMES)) {
            audio_fail(AUDIO_NONFINITE);
            return;
        }
    }
#endif
    tg_scope_write(&scope, scratch, HALF_WORDS / 2u); /* Telegram alone, before drums and Dillo */
    tg_live_read(&engine, &live_now);
    tg_perform_lane(&perform, 0, scratch, HALF_WORDS / 2u, 2, perform_mutes_on & 1u);
#ifdef ELITE_DRUMS808
#ifdef ELITE_RENDER_PROFILE
    uint32_t telegram_ticks = fm1_ticks() - render_start;
    memset(launch.layer_ticks, 0, sizeof launch.layer_ticks);
#endif
    clear_dry_lane(dillo_dry, sizeof dillo_dry / sizeof dillo_dry[0]);
    clear_dry_lane(drums_dry, sizeof drums_dry / sizeof drums_dry[0]);
    int32_t *second_dry = NULL;
#if !defined(ELITE_SINGLE_808) || defined(ELITE_PERCUSSION_727)
    if (
#ifdef ELITE_PERCUSSION_727
        launch.drums_b.percussion.bank &&
#endif
        conductor_drums_b_active(&launch, &launch_requested)) {
        second_dry = drums_b_dry;
        clear_dry_lane(second_dry, sizeof drums_b_dry / sizeof drums_b_dry[0]);
    }
#endif
    if (conductor_mix(&launch, dillo_dry, drums_dry, second_dry, HALF_WORDS / 2u,
                      &applied, edge_before, &launch_requested)) {
        audio_fail(AUDIO_NONFINITE);
        return;
    }
#ifdef ELITE_RENDER_PROFILE
    uint32_t mixer_start = fm1_ticks();
#endif
    tg_perform_lane(&perform, 1, dillo_dry, HALF_WORDS / 2u, CONDUCTOR_DRY_STRIDE, perform_mutes_on & 2u);
    tg_perform_lane(&perform, 2, drums_dry, HALF_WORDS / 2u, CONDUCTOR_DRY_STRIDE, perform_mutes_on & 4u);
    tg_perform_lane(&perform, 3, second_dry, HALF_WORDS / 2u, CONDUCTOR_DRY_STRIDE, perform_mutes_on & 8u);
    conductor_mix_audio_prepared(&mixer_audio, &effects, scratch, dillo_dry, drums_dry, second_dry,
                                 HALF_WORDS / 2u, &mix_requested, applied.master);
#ifdef ELITE_RENDER_PROFILE
    uint32_t mixer_ticks = fm1_ticks() - mixer_start;
#endif
    const dillo_layer *playing_dillo = &launch.dillo;
#else
    tg_fx_render(&effects, scratch, HALF_WORDS / 2u, applied.patch.value[TG_DELAY],
                 applied.patch.value[TG_REVERB], applied.master);
#ifdef ELITE_DILLO
    if (dillo_layer_mix(&dillo, scratch, HALF_WORDS / 2u, &applied, edge_before,
                        dillo_requested ? &dillo_requested_pattern : NULL)) {
        audio_fail(AUDIO_NONFINITE);
        return;
    }
    const dillo_layer *playing_dillo = &dillo;
#endif
#endif
    tg_perform_render(&perform, scratch, HALF_WORDS / 2u, applied.tempo,
                      perform_fx_on ? perform_fx_on - 1u : TG_PF_NONE, perform_filters_on);
    if (clock_out_on || clockout.running) {
        uint8_t events[TG_CLOCKOUT_MAX_EVENTS];
        unsigned playing = clock_out_on && applied.clock.source == TG_CLOCK_INTERNAL && applied.running;
        unsigned n = tg_clockout_block(&clockout, playing, applied.tempo, HALF_WORDS / 2u, events);
        for (unsigned i = 0; i < n; i++) elite_midi_out_isr(0x0fu | (uint32_t)events[i] << 8);
    }
    uint32_t render_end = fm1_ticks();
    /* Keep the same DMA safety limits as the review build. Record which
     * limit stopped playback before any attempted write to a busy half. */
    uint32_t icfg = fm1_icfg();
    fm1_irq_off();
    uint32_t elapsed = fm1_ticks() - start;
    if (elapsed > audio_peak_ticks)
        audio_peak_ticks = elapsed;
    if (elapsed >= COMMIT_TICKS) {
        fault_control_ticks = render_start - start;
        fault_render_ticks = render_end - render_start;
        fault_input_ticks = input_ticks - scan_start;
        fault_pad_hits = 0;
#ifdef ELITE_RENDER_PROFILE
        uint32_t measured = fault_control_ticks + telegram_ticks + mixer_ticks +
            launch.layer_ticks[MIX_DILLO] + launch.layer_ticks[MIX_808] + launch.layer_ticks[MIX_808_B];
        render_profile = (elite_usb_render_profile){
            .magic = ELITE_USB_DIAG_MAGIC, .build = ELITE_BUILD, .committed_blocks = conductor_commits,
            .elapsed_ticks = elapsed, .fault = AUDIO_DEADLINE, .rhythm_step = launch.drums.head,
            .telegram_ticks = telegram_ticks, .dillo_ticks = launch.layer_ticks[MIX_DILLO],
            .drums_a_ticks = launch.layer_ticks[MIX_808], .drums_b_ticks = launch.layer_ticks[MIX_808_B],
            .mixer_ticks = mixer_ticks, .control_ticks = fault_control_ticks,
            .other_ticks = elapsed - measured, .scan_ticks = fault_input_ticks,
            .callback_ticks = HALF_TICKS, .guard_ticks = COMMIT_TICKS,
        };
#endif
        audio_fail(AUDIO_DEADLINE);
    } else if ((fm1_audio_pending() & FM1_AUDIO_HALF) || fm1_audio_free_half() != half) {
        audio_fail(AUDIO_OWNERSHIP);
    } else {
        unsigned signal = 0;
        for (unsigned i = 0; i < HALF_WORDS; i++) {
            dma[half * HALF_WORDS + i] = scratch[i];
            if (scratch[i] > 256 || scratch[i] < -256) signal = 1;
        }
        unsigned quiet = !signal && !applied.running && !applied.live_note && !applied.arp_count;
        audio_quiet_blocks = quiet ? (audio_quiet_blocks < 4 ? audio_quiet_blocks + 1 : 4) : 0;
        __asm__ volatile("csync" ::: "memory");
        if ((fm1_audio_pending() & FM1_AUDIO_HALF) || fm1_audio_free_half() != half)
            audio_fail(AUDIO_OWNERSHIP);
        else {
            audio_signal_blocks += signal;
            /* The panel acknowledges only audio that reached a safe DMA half. */
            play_step = applied.running ? engine.play_step : TG_STEPS;
            playing_note = engine.sounding;
#ifdef ELITE_DILLO
            dillo_head = playing_dillo->head < DILLO_STEPS ? playing_dillo->head : TG_STEPS;
#endif
#ifdef ELITE_DRUMS808
            unsigned lanes = mixer_audio.signal_lanes;
            conductor_commits++;
            record_cue_commit(lanes, second_dry != NULL);
            drums_b_applied = launch.active.drums_b.cue;
            drums_b_active = launch.active.drums_b.slot;
            drums_b_playing = second_dry && launch.active.drums_b.enabled && !launch.active.drums_b.muted;
            drums_head = launch.drums.head;
            dillo_applied = launch.active.dillo_cue;
            drums_applied = launch.active.drums_cue;
            telegram_applied = launch.active.telegram_cue;
            telegram_active = launch.active.telegram_slot;
            telegram_library_active = launch.active.telegram_library;
            dillo_active = launch.active.dillo_slot;
            drums_active = launch.active.drums_slot;
            dillo_playing = launch.active.dillo_on;
            drums_playing = launch.active.drums_on;
            conductor_four_blocks += lanes == (1u << MIX_AUDIO_LANES) - 1u;
            for (unsigned lane = 0; lane < MIX_AUDIO_LANES; lane++)
                conductor_lane_blocks[lane] += (lanes >> lane) & 1u;
            elapsed = fm1_ticks() - start;
            if (elapsed > conductor_postcopy_peak) conductor_postcopy_peak = elapsed;
            record_kit_commits(lanes, elapsed, second_dry != NULL);
#endif
        }
    }
    fm1_icfg_set(icfg);
}

#include "library_audio.inc"
#include "library_storage.inc"

static void finish_display_transfer(void)
{
    uint32_t timeouts = lcd_strip_pending ? lcd_strip_timeouts : fm1_lcd_timeouts;
    lcd_sync();
    if (fm1_lcd_timeouts != timeouts) {
        redraw_view = 1;
        next_strip = 0;
    } else {
        lcd_strips += lcd_strip_pending;
    }
    lcd_strip_pending = 0;
}

static void line(unsigned y, const char *s, uint16_t color)
{
    finish_display_transfer();
    uint16_t swapped = (uint16_t)((color << 8) | (color >> 8));
    for (unsigned c = 0; c < UI_CHARS; c++) {
        unsigned ch = *s ? (unsigned char)*s++ : ' ';
        for (unsigned row = 0; row < 16; row++) {
            uint8_t bits = font[ch - ' '][row];
            for (unsigned px = 0; px < 8; px++)
                lcd_row[row * UI_CHARS * 8 + c * 8 + px] = bits & (0x80u >> px) ? swapped : 0;
        }
    }
    lcd_transfer_tick = fm1_ticks();
    lcd_blit(12, y, UI_CHARS * 8, 16, lcd_row);
}

static void number(char *p, unsigned n)
{
    p[0] = (char)('0' + n / 100);
    p[1] = (char)('0' + n / 10 % 10);
    p[2] = (char)('0' + n % 10);
}

static unsigned panel_head(const struct panel *panel)
{
    unsigned head = panel->sound.running ? play_step : TG_STEPS;
#ifdef ELITE_DILLO
    if (panel->page == PAGE_DILLO) head = dillo_head;
#endif
#ifdef ELITE_DRUMS808
    if (panel->page == PAGE_DRUMS || panel->page == PAGE_CONDUCTOR) head = drums_head;
    head = conductor_edit_head(panel, head);
#endif
    return head;
}

static void refresh_pads(const struct panel *panel)
{
    unsigned head = panel_head(panel);
    for (unsigned pad = 0; pad < TG_PADS; pad++) {
        uint32_t rgb = safe_mode || audio_fault || updating ? 0 : pad_color(panel, pad, head);
        padled_set(pad, rgb >> 16, rgb >> 8, rgb);
    }
    if (padled_dirty) {
        lcd_sync();
        uint32_t timeouts = fm1_lcd_timeouts;
        uint32_t start = fm1_ticks();
        padled_flush();
        uint32_t elapsed = fm1_ticks() - start;
        if (elapsed > pad_peak_ticks) pad_peak_ticks = elapsed;
        if (fm1_lcd_timeouts == timeouts) pad_frames++;
        else padled_dirty = 1; /* The driver clears dirty before sending. */
    }
    visible_pad_head = panel->sound.running ? play_step : TG_STEPS;
}

static uint16_t view_frame[VIEW_W * VIEW_H] __attribute__((section(".pool"), aligned(4)));
static uint16_t view_sent[VIEW_W * VIEW_H] __attribute__((section(".pool"), aligned(4)));
static uint16_t view_dma[VIEW_W * VIEW_STRIP] __attribute__((section(".pool"), aligned(4)));

static unsigned display_bus_ready(void)
{
    if (lcd_busy && !(FM1_LCD_SPI_CON & 0x8000u) &&
        fm1_ticks() - lcd_transfer_tick < LCD_DEFER_TICKS) return 0;
    if (lcd_busy || lcd_strip_pending) finish_display_transfer();
    return 1;
}

static unsigned panel_beat(const struct panel *panel)
{
#ifdef ELITE_DRUMS808
    if (!audio_fault && !safe_mode) return view_header_beat(panel, drums_head);
    return HEADER_STOPPED;
#else
    (void)panel;
    return 0;
#endif
}

static void show_panel(const struct panel *panel, unsigned head, unsigned beat)
{
    draw_instrument(view_frame, panel, head, playing_note, audio_fault, safe_mode, beat);
    next_strip = 0;
}

#ifdef ELITE_DRUMS808
static unsigned display_panel_changed(struct panel *shown, const struct panel *next)
{
    /* Display the clock source, transport state and rounded tempo. The
     * substep counters and timestamps belong to the audio/transport path. */
    tg_clock_snapshot *prior = &shown->sound.clock;
    const tg_clock_snapshot *clock = &next->sound.clock;
    if (prior->source == clock->source && prior->state == clock->state &&
        control_value(&shown->sound, CTRL_TEMPO) == control_value(&next->sound, CTRL_TEMPO))
        *prior = *clock;
    shown->sound.clock_age_us = next->sound.clock_age_us;
    shown->view_frames = next->view_frames;
    return memcmp(shown, next, sizeof *shown) != 0;
}

static unsigned focus_animation_due(const struct panel *panel, uint32_t previous)
{
    if (!panel->strip_knob || panel->view_frames / 10u == previous / 10u ||
        (uint32_t)(panel->view_frames - panel->strip_frame) >= CONTROL_FOCUS_FRAMES ||
        panel->key_transpose_armed || audio_fault || safe_mode)
        return 0;
    struct focus_card card;
    if (!focus_lookup(panel, panel->strip_knob - 1u, &card)) return 0;
    if ((uint32_t)(panel->view_frames - panel->strip_frame) < 100u) return 1; /* the dial's turn trail */
    if (panel->motion_state) return panel->view_frames / 40u != previous / 40u; /* MOTION: the bar marker, about 16 times a second */
    switch (card.picture) {
    case PIC_LEVEL: case PIC_PWM: case PIC_LFO: case PIC_VCF_LFO: case PIC_VIBRATO:
    case PIC_TEMPO: case PIC_DELAY: case PIC_REVERB: return 1;
    /* The filter and envelope pictures move only when the voice does: redraw while a note sounds,
     * and once more as it ends so the marker is cleared. */
    case PIC_CUTOFF: case PIC_RES: case PIC_ENV: case PIC_ADSR_STAGE: case PIC_DECAY_101: {
        static uint8_t was_active;
#ifdef TG_LIVE
        unsigned active = TG_LIVE->active;
#else
        unsigned active = 0; /* a host scheduler model without the voice */
#endif
        unsigned due = active || was_active;
        was_active = (uint8_t)active;
        return due;
    }
    default: return 0;
    }
}
#endif

enum { LCD_DIRTY_COLUMNS = 12 };
_Static_assert(VIEW_W % LCD_DIRTY_COLUMNS == 0, "Whole dirty columns must fit an LCD strip");

static unsigned display_columns_changed(const uint16_t *source, const uint16_t *sent, unsigned x)
{
    for (unsigned row = 0; row < VIEW_STRIP; row++, x += VIEW_W)
        if (memcmp(source + x, sent + x, LCD_DIRTY_COLUMNS * sizeof *source))
            return 1;
    return 0;
}

static void service_display(void)
{
    while (next_strip < VIEW_H) {
        unsigned y = next_strip;
        next_strip += VIEW_STRIP;
        const uint16_t *source = &view_frame[y * VIEW_W];
        uint16_t *sent = &view_sent[y * VIEW_W];
        unsigned left = 0, right = VIEW_W;
        if (!redraw_view) {
            if (!memcmp(source, sent, sizeof view_dma)) continue;
            /* Send one rectangle, trimming unchanged columns on both sides. */
            while (left + LCD_DIRTY_COLUMNS < right && !display_columns_changed(source, sent, left))
                left += LCD_DIRTY_COLUMNS;
            while (right > left + LCD_DIRTY_COLUMNS &&
                   !display_columns_changed(source, sent, right - LCD_DIRTY_COLUMNS))
                right -= LCD_DIRTY_COLUMNS;
        }
        lcd_sync();
        unsigned width = right - left;
        uint16_t *packed = view_dma;
        for (unsigned row = 0, offset = left; row < VIEW_STRIP; row++, offset += VIEW_W) {
            memcpy(sent + offset, source + offset, width * sizeof *source);
            for (unsigned x = 0; x < width; x++) {
                uint16_t color = source[offset + x];
                *packed++ = (uint16_t)((color << 8) | (color >> 8));
            }
        }
        lcd_transfer_tick = fm1_ticks();
        lcd_strip_timeouts = fm1_lcd_timeouts;
        lcd_blit(left, y, width, VIEW_STRIP, view_dma);
        lcd_strip_pending = 1;
        return;
    }
    redraw_view = 0;
}

static void show_usb_activity(void)
{
    static uint32_t last_rx = UINT32_MAX, last_tx = UINT32_MAX, last_ack = UINT32_MAX;
    uint32_t rx = usb.rx_pkts, tx = usb.tx_pkts, ack = so_gone;
    if (rx == last_rx && tx == last_tx && ack == last_ack)
        return;
    last_rx = rx;
    last_tx = tx;
    last_ack = ack;
    char text[] = "USB RX000 TX000 ACK000";
    number(text + 6, rx % 1000u);
    number(text + 12, tx % 1000u);
    number(text + 19, ack % 1000u);
    line(178, text, 0x8410);
}

static void update_clock(void)
{
    uint32_t ms = (fm1_ticks() - last_ms_tick) / TICKS_PER_MS;
    last_ms_tick += ms * TICKS_PER_MS;
    fm1_ms += ms;
}

#include "midi_performance.inc"
#include "midi_output.inc"

static void poll_update_usb(void)
{
    midi_drain();
    usb_poll();
    midi_drain();
}

static uint32_t ota_now_ms(void) { update_clock(); return fm1_ms; }
static void ota_idle(void) { update_clock(); fm1_wdt_feed(); poll_update_usb(); }
static int ota_erase(uint32_t off)
{
    uint32_t elapsed;
    return FL_IN(off, 4096u, FL_OTA_LO, FL_OTA_HI) ? fl_erase4k(off, &elapsed) : -1;
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    return FL_IN(off, n, FL_OTA_LO, FL_OTA_HI) ? fl_write(off, p, n) : -1;
}
static int ota_fread(uint32_t off, void *p, uint32_t n)
{
    if (FL_IN(off, n, FL_OTA_LO, FL_OTA_HI)) {
        memcpy(p, (const void *)FL_XIP(off), n);
        return 0;
    }
    if (off >= FL_HEAD_HI || n > FL_HEAD_HI - off)
        return -1;
    /* Stock rollback compares the raw SPL and config bytes before staging.
     * The XIP app mapping starts at flash 0x4000. */
    uint32_t irq = irq_save();
    int rc = FL_FAR(fl_read_ram)(off, p, n);
    irq_restore(irq);
    return rc;
}
static void ota_show(uint32_t step, int32_t code)
{
    ota_trace(ELITE_OTA_STATUS, step | ((uint32_t)(uint8_t)code << 8));
    elite_ota_flags = (elite_ota_flags & ELITE_USB_OTA_ACTIVE) |
                     (step << ELITE_USB_OTA_STAGE_SHIFT) |
                     ((uint32_t)(uint8_t)code << ELITE_USB_OTA_ERROR_SHIFT);
    char text[] = "UPDATE STEP 0  ERR -000";
    text[12] = (char)('0' + step);
    text[19] = code < 0 ? '-' : ' ';
    number(text + 20, (unsigned)(code < 0 ? -code : code));
    line(198, text, 0xfd20);
    show_usb_activity();
}
static void ota_commit(const uint8_t *parm)
{
    ota_trace(ELITE_OTA_COMMIT, 0);
    bootguard_intentional(&guard);
    usb_detach();
    fm1_enter_update(parm);
}

#include "ota_lifecycle.inc"
#include "usb_reconnect.inc"

enum bench_result { BENCH_IDLE, BENCH_RUNNING, BENCH_DONE, BENCH_FAULT, BENCH_STOPPED };
static uint8_t bench_result;
static uint32_t bench_start;
#ifdef ELITE_SINGLE_808
enum { BENCH_CLAVES = 1u, BENCH_DRIVE = 2u, BENCH_727 = 4u };
static uint8_t bench_cymbal, bench_usb_armed, bench_usb_started;
static uint8_t bench_options, bench_saved_page, bench_saved_cursor;
static tg_clock bench_previous_clock;

static void bench_restore_view(struct panel *panel)
{
    if (bench_result == BENCH_RUNNING || !bench_cymbal) return;
    if (panel->page == PAGE_STEPS) {
        panel->page = bench_saved_page;
        panel->cursor = bench_saved_cursor;
    }
    bench_cymbal = 0;
}

static void bench_restore_usb_clock(struct panel *panel)
{
    if (!bench_usb_armed) return;
    tg_clock measured = performance_clock;
    performance_clock = bench_previous_clock;
    performance_clock.out.epoch = measured.out.epoch + 1u;
    performance_clock.out.steps = measured.out.steps;
    performance_clock.out.base = measured.out.steps -
        (bench_previous_clock.out.steps - bench_previous_clock.out.base);
    performance_clock.out.gate_breaks = measured.out.gate_breaks + 1u;
    performance_clock.out.losses = measured.out.losses;
    performance_clock.received = measured.received;
    performance_clock.clocks = measured.clocks;
    performance_clock.ignored = measured.ignored;
    performance_clock.coalesced = measured.coalesced;
    panel->sound.clock = performance_clock.out;
    panel->sound.clock_age_us = 0;
    panel->sound.running = 0;
    bench_usb_armed = bench_usb_started = 0;
}
#endif

static int bench_message(struct panel *next)
{
    const uint8_t *message;
    uint32_t length;
#ifdef ELITE_SINGLE_808
    const unsigned last_command = 5;
#else
    const unsigned last_command = 3;
#endif
    if (!ota_frame_get(&message, &length) || length < 4 ||
        memcmp(message, "\x7d\x45\x4c", 3) || message[3] > last_command)
        return 0;
    unsigned command = message[3];
#ifdef ELITE_SINGLE_808
    if (length != 4 && (length != 5 || command < 4 || message[4] > (BENCH_CLAVES | BENCH_DRIVE | BENCH_727)))
        return 0;
    unsigned options = length == 5 ? message[4] : 0;
#ifndef ELITE_PERCUSSION_727
    if (options & BENCH_727) return 0;
#endif
#else
    if (length != 4) return 0;
#endif
    ota_frame_done();
    if (command == 3) {
        uint8_t reply[] = {0xf0, 0x7d, 0x45, 0x4c, 0x41, ELITE_BUILD / 100, ELITE_BUILD % 100,
                           0, 0, 0, 0, 0, 0, 0, 0, 0, 0xf7};
        uint32_t values[] = {pad_frames, pad_peak_ticks, fm1_lcd_timeouts};
        for (unsigned i = 0; i < 3; i++)
            for (unsigned group = 0; group < 3; group++)
                reply[7 + i * 3 + group] = (uint8_t)((values[i] >> (group * 7)) & 127u);
        ota_wire_send(reply, sizeof reply);
        return 1;
    }
    unsigned start = command == 1;
#ifdef ELITE_SINGLE_808
    if (command >= 4)
        start = (command == 5 || next->sound.clock.source == TG_CLOCK_INTERNAL) &&
                next->drums_enabled && next->dillo_enabled && !next->sound.phrase_muted &&
                !next->record && !next->dillo_record && !next->dillo_live.note &&
                !next->sound.live_note && !next->sound.arp && !fm1_in.keys &&
                !next->pads_down && !drum_roll_active(&next->drums_roll);
    if (bench_usb_armed) start = 0;
#ifdef ELITE_PERCUSSION_727
    if (options & BENCH_727) start &= next->layer_available;
#endif
#endif
    if (start && bench_result != BENCH_RUNNING && !next->sound.running && seen_half && !audio_fault && !safe_mode) {
#ifdef ELITE_SINGLE_808
        bench_cymbal = command >= 4;
        bench_options = (uint8_t)options;
        if (bench_cymbal) {
            bench_saved_page = next->page;
            bench_saved_cursor = next->cursor;
            next->page = PAGE_STEPS;
            next->cursor = 0;
        }
        if (command == 5) {
            bench_previous_clock = performance_clock;
            tg_clock_select(&performance_clock, TG_CLOCK_USB, fm1_ms * 1000u);
            tg_clock_transport(&performance_clock, 0xfc, fm1_ms * 1000u);
            next->sound.clock = performance_clock.out;
            bench_usb_armed = 1;
        }
#endif
        bench_result = BENCH_RUNNING;
        bench_start = fm1_ms;
        if (command == 1) {
            next->page = PAGE_STEPS;
            next->hold = 0;
            if (!fm1_in.keys && !next->pads_down) next->sound.live_note = 0;
        }
        fm1_irq_off();
        audio_peak_ticks = 0;
        fm1_irq_on();
        next->sound.running = command != 5;
    }
    if (command == 2) {
        if (bench_result == BENCH_RUNNING) {
            next->sound.running = 0;
            bench_result = BENCH_STOPPED;
        }
#ifdef ELITE_SINGLE_808
        bench_restore_usb_clock(next);
#endif
    }
    uint8_t reply[] = {0xf0, 0x7d, 0x45, 0x4c, 0x40, ELITE_BUILD / 100, ELITE_BUILD % 100,
                       bench_result, audio_fault, next->sound.master,
                       0, 0, 0, 0, 0, 0, 0xf7};
    uint32_t values[] = {audio_peak_ticks, audio_interval_ticks};
    for (unsigned i = 0; i < 2; ++i)
        for (unsigned group = 0; group < 3; ++group)
            reply[10 + i * 3 + group] = (uint8_t)((values[i] >> (group * 7)) & 127u);
    ota_wire_send(reply, sizeof reply);
    return 1;
}

static void bench_tick(struct panel *next)
{
    uint32_t elapsed = fm1_ms - bench_start;
#ifdef ELITE_SINGLE_808
    if (bench_usb_armed) {
        if (bench_result != BENCH_RUNNING) {
            /* Give the host time to verify its final clock count after MIDI Stop. */
            if (elapsed >= 3000u) bench_restore_usb_clock(next);
            return;
        }
        if (audio_fault || next->sound.clock.source != TG_CLOCK_USB) {
            bench_result = audio_fault ? BENCH_FAULT : BENCH_STOPPED;
            bench_restore_usb_clock(next);
            return;
        }
        if (!bench_usb_started) {
            if (next->sound.clock.state == TG_CLOCK_RUNNING) {
                bench_usb_started = 1;
                bench_start = fm1_ms;
            } else if (elapsed >= 8000u) {
                bench_result = BENCH_STOPPED;
                bench_restore_usb_clock(next);
            }
        } else if (next->sound.clock.state != TG_CLOCK_RUNNING) {
            bench_result = BENCH_STOPPED;
            bench_start = fm1_ms;
        } else if (elapsed >= 125000u) {
            bench_result = BENCH_DONE;
            bench_restore_usb_clock(next);
        }
        return;
    }
#endif
    if (bench_result != BENCH_RUNNING)
        return;
    if (audio_fault) {
        bench_result = BENCH_FAULT;
        next->sound.running = 0;
    } else if (!next->sound.running) {
        bench_result = BENCH_STOPPED;
    } else if (elapsed >=
#ifdef ELITE_SINGLE_808
               (bench_cymbal ? 125000u : 20000u)
#else
               20000u
#endif
    ) {
        bench_result = BENCH_DONE;
        next->sound.running = 0;
    }
}

#ifdef ELITE_SINGLE_808
#ifdef ELITE_PERCUSSION_727
#include "percussion_bench.inc"
#endif
static void bench_cymbal_fixture(tg_controls *sound, conductor_patterns *patterns)
{
    static const uint8_t phrase[] = {
        48, 48 | TG_TIE, 48 | TG_TIE, 48 | TG_TIE,
        55, 55 | TG_TIE, 55 | TG_TIE, 55 | TG_TIE,
        51, 51 | TG_TIE, 51 | TG_TIE, 51 | TG_TIE,
        53, 53 | TG_TIE, 53 | TG_TIE, 53 | TG_TIE,
    };
    static const dillo_phrase bass = {.length = 16, .steps = {
        {DILLO_NOTE, 36, 98, DILLO_ACCENT}, {DILLO_TIE, 0, 98, 0},
        {DILLO_NOTE, 36, 98, 0}, {DILLO_NOTE, 43, 98, 0},
        {DILLO_NOTE, 43, 98, DILLO_ACCENT}, {DILLO_TIE, 0, 98, 0},
        {DILLO_NOTE, 43, 98, 0}, {DILLO_NOTE, 43, 98, 0},
        {DILLO_NOTE, 39, 98, DILLO_ACCENT}, {DILLO_TIE, 0, 98, 0},
        {DILLO_NOTE, 39, 98, 0}, {DILLO_NOTE, 46, 98, 0},
        {DILLO_NOTE, 41, 98, DILLO_ACCENT}, {DILLO_TIE, 0, 98, 0},
        {DILLO_NOTE, 41, 98, 0}, {DILLO_NOTE, 36, 98, 0},
    }};
    memset(sound->steps, 0, sizeof sound->steps);
    memcpy(sound->steps, phrase, sizeof phrase);
    sound->length = sizeof phrase;
    sound->phrase_gate = 98;
    patterns->dillo = bass;
    patterns->drum_keys = (drum_roll){.rate = 5, .velocity = {[D8_CY] = 96}};
#ifdef ELITE_PERCUSSION_727
    if (bench_options & BENCH_727) bench_percussion_fixture(patterns);
#endif
    if (bench_options & BENCH_CLAVES) {
        drum_kit_select_sound(&patterns->kit, D8S_CL);
        patterns->drums.hits[D8_RS] = UINT16_MAX;
        patterns->drums.accents[D8_RS] = 0x1111;
    }
    if (bench_options & BENCH_DRIVE) {
        patterns->dillo_sound.tone.drive = CONDUCTOR_DILLO_START_DRIVE;
        sound->patch.drive = CONDUCTOR_TELEGRAM_START_DRIVE;
        patterns->mixer.value[MIX_TELEGRAM][MIX_DRIVE] = sound->patch.drive;
    }
}
#endif

static void elite_usb_audio_send(uint16_t length)
{
    static elite_usb_audio status;
    uint32_t tx_csr = elite_usb_tx_csr();
    status = (elite_usb_audio){
        .magic = ELITE_USB_DIAG_MAGIC,
        .build = ELITE_BUILD,
        .commit_ticks = COMMIT_TICKS,
        .master = requested.master,
        .playing = requested.running,
        .tempo = requested.tempo,
        .bench = bench_result,
        .tx_csr = tx_csr,
        .tx_queued = elite_usb_unacknowledged_sysex(tx_csr),
    };
    uint32_t config = fm1_icfg();
    fm1_irq_off();
    status.irq_ticks = audio_interval_ticks;
    status.mix_ticks = audio_peak_ticks;
    status.control_ticks = fault_control_ticks;
    status.render_ticks = fault_render_ticks;
    status.scan_ticks = fault_input_ticks;
    status.pad_hits = fault_pad_hits;
    status.fault = audio_fault;
    fm1_icfg_set(config);
    e0_send((const uint8_t *)&status, sizeof status, length);
}

#ifdef ELITE_PERCUSSION_727
#include "percussion_load.h"
/* The 727 pool arrives over USB MIDI (tools/provision_727.py): no ROM mode, no flasher. */
static p727_stage pool_stage;
static int pool_last_rc; /* the driver's own result for the last erase or page write, sent with a commit */
static int pool_read(void *context, uint32_t offset, uint8_t *out, uint32_t n)
{
    (void)context;
    const volatile uint8_t *flash = (const volatile uint8_t *)FL_XIP(offset);
    for (uint32_t i = 0; i < n; i++) out[i] = flash[i];
    return 0;
}
static int pool_erase(void *context, uint32_t offset)
{
    (void)context;
    uint32_t took;
    fm1_wdt_feed();
    int rc = fl_erase4k(offset, &took);
    fm1_wdt_feed();
    pool_last_rc = rc;
    return rc;
}
static int pool_program(void *context, uint32_t offset, const uint8_t *data, uint32_t n)
{
    (void)context;
    int rc = fl_write(offset, data, n);
    if (rc) pool_last_rc = rc;
    return rc;
}
static const p727_io pool_io = {0, pool_read, pool_erase, pool_program};

static void pool_unmount(struct panel *next)
{
    if (!next->layer_available) return;
    uint32_t f = irq_save();
    percussion_init(&launch.drums_b.percussion, 0); /* the renderer skips a layer with no bank */
    irq_restore(f);
    next->layer_available = 0;
}

static unsigned pool_mount(struct panel *next)
{
    if (next->layer_available || safe_mode) return next->layer_available;
    if (!percussion_flash_open(&percussion_assets, fm1_xip_ptr(0), percussion_flash_clips, percussion_flash_sizes,
                               percussion_flash_crc, fm1_wdt_feed))
        return 0;
    percussion_prepare_attacks(&percussion_assets, &percussion_attacks);
    next->layer_available = 1;
    if (next->drums_b.percussion_kit) {
        uint32_t f = irq_save();
        conductor_set_percussion_kit(&launch, &percussion_assets, next->drums_b.percussion_kit - 1u);
        irq_restore(f);
    }
    return 1;
}

static int pool_message(struct panel *next)
{
    const uint8_t *message;
    uint32_t length;
    if (!ota_frame_get(&message, &length) || length < 4 || memcmp(message, "\x7d\x45\x4c", 3) ||
        message[3] < 0x10 || message[3] > 0x14)
        return 0;
    unsigned command = message[3];
    uint8_t reply[320] = {0xf0, 0x7d, 0x45, 0x4c, (uint8_t)(command + 0x40)};
    uint32_t n = 5;
    uint8_t chunk[P727_LOAD_CHUNK];
    if (command == 0x10 && length == 9) {
        uint32_t offset = p727_get21(message + 4), size = message[7] | (uint32_t)message[8] << 7;
        int rc = p727_read(&pool_io, offset, chunk, size);
        p727_put21(reply + n, offset);
        n += 3;
        reply[n++] = (uint8_t)rc;
        if (rc == P727_OK) n += p727_pack7(chunk, size, reply + n);
    } else if (command == 0x11 && length > 8) {
        uint32_t sector = p727_get21(message + 4);
        unsigned part = message[7];
        int rc = p727_unpack7(message + 8, length - 8, chunk, sizeof chunk) == P727_LOAD_CHUNK ?
                 p727_stage_chunk(&pool_stage, sector, part, chunk) : P727_BAD;
        p727_put21(reply + n, sector);
        n += 3;
        reply[n++] = (uint8_t)part;
        reply[n++] = (uint8_t)rc;
    } else if (command == 0x12 && length == 12) {
        uint32_t sector = p727_get21(message + 4), crc = p727_get35(message + 7);
        int busy = safe_mode || next->sound.running || next->sound.clock.state == TG_CLOCK_RUNNING ||
                   bench_result == BENCH_RUNNING;
        if (!busy) pool_unmount(next);
        pool_last_rc = 0;
        int rc = p727_commit(&pool_stage, &pool_io, sector, crc, busy);
        p727_put21(reply + n, sector);
        n += 3;
        reply[n++] = (uint8_t)rc;
        reply[n++] = (uint8_t)((pool_last_rc < 0 ? -pool_last_rc : pool_last_rc) & 127); /* driver detail */
    } else if (command == 0x13 && length == 4) {
        reply[n++] = (uint8_t)pool_mount(next);
    } else if (command == 0x14 && length == 4) {
        reply[n++] = ELITE_BUILD / 100;
        reply[n++] = ELITE_BUILD % 100;
        reply[n++] = next->layer_available;
        reply[n++] = (uint8_t)(next->sound.running || bench_result == BENCH_RUNNING);
        uint32_t shed = launch.drums_b_shed;
        p727_put21(reply + n, shed > 0x1fffffu ? 0x1fffffu : shed); /* 727 blocks skipped under load */
        n += 3;
    } else {
        ota_frame_done();
        return 1; /* ours, but malformed: drop it */
    }
    ota_frame_done();
    reply[n++] = 0xf7;
    ota_wire_send(reply, n);
    return 1;
}
#endif


static void elite_usb_activity_send(uint16_t length)
{
    static elite_usb_activity status;
    uint32_t config = fm1_icfg();
    fm1_irq_off();
    uint64_t keys = fm1_in.keys;
    uint32_t pads = smk_pad_own, sounding = playing_note, blocks = audio_signal_blocks;
    uint32_t wheel_raw = fm1_in.wheel[0] | ((uint32_t)fm1_in.wheel[1] << 16);
    uint32_t wheel_applied = (uint16_t)applied.bend | ((uint32_t)applied.mod << 16);
    fm1_icfg_set(config);
    status = (elite_usb_activity){.magic = ELITE_USB_DIAG_MAGIC, .build = ELITE_BUILD,
        .signal_blocks = blocks, .lcd_strips = lcd_strips, .rgb_frames = pad_frames,
        .lcd_timeouts = fm1_lcd_timeouts, .live_strikes = live_strikes,
        .key_down_low = (uint32_t)keys, .key_down_high = (uint32_t)(keys >> 32),
        .pad_down = pads, .sounding_note = sounding, .recording = midi_step_armed,
        .arp_mode = requested.arp, .wheel_raw = wheel_raw,
        .wheel_center = wheel_ready ? bend_center : 0, .wheel_applied = wheel_applied};
    e0_send((const uint8_t *)&status, sizeof status, length);
}

#ifdef ELITE_DRUMS808
static const struct panel *recall_panel;

static void elite_usb_recall_send(uint16_t length)
{
    static elite_usb_recall status;
    status = (elite_usb_recall){.magic = ELITE_USB_DIAG_MAGIC, .build = ELITE_BUILD, .uptime_ms = fm1_ms,
        .state = ELITE_RECALL_UNAVAILABLE};
    if (recall_panel) library_recall_status(&status, recall_panel);
    e0_send((const uint8_t *)&status, sizeof status, length);
}

static void elite_usb_conductor_live_send(uint16_t length)
{
    static elite_usb_conductor_live status;
    uint32_t config = fm1_icfg();
    fm1_irq_off();
    unsigned usb_clock = applied.clock.source == TG_CLOCK_USB;
    unsigned playing = usb_clock ? applied.clock.state == TG_CLOCK_RUNNING : applied.running;
    uint64_t keys = fm1_in.keys;
    unsigned pending = (launch_requested.drums_cue != drums_applied) |
        ((launch_requested.drums_b.cue != drums_b_applied) << 1) |
        ((launch_requested.dillo_cue != dillo_applied) << 2) |
        ((launch_requested.telegram_cue != telegram_applied) << 3);
    const tg_clock_snapshot *clock = &applied.clock;
    unsigned last_step = (clock->seek + clock->steps - clock->base + DRUM_STEPS - 1u) % DRUM_STEPS;
    status = (elite_usb_conductor_live){
        .magic = ELITE_USB_DIAG_MAGIC, .build = ELITE_BUILD, .uptime_ms = fm1_ms,
        .state = (playing ? ELITE_CONDUCTOR_PLAY : 0) | (usb_clock ? ELITE_CONDUCTOR_USB : 0) |
            (safe_mode || updating ? ELITE_CONDUCTOR_UNAVAILABLE : 0) |
            (bench_result == BENCH_RUNNING ? ELITE_CONDUCTOR_BENCH : 0) |
            (recall_panel && recall_panel->conductor_dillo_keys ? ELITE_LIVE_DILLO_KEYS : 0) |
            (recall_panel && recall_panel->page == PAGE_CONDUCTOR ? ELITE_LIVE_HOME : 0) |
            (midi_owned ? ELITE_LIVE_MIDI : 0) |
            (pending << ELITE_LIVE_PENDING_SHIFT) |
            (usb_clock ? (unsigned)clock->state << ELITE_LIVE_CLOCK_SHIFT |
                         last_step << ELITE_LIVE_STEP_SHIFT |
                         (unsigned)clock->phase << ELITE_LIVE_PHASE_SHIFT |
                         (uint32_t)clock->epoch << ELITE_LIVE_EPOCH_SHIFT : 0),
        .committed_blocks = conductor_commits, .section_launches = section_trace.count,
        .misaligned_launches = section_trace.misaligned_launches, .launch_block = section_trace.block,
        .launch_slots = section_trace.slots, .telegram_offset = section_trace.telegram_offset,
        .rhythm_offset = section_trace.rhythm_offset, .late_usb_step = section_trace.late_usb_step,
        .key_down_low = (uint32_t)keys, .key_down_high = (uint32_t)(keys >> 32),
        .live_notes = conductor_live_notes(), .fault = audio_fault,
    };
    fm1_icfg_set(config);
    e0_send((const uint8_t *)&status, sizeof status, length);
}

static void elite_usb_conductor_kits_send(uint16_t length)
{
    static elite_usb_conductor_kits status;
    uint32_t config = fm1_icfg();
    fm1_irq_off();
    status = (elite_usb_conductor_kits){
        .magic = ELITE_USB_DIAG_MAGIC, .build = ELITE_BUILD, .uptime_ms = fm1_ms,
        .committed_blocks = conductor_commits, .callback_ticks = HALF_TICKS,
        .fault = audio_fault, .lcd_timeouts = fm1_lcd_timeouts, .part = {kit_trace[0], kit_trace[1]},
    };
    fm1_icfg_set(config);
    e0_send((const uint8_t *)&status, sizeof status, length);
}

static void elite_usb_conductor_cues_send(uint16_t length)
{
    static elite_usb_conductor_cues status;
    uint32_t config = fm1_icfg();
    fm1_irq_off();
    status = (elite_usb_conductor_cues){
        .magic = ELITE_USB_DIAG_MAGIC, .build = ELITE_BUILD, .uptime_ms = fm1_ms,
        .committed_blocks = conductor_commits, .fault = audio_fault,
        .active_slots = cue_slots(drums_active, drums_b_active, dillo_active, telegram_active),
        .event = cue_trace,
    };
    fm1_icfg_set(config);
    e0_send((const uint8_t *)&status, sizeof status, length);
}

#ifdef ELITE_RENDER_PROFILE
static void elite_usb_render_profile_send(uint16_t length)
{
    /* The ISR publishes once on the latched deadline fault. */
    if (render_profile.magic)
        e0_send((const uint8_t *)&render_profile, sizeof render_profile, length);
    else {
        static const elite_usb_render_profile empty = {.magic = ELITE_USB_DIAG_MAGIC, .build = ELITE_BUILD};
        e0_send((const uint8_t *)&empty, sizeof empty, length);
    }
}
#endif

static void elite_usb_conductor_send(uint16_t length)
{
    static elite_usb_conductor status;
    uint32_t config = fm1_icfg();
    fm1_irq_off();
    unsigned usb_clock = applied.clock.source == TG_CLOCK_USB;
    unsigned playing = usb_clock ? applied.clock.state == TG_CLOCK_RUNNING : applied.running;
    status = (elite_usb_conductor){
        .magic = ELITE_USB_DIAG_MAGIC, .build = ELITE_BUILD, .uptime_ms = fm1_ms,
        .committed_blocks = conductor_commits, .four_lane_blocks = conductor_four_blocks,
        .telegram_blocks = conductor_lane_blocks[MIX_TELEGRAM], .dillo_blocks = conductor_lane_blocks[MIX_DILLO],
        .drums_a_blocks = conductor_lane_blocks[MIX_808], .drums_b_blocks = conductor_lane_blocks[MIX_808_B],
        .postcopy_peak_ticks = conductor_postcopy_peak, .callback_ticks = HALF_TICKS, .fault = audio_fault,
        .lcd_strips = lcd_strips, .rgb_frames = pad_frames, .lcd_timeouts = fm1_lcd_timeouts,
        .state = (playing ? ELITE_CONDUCTOR_PLAY : 0) | (usb_clock ? ELITE_CONDUCTOR_USB : 0) |
            (dillo_playing ? ELITE_CONDUCTOR_DILLO : 0) | (drums_playing ? ELITE_CONDUCTOR_808_A : 0) |
            (drums_b_playing ? ELITE_CONDUCTOR_808_B : 0) | (bench_result == BENCH_RUNNING ? ELITE_CONDUCTOR_BENCH : 0) |
            (safe_mode || updating ? ELITE_CONDUCTOR_UNAVAILABLE : 0) |
#ifdef ELITE_SINGLE_808
            (bench_result == BENCH_RUNNING && bench_cymbal && launch.drums.repeat_tail &&
             launch.drums.roll.velocity[D8_CY] ? ELITE_CONDUCTOR_BENCH_CYMBAL : 0) |
#endif
#ifdef ELITE_PERCUSSION_727
            (bench_result == BENCH_RUNNING && (bench_options & BENCH_727) && drums_b_playing &&
             launch.active.drums_b.percussion_kit == P727_LATIN + 1u ? ELITE_CONDUCTOR_BENCH_727 : 0) |
#endif
            ((uint32_t)applied.master << ELITE_CONDUCTOR_MASTER_SHIFT),
    };
    fm1_icfg_set(config);
    e0_send((const uint8_t *)&status, sizeof status, length);
}
#endif

static void elite_usb_midi_send(uint16_t length)
{
    static elite_usb_midi status;
    unsigned note = tg_midi_note(&performance_midi, requested.patch.trigger);
    status = (elite_usb_midi){
        .magic = ELITE_USB_DIAG_MAGIC, .build = ELITE_BUILD,
        .accepted = performance_midi.accepted, .ignored = performance_midi.ignored,
        .stream_resets = midi_stream_resets, .held = performance_midi.count,
        .note = note == TG_NOTE_ZERO ? 0 : note, .sustain = performance_midi.sustain,
        .bend = performance_midi.bend, .mod = performance_midi.mod, .owned = midi_owned,
        .queue_depth = mi_w - mi_r, .channel = performance_midi.channel + 1u, .configured = usb.config, .suspended = usb.suspended,
        .all_sound_off = performance_midi.silence,
    };
    e0_send((const uint8_t *)&status, sizeof status, length);
}

static void elite_usb_clock_send(uint16_t length)
{
    static elite_usb_clock status;
    const tg_clock_snapshot *c = &performance_clock.out;
    status = (elite_usb_clock){
        .magic = ELITE_USB_DIAG_MAGIC, .build = ELITE_BUILD, .source = c->source, .state = c->state,
        .epoch = c->epoch, .base = c->base, .steps = c->steps, .seek = c->seek,
        .phase = c->phase, .period_us = c->period_us, .received = performance_clock.received,
        .clocks = performance_clock.clocks, .ignored = performance_clock.ignored,
        .losses = performance_clock.out.losses, .coalesced = performance_clock.coalesced,
    };
    e0_send((const uint8_t *)&status, sizeof status, length);
}

static void elite_usb_boot_send(uint16_t length)
{
    boot_diag.active_checkpoint = elite_ota_trace;
    e0_send((const uint8_t *)&boot_diag, sizeof boot_diag, length);
}

static void main_loop(void)
{
    struct panel next = { .page = HOME_PAGE, .pad_light = 72, .pad_midi_channel = 9, .sound = { .tempo = 118, .master = 44, .steps = STARTUP_STEPS } };
    init_map(&next);
#ifdef ELITE_DRUMS808
    select_preset(&next, 71); /* a fresh unit opens on Roland's 1982 charts: E.Bass Guitar, bank 1982 FACTORY */
#else
    select_preset(&next, 0);
#endif
    uint32_t ui_time = 0, feed_time = 0, pad_time = 0;
    uint8_t last_fault = 0, last_note = 0, last_step = TG_STEPS, shown_fader_caught = 0;
    fl_plain_window_init();
#ifdef ELITE_PERCUSSION_727
    next.layer_available = !safe_mode && percussion_flash_open(&percussion_assets, fm1_xip_ptr(0),
        percussion_flash_clips, percussion_flash_sizes, percussion_flash_crc, fm1_wdt_feed);
    if (next.layer_available) percussion_prepare_attacks(&percussion_assets, &percussion_attacks);
#endif
    library_setup(&next);
#ifdef ELITE_DRUMS808
    recall_panel = &next;
#endif
    fm1_irq_off();
    fm1_input_init();
    smk_trim_load((const uint8_t *)FL_XIP(SMK_TRIM_FLASH));
    smk_pad_map = pad_map;
    lcd_init();
    struct panel shown = next;
    unsigned last_beat = panel_beat(&next);
#ifdef ELITE_DRUMS808
    uint8_t shown_fader_position[4] = {0};
#endif
    show_panel(&next, panel_head(&next), last_beat);
    tg_init(&engine, &next.sound);
#ifdef ELITE_DRUMS808
    conductor_init(&launch);
#ifdef ELITE_PERCUSSION_727
    launch.drums_b_fits = drums_b_fits;
#endif
#ifdef ELITE_PERCUSSION_727
    if (next.layer_available)
        conductor_set_percussion_kit(&launch, &percussion_assets, next.drums_b.percussion_kit - 1u);
#endif
    launch_requested.mixer = conductor_mixer_controls(&next);
    launch_requested.publication = 1;
    launch_requested.dillo_sound = next.dillo_sound;
    launch_requested.kit = next.drums_kit;
    launch_requested.drums_b = next.drums_b;
    launch_requested.drum_init = next.drums_b_initialized << 1;
    launch_requested.has_kit = 1;
    launch_requested.kit_slots = next.kit_slot | next.drums_b_kit_slot << 4;
    committed_kit_slots = launch_requested.kit_slots;
    conductor_prepare_kits(&launch_requested, &launch_requested, launch_kit_buffers);
    conductor_prepare_mix(&mix_requested, &launch_requested.mixer, launch_requested.drums_b.mix);
#elif defined(ELITE_DILLO)
    dillo_layer_init(&dillo);
#endif
    requested = applied = next.sound;
    tg_perform_init(&perform, perform_ring);
    last_ms_tick = fm1_ticks();
    usb_start();
    ota_wire_mark();
    if (!safe_mode)
        fm1_audio_init(dma, HALF_WORDS, isr_alnk0, 3);
    fm1_timer5_start(isr_timer5, 4);
    fm1_guard_lock_top();
    fm1_irq_enable_all();

    for (;;) {
        update_clock();
        if (fm1_ms - feed_time >= 100u) {
            feed_time = fm1_ms;
            fm1_wdt_feed();
        }
        if (guard.pending && !audio_fault && fm1_ms >= 30000u)
            bootguard_settled(&guard);
        elite_usb_retry();
        poll_update_usb();
        if (usb.uboot_req) {
            audio_stop();
            bootguard_intentional(&guard);
            usb_detach();
            fm1_enter_uboot();
        }
        unsigned resume_after_ota = usb.ota_req;
        if (resume_after_ota) {
            usb_reconnect_pending = 0;
            ota_trace(ELITE_OTA_ENTER, 0);
            ota_attempt(&next);
        }
#ifdef ELITE_BAND_PRESETS
        if (!audio_fault && !safe_mode) {
            next.dillo_applied = dillo_applied;
            next.telegram_applied = telegram_applied;
            conductor_finish_studio(&next);
            conductor_finish_band(&next);
        }
#endif
        read_performance(&next, visible_pad_head);
#ifdef ELITE_DRUMS808
        motion_clock(&next, conductor_running(&next) && drums_head < 16u ? drums_head : 16u, fm1_in.frames);
#endif
        live_strikes += (uint8_t)(next.sound.live_trigger - requested.live_trigger);
        {
            unsigned route = next.midi_tx;                 /* GLOBAL > MIDI OUT: OFF, USB, TRS, BOTH */
            if ((route & TG_MIDI_ROUTE_TRS) && !trs_ready) {
                uint32_t icfg = fm1_icfg();
                fm1_irq_off();
                smk_uart1_tx_init();
                trs_ready = 1;
                fm1_icfg_set(icfg);
            }
            /* OFF keeps the last route, so note-offs and Stop leave where the notes went. A change while notes
             * sound closes them on the old route first (All Notes Off per channel); the new one starts clean. */
            unsigned want = route ? route : midi_route;
            if (want != midi_route && midi_sent_channels) {
                for (unsigned ch = 0; ch < MIDI_CHANNELS; ch++)
                    if (midi_sent_channels & (1u << ch)) elite_midi_out(0x0bu | (0xb0u | ch) << 8 | 123u << 16);
                memset(midi_sent, 0, sizeof midi_sent);
                midi_sent_channels = 0;
            }
            midi_route = (uint8_t)want;
            clock_out_on = next.clock_out;
            perform_fx_on = next.perform_fx;
            perform_filters_on = next.perform_filters;
            perform_mutes_on = next.perform_mutes;
#ifdef ELITE_PERCUSSION_727
            for (unsigned t = 0; t < P727_TRACKS; t++) { /* the 727 page's knobs, read by the renderer */
                unsigned g = (unsigned)next.layer_mix[layer_track_group[t]] * next.layer_mix[7] * 128u / 10000u;
                launch.drums_b.percussion.track_gain[t] = (uint8_t)(g > 255u ? 255u : g);
            }
#endif
        }
        midi_send_sideman(&next);
        midi_send_panel(&next);
        bench_tick(&next);
#ifdef ELITE_PERCUSSION_727
        if (!editor_message(&next) && !pool_message(&next) && !bench_message(&next))
#else
        if (!editor_message(&next) && !bench_message(&next))
#endif
            ota_service();
#ifdef ELITE_SINGLE_808
        bench_restore_view(&next);
#endif
#ifdef ELITE_DRUMS808
        conductor_patterns patterns = {
            .dillo = next.dillo_pattern, .drums = next.drums_pattern,
            .dillo_cue = next.dillo_cue, .drums_cue = next.drums_cue, .telegram_cue = next.telegram_cue,
            .telegram_slot = next.telegram_slot, .telegram_library = next.telegram_library, .dillo_live = next.dillo_live,
            .dillo_articulation = next.dillo_articulation,
            .mixer = conductor_mixer_controls(&next), .drum_keys = next.drums_roll,
            .drum_auditions = {next.drum_auditions[0], next.drum_auditions[1]},
            .kit = next.drums_kit, .has_kit = 1, .dillo_sound = next.dillo_sound,
            .drums_b = next.drums_b, .dillo_on = next.dillo_enabled, .drums_on = next.drums_enabled,
            .dillo_slot = next.dillo_slot, .drums_slot = next.drums_slot, .scene = next.scene_queued,
            .drum_init = next.drums_initialized | (next.drums_b_initialized << 1),
            .kit_slots = next.kit_slot | next.drums_b_kit_slot << 4,
            .publication = launch_requested.publication,
        };
        unsigned cancel = next.cancel_cue == CANCEL_CUE_REQUESTED;
        const tg_controls *audio_controls = &next.sound;
#if defined(ELITE_SINGLE_808) || defined(ELITE_BAND_PRESETS)
        tg_controls test_controls;
#endif
#ifdef ELITE_BAND_PRESETS
        if ((next.band_preset || next.studio_dillo_pending || next.studio_telegram_pending) && !cancel) {
            test_controls = next.sound;
            if (next.band_preset) conductor_prepare_band(&next, &patterns, &test_controls);
            else conductor_prepare_studio(&next, &patterns, &test_controls);
            audio_controls = &test_controls;
        }
#endif
#ifdef ELITE_SINGLE_808
        if (bench_result == BENCH_RUNNING && bench_cymbal && audio_controls == &next.sound) {
            test_controls = next.sound;
            bench_cymbal_fixture(&test_controls, &patterns);
            audio_controls = &test_controls;
        }
#endif
        /* MOTION: the recorded movement reaches the voices as a knob turn does, in the published controls. */
        tg_controls motion_controls;
        if (motion_plays(&next)) {
            motion_controls = *audio_controls;
            motion_controls.patch.value[next.motion_target - 1u] = motion.effective;
            audio_controls = &motion_controls;
        }
        conductor_prepare_kits(&patterns, &launch_requested, launch_kit_buffers);
        unsigned canceled = 0;
        if (cancel) conductor_prepare_cancel(&next, &cancel_drafts);
        unsigned publish = memcmp(audio_controls, &requested, sizeof requested) ||
                           memcmp(&patterns, &launch_requested, sizeof patterns);
        conductor_mix_targets next_mix;
        unsigned mix_changed = publish && (memcmp(&patterns.mixer, &launch_requested.mixer, sizeof patterns.mixer) ||
            memcmp(patterns.drums_b.mix, launch_requested.drums_b.mix, MIX_CONTROLS));
        if (mix_changed) conductor_prepare_mix(&next_mix, &patterns.mixer, patterns.drums_b.mix);
        uint32_t launch_irq = fm1_icfg();
        fm1_irq_off();
        next.dillo_applied = dillo_applied;
        next.drums_applied = drums_applied;
        next.telegram_applied = telegram_applied;
        next.telegram_active = telegram_active;
        next.telegram_library_active = telegram_library_active;
        next.dillo_active = dillo_active;
        next.drums_active = drums_active;
        next.dillo_playing = dillo_playing;
        next.drums_playing = drums_playing;
        next.drums_b_applied = drums_b_applied;
        next.drums_b_active = drums_b_active;
        next.drums_b_playing = drums_b_playing;
        /* Acknowledge the published request before replacing it. A new
         * pad strike can arrive in the same foreground pass as the bar. */
        unsigned applied_scene = audio_fault ? 0 : conductor_acknowledge_scene(&next, &launch_requested);
        patterns.scene = next.scene_queued;
        if (cancel && !audio_fault && !safe_mode) {
            canceled = conductor_cancel_pending(&next, &patterns, &launch.active, &engine.controls);
            publish |= canceled != 0;
#ifdef ELITE_BAND_PRESETS
            /* A sound whose phrase committed during this pass will be
             * reconciled before the foreground publishes again. */
            if (next.band_preset || next.studio_dillo_pending || next.studio_telegram_pending) publish = 0;
#endif
        }
        if (publish) {
            /* Zero is reserved for callers without a publication counter. */
            if (++patterns.publication == 0) patterns.publication = 1;
            requested = *audio_controls;
            launch_requested = patterns;
            if (mix_changed) mix_requested = next_mix;
        }
        fm1_icfg_set(launch_irq);
        if (cancel) conductor_finish_cancel(&next, &cancel_drafts, canceled);
        conductor_update_active_scene(&next, applied_scene);
#else
        if (memcmp(&next.sound, &requested, sizeof requested)
#ifdef ELITE_DILLO
            || dillo_requested != next.dillo_enabled
            || memcmp(&dillo_requested_pattern, &next.dillo_pattern, sizeof dillo_requested_pattern)
#endif
        ) {
            fm1_irq_off();
            requested = next.sound;
#ifdef ELITE_DILLO
            dillo_requested = next.dillo_enabled;
            dillo_requested_pattern = next.dillo_pattern;
#endif
            fm1_irq_on();
        }
#endif
        elite_usb_service_reconnect();
        if (resume_after_ota) {
            ota_trace(ELITE_OTA_RESTART_AUDIO, elite_ota_flags >> ELITE_USB_OTA_STAGE_SHIFT);
            audio_restart();
            redraw_view = 1; /* OTA text bypassed the strip cache. */
            elite_ota_trace = 0;
        }
        library_service(&next);
        if (resume_after_ota || (next_strip >= VIEW_H && fm1_ms - ui_time >= 45u)) {
            unsigned head = panel_head(&next);
            unsigned strip_expired = 0, focus_motion = 0, pickup_moved = 0, scope_motion = 0;
#ifdef ELITE_DRUMS808
            focus_motion = focus_animation_due(&next, shown.view_frames);
            /* The play page's waveform moves on its own: redraw only its plot when nothing covers it. */
            scope_motion = next.page == PAGE_TONE && shown.page == PAGE_TONE && !next.key_transpose_armed && !next.perform_view &&
                !audio_fault && !safe_mode && !view_globe_map_visible(&next) &&
                !(next.strip_knob && (uint32_t)(next.view_frames - next.strip_frame) < CONTROL_FOCUS_FRAMES);
            if (next.page == PAGE_MANUAL || next.page == PAGE_FILTER_ENV || next.page == PAGE_MIX)
                for (unsigned f = 0; f < 4; f++)
                    pickup_moved |= !(fader_caught & (1u << f)) &&
                        view_fader_offset(last_fader[f]) != view_fader_offset(shown_fader_position[f]);
            /* Ignore the scan counter in state equality. The timed overlay
             * requests only the stage strips that its animation changes. */
            strip_expired = shown.strip_knob &&
                (uint32_t)(shown.view_frames - shown.strip_frame) < CONTROL_FOCUS_FRAMES &&
                (uint32_t)(next.view_frames - shown.strip_frame) >= CONTROL_FOCUS_FRAMES;
            unsigned panel_changed = display_panel_changed(&shown, &next);
#else
            unsigned panel_changed = memcmp(&next, &shown, sizeof next) != 0;
#endif
            unsigned pad_motion = !audio_fault && !safe_mode && view_motion_due(&next, shown.view_frames);
            unsigned changed = resume_after_ota || strip_expired || pickup_moved || panel_changed || pad_motion ||
                               playing_note != last_note || fader_caught != shown_fader_caught || audio_fault != last_fault;
            unsigned beat = panel_beat(&next);
            unsigned head_changed = head != last_step, beat_changed = beat != last_beat;
            if (changed || head_changed || beat_changed || focus_motion || scope_motion) {
                ui_time = fm1_ms;
                last_step = (uint8_t)head;
                last_beat = beat;
#ifdef ELITE_DRUMS808
                unsigned bar_only = next.page == PAGE_CONDUCTOR && !next.draft_view &&
                    !next.conductor_seq_pending && !next.drums_repeat && !next.key_transpose_armed;
                if (!changed && !redraw_view && scope_motion && !focus_motion && !head_changed && !beat_changed) {
                    view_tel_play_plot(view_frame, &shown, playing_note);
                    view_dillo_wallpaper(view_frame, view_livery(&shown), shown.main_notice == 3);
                    next_strip = VIEW_TEL_PLOT_Y / VIEW_STRIP * VIEW_STRIP;
                } else if (!changed && !redraw_view && focus_motion) {
                    view_focus(view_frame, &next);
                    view_dillo_wallpaper(view_frame, view_livery(&next), next.main_notice == 3);
                    if (beat_changed) view_header_motion(view_frame, &next, beat);
                    next_strip = beat_changed ? 0 : FOCUS_Y / VIEW_STRIP * VIEW_STRIP;
                } else if (!changed && !redraw_view && !audio_fault && !safe_mode && (!head_changed || bar_only)) {
                    if (head_changed) view_conductor_bar(view_frame, &next, head);
                    if (beat_changed) {
                        view_header_motion(view_frame, &next, beat);
                        if (bar_only) view_conductor_cue_motion(view_frame, &next, beat);
                    }
                    next_strip = beat_changed ? 0 : CONDUCTOR_BAR_STRIP;
                } else
#endif
                {
                    shown = next;
                    last_fault = audio_fault;
                    last_note = playing_note;
                    shown_fader_caught = fader_caught;
#ifdef ELITE_DRUMS808
                    memcpy(shown_fader_position, last_fader, sizeof shown_fader_position);
#endif
                    fm1_led_key(BTN_PLAY, next.sound.running && !safe_mode && !audio_fault);
#ifdef ELITE_DILLO
                    fm1_led_key(BTN_REC, next.record || (next.page == PAGE_DILLO && next.dillo_record));
#else
                    fm1_led_key(BTN_REC, next.record);
#endif
                    fm1_led_key(BTN_ARP, next.sound.arp != TG_ARP_OFF);
                    fm1_led_key(BTN_TIE, next.hold && !safe_mode && !audio_fault);
                    show_panel(&next, head, beat);
                }
            }
        }
        if (!display_bus_ready()) continue;
        if (fm1_ms - pad_time >= 12u) {
            pad_time = fm1_ms;
            refresh_pads(&next);
        }
        service_display();
    }
}

void fm1_cstart(void)
{
    fm1_time_init();
    fm1_reset_reason();
    uint32_t reason = bootguard_reason(fm1_boot.p3_rst, fm1_boot.rst_src);
    fm1_wdt_arm(0x0d);
    uint32_t mode = bootguard_step(&guard, reason);
    if (mode == BOOT_UBOOT)
        fm1_enter_uboot();
    fm1_irq_init();
    for (uint32_t *d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (uint32_t *d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (uint32_t *s = _data_load, *d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (uint32_t *s = _rt_load, *d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;
    boot_diag = (elite_usb_boot){
        .magic = ELITE_USB_DIAG_MAGIC, .build = ELITE_BUILD,
        .p3_rst = fm1_boot.p3_rst, .rst_src = fm1_boot.rst_src, .wdt_con = fm1_boot.wdt_con,
        .boot_mode = mode,
        .prior_checkpoint = elite_ota_previous(elite_ota_trace, guard.cls == BG_RST_POWERON),
    };
    elite_ota_trace = 0;
    if ((boot_diag.prior_checkpoint & ELITE_OTA_TRACE_FAULT) && fm1_crash.magic == FM1_CRASH_MAGIC) {
        boot_diag.fault_vector = fm1_crash.vec;
        boot_diag.fault_pc = fm1_crash.pc;
        boot_diag.fault_sp = fm1_crash.sp;
        boot_diag.fault_emu = fm1_crash.emu;
        boot_diag.fault_dbg = fm1_crash.dbg;
        boot_diag.fault_uptime_ms = fm1_crash.uptime_ms;
    }
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    safe_mode = mode == BOOT_SAFE;
    main_loop();
}
