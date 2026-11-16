/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ELITE_USB_DIAG_H
#define ELITE_USB_DIAG_H

#include <stdint.h>

#define ELITE_USB_PRODUCT "Telegram 101"
#define ELITE_USB_LEGACY_PRODUCT "ChoralRoot FM-1"

#define ELITE_USB_DIAG_REQUEST 0xd4
#define ELITE_USB_DIAG_VALUE 0x454c
#define ELITE_USB_DIAG_SCHEMA 1
#define ELITE_USB_AUDIO_SCHEMA 2
#define ELITE_USB_MIDI_SCHEMA 3
#define ELITE_USB_CLOCK_SCHEMA 4
#define ELITE_USB_BOOT_SCHEMA 5
#define ELITE_USB_ACTIVITY_SCHEMA 6
#define ELITE_USB_CONDUCTOR_SCHEMA 7
#define ELITE_USB_RECALL_SCHEMA 8
#define ELITE_USB_CONDUCTOR_LIVE_SCHEMA 9
#define ELITE_USB_CONDUCTOR_KITS_SCHEMA 10
#define ELITE_USB_RENDER_PROFILE_SCHEMA 11
#define ELITE_USB_CONDUCTOR_CUES_SCHEMA 12
#define ELITE_USB_RECONNECT_REQUEST 0xd5
#define ELITE_USB_RECONNECT_MIN_BUILD 543
/* High bits in the schema 1 flags word are available from build 510. */
#define ELITE_USB_OTA_ACTIVE (1u << 11)
#define ELITE_USB_OTA_STAGE_SHIFT 12
#define ELITE_USB_OTA_ERROR_SHIFT 16

#define ELITE_USB_DIAG_MAGIC 0x4c454242u /* BBEL in little endian */

typedef struct {
    uint32_t magic, build, rx, tx, acknowledged, resets, setups, flags;
    uint32_t frame_bytes, last_rx_bytes, sysex_pos, midi_queue_depth;
    uint8_t rx_header[16];
} elite_usb_diag;

_Static_assert(sizeof(elite_usb_diag) == 64, "USB diagnostics must fit in one control packet");

typedef struct {
    uint32_t magic, build, irq_ticks, mix_ticks, commit_ticks;
    uint32_t control_ticks, render_ticks, scan_ticks, pad_hits;
    uint32_t fault, master, playing, tempo, bench, tx_csr, tx_queued;
} elite_usb_audio;

_Static_assert(sizeof(elite_usb_audio) == 64, "Audio diagnostics must fit in one control packet");

typedef struct {
    uint32_t magic, build, accepted, ignored, stream_resets, held, note, sustain;
    int32_t bend;
    uint32_t mod, owned, queue_depth, channel, configured, suspended, all_sound_off; /* Last word is zero before build 491. */
} elite_usb_midi;
_Static_assert(sizeof(elite_usb_midi) == 64, "MIDI diagnostics must fit in one control packet");

typedef struct {
    uint32_t magic, build, source, state, epoch, base, steps, seek;
    uint32_t phase, period_us, received, clocks, ignored, losses, coalesced, reserved;
} elite_usb_clock;
_Static_assert(sizeof(elite_usb_clock) == 64, "Clock diagnostics must fit one control packet");

typedef struct {
    uint32_t magic, build, p3_rst, rst_src, wdt_con, boot_mode;
    uint32_t prior_checkpoint, active_checkpoint;
    uint32_t fault_vector, fault_pc, fault_sp, fault_emu, fault_dbg, fault_uptime_ms;
    uint32_t reserved[2];
} elite_usb_boot;

_Static_assert(sizeof(elite_usb_boot) == 64, "Boot diagnostics must fit in one control packet");

typedef struct {
    uint32_t magic, build, signal_blocks, lcd_strips, rgb_frames, lcd_timeouts, live_strikes;
    uint32_t key_down_low, key_down_high; /* Physical matrix state, from build 562. */
    uint32_t pad_down, sounding_note, recording, arp_mode; /* Bench state, from build 569. */
    /* Build 573 packs pitch/modulation into low/high halves. Bend is signed. */
    uint32_t wheel_raw, wheel_center, wheel_applied;
} elite_usb_activity;

_Static_assert(sizeof(elite_usb_activity) == 64, "Activity diagnostics must fit in one control packet");

/* A coherent sample of successfully committed Conductor audio blocks. */
enum {
    ELITE_CONDUCTOR_PLAY = 1u << 0, ELITE_CONDUCTOR_USB = 1u << 1,
    ELITE_CONDUCTOR_DILLO = 1u << 2, ELITE_CONDUCTOR_808_A = 1u << 3,
    ELITE_CONDUCTOR_808_B = 1u << 4, ELITE_CONDUCTOR_BENCH = 1u << 5,
    ELITE_CONDUCTOR_UNAVAILABLE = 1u << 6, ELITE_CONDUCTOR_BENCH_CYMBAL = 1u << 7,
    ELITE_CONDUCTOR_MASTER_SHIFT = 8, ELITE_CONDUCTOR_BENCH_727 = 1u << 15
};

typedef struct {
    uint32_t magic, build, uptime_ms, committed_blocks, four_lane_blocks;
    uint32_t telegram_blocks, dillo_blocks, drums_a_blocks, drums_b_blocks;
    uint32_t postcopy_peak_ticks, callback_ticks, fault;
    uint32_t lcd_strips, rgb_frames, lcd_timeouts, state;
} elite_usb_conductor;
_Static_assert(sizeof(elite_usb_conductor) == 64, "Conductor diagnostics must fit in one control packet");

/* Read-only evidence for joint bar launches and physical key ownership. */
enum {
    ELITE_LIVE_DILLO_KEYS = 1u << 8, ELITE_LIVE_MIDI = 1u << 9, ELITE_LIVE_HOME = 1u << 10,
    /* Build 641 adds the audio clock and pending pattern mask to the same snapshot. */
    ELITE_LIVE_CLOCK_SHIFT = 11, ELITE_LIVE_PENDING_SHIFT = 13,
    ELITE_LIVE_STEP_SHIFT = 17, ELITE_LIVE_PHASE_SHIFT = 21, ELITE_LIVE_EPOCH_SHIFT = 24,
};
typedef struct {
    uint32_t magic, build, uptime_ms, state, committed_blocks;
    uint32_t section_launches, misaligned_launches, launch_block, launch_slots;
    uint32_t telegram_offset, rhythm_offset, late_usb_step;
    uint32_t key_down_low, key_down_high, live_notes, fault;
} elite_usb_conductor_live;
_Static_assert(sizeof(elite_usb_conductor_live) == 64, "Conductor live diagnostics must fit in one control packet");

enum { ELITE_CUE_USB = 1u << 4 }; /* Signal mask occupies the low four bits. */
typedef struct {
    uint32_t count, block, lanes, before_slots, after_slots;
    uint32_t telegram_offset, rhythm_offset, late_usb_step, context, live_notes;
} elite_usb_cue_commit;

typedef struct {
    uint32_t magic, build, uptime_ms, committed_blocks, fault, active_slots;
    elite_usb_cue_commit event;
} elite_usb_conductor_cues;
_Static_assert(sizeof(elite_usb_conductor_cues) == 64, "Cue diagnostics must fit in one control packet");

enum {
    ELITE_KIT_ALIGNED_LAUNCH = 1u << 4, ELITE_KIT_USB = 1u << 5, ELITE_KIT_PLAY = 1u << 6,
    ELITE_KIT_SLOT_SHIFT = 8, ELITE_KIT_TELEGRAM_SHIFT = 16, ELITE_KIT_DILLO_SHIFT = 24
};
typedef struct {
    uint32_t count, block, ticks, context; /* Signal mask in the low four bits. */
} elite_usb_kit_commit;

typedef struct {
    uint32_t magic, build, uptime_ms, committed_blocks, callback_ticks, fault, lcd_timeouts, reserved;
    elite_usb_kit_commit part[2];
} elite_usb_conductor_kits;
_Static_assert(sizeof(elite_usb_conductor_kits) == 64, "Kit diagnostics must fit in one control packet");

enum {
    ELITE_RECALL_VERIFIED = 1u << 0, ELITE_RECALL_BUSY = 1u << 1, ELITE_RECALL_DIRTY = 1u << 2,
    ELITE_RECALL_UNAVAILABLE = 1u << 3, ELITE_RECALL_SCENE = 1u << 4,
    ELITE_RECALL_ARRANGEMENT = 1u << 5, ELITE_RECALL_STARTUP_DIRTY = 1u << 6
};

typedef struct {
    uint32_t magic, build, uptime_ms, state;
    uint32_t journal_generation, library_crc, tail_hash;
    uint32_t routing_hash, drums_a_hash, drums_b_hash, dillo_hash, telegram_phrase_hash;
    uint32_t voices_hash, mixer_hash, telegram_sound_hash, scene;
} elite_usb_recall;
_Static_assert(sizeof(elite_usb_recall) == 64, "Recall diagnostics must fit in one control packet");

/* A single over-budget callback. Scan time overlaps the stage durations. */
typedef struct {
    uint32_t magic, build, committed_blocks, elapsed_ticks, fault, rhythm_step;
    uint32_t telegram_ticks, dillo_ticks, drums_a_ticks, drums_b_ticks, mixer_ticks;
    uint32_t control_ticks, other_ticks, scan_ticks, callback_ticks, guard_ticks;
} elite_usb_render_profile;
_Static_assert(sizeof(elite_usb_render_profile) == 64, "Render profile must fit in one control packet");

#endif
