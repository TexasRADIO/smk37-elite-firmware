/* SPDX-License-Identifier: GPL-3.0-only */
/* Foreground owns panel edits. Only a complete snapshot crosses to audio. */
#include "../synth/store101.h"
#include "../synth/catalog101.h"
#ifdef ELITE_DILLO
#include "../dillo/layer303.h"
#endif
#ifdef ELITE_DRUMS808
#include "../conductor/launch.h"
typedef struct {
    uint16_t pattern[MIX_AUDIO_LANES];
    uint8_t kits, sounds;
} conductor_assets;
#endif
enum panel_page { PAGE_TONE, PAGE_STEPS, PAGE_PATCH, PAGE_MANUAL, PAGE_ASSIGN, PAGE_FX, PAGE_GLOBAL, PAGE_FILTER_ENV, PAGE_ARP, PAGE_DILLO, PAGE_DRUMS, PAGE_CONDUCTOR, PAGE_MIX, PAGE_SCENES, PAGE_KITS, PAGE_DILLO_SOUND, PAGE_DRUM_PRESETS, PAGE_INSTRUMENTS, PAGE_SYNTH_PRESETS };
enum { LIB_FACTORY, LIB_SOUNDS, LIB_PHRASES, LIB_IMPORTED };
enum { LIB_NORMAL, LIB_EMPTY, LIB_SESSION, LIB_SAVED, LIB_FAILED,
       LIB_UNAVAILABLE, LIB_WAIT_STOP, LIB_WAIT_QUIET };
enum { SAVE_NONE, SAVE_PICK, SAVE_CONFIRM };
enum { UNDO_NONE, UNDO_SHIFT, UNDO_PITCH, UNDO_LENGTH, UNDO_STEP, UNDO_GATE, UNDO_NEW };
enum { REC_OFF, REC_EDIT, REC_NEW };
enum { DILLO_REC_OFF, DILLO_REC_DIRECT, DILLO_REC_PITCH, DILLO_REC_TIME };
enum { PEDAL_OFF, PEDAL_LOW, PEDAL_HIGH };
enum { MIDI_STEP_ENTRY = 1, MIDI_STEP_TIE = 2 };
static tg_library user_library;
static tg_layout profile_drafts[TG_LAYOUT_PROFILES];
static uint8_t profile_draft_slots, fader_caught;
enum panel_button {
    BTN_ARP = 6, BTN_REST = 7, BTN_SCALE = 8, BTN_TIE = 9, BTN_GLOBE = 10,
    BTN_ASSIGN = 11, BTN_PATCH = 12, BTN_PARA = 13, BTN_FX = 14,
    BTN_SEQ = 15, BTN_PLAY = 16, BTN_REC = 17
};

struct patch_snapshot {
    tg_patch patch;
    uint16_t imported_slot;
    uint8_t preset, user_slot, user_sound, imported_sound, scene_sound;
#ifdef ELITE_DRUMS808
    uint8_t startup_sound;
#endif
#ifdef ELITE_BAND_PRESETS
    uint16_t band_sound, studio_sound;
#endif
};

struct panel {
    tg_controls sound;
    struct patch_snapshot alternate_patch;
    uint8_t compare_available, compare_active;
    uint8_t page, preset, cursor, record, knob_bank, fader_bank, phrase_undo;
    uint8_t undo_steps[TG_STEPS], undo_length, undo_cursor, undo_gate;
    /* Last recorded key: 1..TG_KEYS for the keybed, TG_KEYS + pitch for MIDI. */
    uint8_t midi_step_entry, record_key;
    uint8_t audition_step, audition_velocity, audition_strike; /* Step index plus one. */
    uint8_t preset_bank, pad_light, hold, clock_toggle;
    uint8_t knob_map[2][8], fader_map[4][4], assign_slot, assign_source;
    uint8_t pad_note[TG_PADS], pad_keys, pad_lead;
    uint16_t pads_down;
    uint8_t repeat_on, repeat_latch, repeat_latched, repeat_gesture, repeat_edit;
    int8_t octave, key_transpose;
    uint8_t key_transpose_armed, globe_tap_pending, arp_tap_pending;
    uint8_t scale_root, scale_type, arp_latch, midi_channel, pad_midi_channel, midi_tx, profile;
    uint8_t library_kind, save_mode, library_status, imported_sound, import_category;
    uint8_t patch_bank_pending, seq_exit_pending;
    uint16_t library_slot, imported_slot;
    uint8_t user_sound, user_slot, name_cursor, store_request, library_dirty, scene_sound;
    uint8_t pedal_mode, pedal_down;
#ifdef ELITE_DILLO
    dillo_phrase dillo_pattern;
    uint8_t dillo_enabled, dillo_cursor, dillo_record, dillo_undo;
    uint8_t dillo_undo_cursor, dillo_undo_length;
    dillo_step dillo_undo_step;
    dillo_phrase dillo_undo_take;
    uint8_t dillo_pitches[DILLO_STEPS], dillo_pitch_count, dillo_pitch_replace, dillo_time_count, dillo_used;
    uint8_t dillo_take_flags, dillo_take_gate, dillo_take_error;
    uint8_t dillo_time_edit, dillo_time_pick;
    dillo_step dillo_time[DILLO_STEPS];
#endif
#ifdef ELITE_DRUMS808
    uint8_t startup_sound;
    drum_pattern drums_pattern;
    drum_roll drums_roll;
    drum_audition drum_auditions[2];
    drum_kit drums_kit;
    conductor_drum_part drums_b;
    uint32_t drums_b_applied;
    uint16_t drums_b_slots;
    uint8_t drums_focus, drums_b_active, drums_b_playing, drums_b_track;
    uint8_t drums_b_initialized, drums_b_kit_slot;
#ifdef ELITE_PERCUSSION_727
    uint8_t layer_available;
#endif
    dillo_sound dillo_sound;
    uint8_t dillo_sound_slot, dillo_sound_slots, dillo_sound_status, dillo_sound_page;
    uint8_t dillo_sound_home, dillo_sound_bank, dillo_articulation, dillo_audition;
    uint8_t dillo_entry_key, dillo_entry_pitch, dillo_entry_velocity, dillo_pitch_paused;
    uint8_t kit_slot, kit_slots, kit_status, drum_preset_bank, instruments_return, band_bank;
    uint8_t band_bottom; /* Session launch layout, independent of keybed focus. */
#ifdef ELITE_SYNTH_PRESETS
    uint16_t synth_preset_selected; /* One-based index of the last selection on this visit. */
    uint8_t synth_preset_return, synth_preset_dillo, synth_preset_group, synth_preset_page;
    uint8_t synth_duet_selected; /* One-based duet index of the last pad on Acid Duets. */
#endif
#ifdef ELITE_BAND_PRESETS
    uint16_t band_preset, band_sound; /* One-based pending row and current sound. */
    uint16_t studio_dillo_pending, studio_telegram_pending, studio_sound; /* One-based recipe rows. */
    uint8_t dillo_sound_cue; /* One-based Dillo slot an acid pattern brings with it, at its bar. */
    drum_kit band_kit;
#endif
    uint8_t drums_repeat, drums_voice_pick, drum_latch, drums_restore, drum_clear_pending;
    uint8_t drums_undo, drums_undo_track[2];
    uint8_t drums_enabled, drums_initialized, drums_track;
    uint16_t dillo_slots, drums_slots, telegram_slots;
    uint32_t dillo_cue, drums_cue, telegram_cue, dillo_applied, drums_applied, telegram_applied;
    uint8_t conductor_bank, conductor_lane, conductor_seq_pending, conductor_telegram;
    uint8_t telegram_slot, telegram_active, conductor_dillo_keys;
    uint8_t key_focus_notice, focus_chord;
    uint8_t telegram_library, telegram_library_active, telegram_draft_library; /* Library slot plus one. */
    dillo_keys dillo_live;
    conductor_mixer mixer;
    uint8_t mixer_lane, scene_slots, scene_cursor, scene_status, scene_queued, scene_active;
    uint32_t held_buttons, view_frames, strip_frame; /* Redesign strip: modifiers held, last knob turned. */
    uint32_t globe_frame; /* when GLOBE went down: a long hold draws the shift map */
    uint8_t strip_knob;
    uint8_t scene_cleanup;
    conductor_assets scene_cleanup_assets;
    uint32_t conductor_edits, conductor_save_edits;
    uint8_t conductor_rec_pending, conductor_dirty, conductor_save_state, conductor_copy_needed;
    uint8_t conductor_startup_dirty;
    uint8_t dillo_slot, drums_slot, dillo_active, drums_active, dillo_playing, drums_playing;
    uint8_t draft_view, draft_slot[MIX_AUDIO_LANES]; /* Source slot plus one. */
    uint8_t library_open, library_return, library_shelf, library_at; /* LIBRARY on PATCH */
    uint8_t rhythm_source; /* RHYTHM_STUDIO, _LAYER or _BAND on the rhythm page */
    uint8_t rhythm_cursor, rhythm_info; /* Jukebox: selected pad on the page; 0 list, 1 about, 2 how to play */
    uint8_t synth_cursor, synth_info; /* The same for the 303 and 101 pattern and duet shelves */
    uint8_t pad_hit; uint32_t pad_hit_frame; /* Last pad struck, plus one, and when: the screen answers it */
    uint8_t cancel_cue;
#endif
    char save_name[TG_NAME_BYTES];
    uint8_t focus_control, focus_slot, focus_pickup, focus_position, fx_return, arp_focus;
};

#ifdef ELITE_DRUMS808
/* The 808 part every editor shows. With one active 808 it is always A; the
 * saved focus bit is left as loaded so existing setups encode unchanged. */
static unsigned drum_part(const struct panel *panel)
{
    return ELITE_DRUM_PARTS > 1 && panel->drums_focus;
}
#endif

#define STARTUP_STEPS {36, 0, 48, 36 | TG_TIE, 43, 0, 36, 0, 36, 48, 0, 43, 39, 0, 46, 0}

#ifdef ELITE_DRUMS808
enum { HOME_PAGE = PAGE_CONDUCTOR };
enum { EDITOR_LAST = 1, EDITOR_A, EDITOR_B, EDITOR_DILLO, EDITOR_TELEGRAM };
enum { FOCUS_CHORD_KEYS = 1, FOCUS_CHORD_DRUMS };
enum { DRUM_LATCH_A = 1, DRUM_LATCH_B = 2, DRUM_LATCH_BOTH = DRUM_LATCH_A | DRUM_LATCH_B };
enum { DRUM_REPEAT_HOLD_FRAMES = 250, KEY_FOCUS_NOTICE_FRAMES = 500 }; /* 400 and 800 ms. */
enum { CANCEL_CUE_NONE, CANCEL_CUE_REQUESTED, CANCEL_CUE_DONE, CANCEL_CUE_EMPTY };
static uint32_t drum_repeat_frame;
static uint32_t key_focus_frame;
/* Opening a synth's screen hands it the keys once every key is up, so a held note is never cut. */
static uint8_t screen_focus_pending; /* 0 none, 1 Telegram, 2 Dillo */
#ifdef ELITE_BUILD
#define CONDUCTOR_POOL __attribute__((section(".pool"), aligned(4)))
#else
#define CONDUCTOR_POOL
#endif
static dillo_phrase conductor_dillo_slots[CONDUCTOR_SLOTS] CONDUCTOR_POOL;
static drum_pattern conductor_drum_slots[CONDUCTOR_SLOTS] CONDUCTOR_POOL;
static drum_pattern conductor_drum_b_slots[CONDUCTOR_SLOTS] CONDUCTOR_POOL;
static drum_pattern conductor_drum_backup[2] CONDUCTOR_POOL;
static tg_saved_phrase conductor_telegram_slots[CONDUCTOR_SLOTS] CONDUCTOR_POOL;
typedef union {
    drum_pattern drum;
    dillo_phrase dillo;
    tg_saved_phrase telegram;
} conductor_draft;
static conductor_draft conductor_drafts[MIX_AUDIO_LANES] CONDUCTOR_POOL;
/* Physical lane order. MIX_* indices still identify DSP and journal fields.
 * With one 808 the Band shows the 808, Dillo and Telegram; B stays last and hidden. */
#if ELITE_DRUM_PARTS > 1
static const uint8_t conductor_lane_order[MIX_AUDIO_LANES] = {MIX_808, MIX_808_B, MIX_DILLO, MIX_TELEGRAM};
#else
static const uint8_t conductor_lane_order[MIX_AUDIO_LANES] = {MIX_808, MIX_DILLO, MIX_TELEGRAM, MIX_808_B};
#endif
enum { BAND_LANES = ELITE_DRUM_PARTS + 2 }; /* lanes the Band, mixer and drafts show */

static unsigned conductor_lane_position(unsigned lane)
{
    for (unsigned i = 0; i < MIX_AUDIO_LANES; i++)
        if (conductor_lane_order[i] == lane) return i;
    return 0;
}

static unsigned drum_roll_active(const drum_roll *roll)
{
    for (unsigned pad = 0; pad < D8_NUM; pad++)
        if (roll->velocity[pad]) return 1;
    return 0;
}

static void clear_drum_rolls(struct panel *panel, unsigned parts)
{
    if (parts & DRUM_LATCH_A)
        memset(panel->drums_roll.velocity, 0, sizeof panel->drums_roll.velocity);
    if (parts & DRUM_LATCH_B) {
        memset(panel->drums_b.roll.velocity, 0, sizeof panel->drums_b.roll.velocity);
        panel->drums_b.repeat = 0;
    }
}

static void conductor_changed(struct panel *panel)
{
    panel->conductor_edits++;
    panel->conductor_dirty = 1;
}

static void conductor_focus_keys(struct panel *panel, unsigned dillo, uint32_t frames)
{
    screen_focus_pending = 0; /* An explicit choice wins over a pending screen hand-off. */
    if (panel->conductor_dillo_keys == dillo) return;
    panel->conductor_dillo_keys = (uint8_t)dillo;
    if (dillo) {
        panel->record = panel->record_key = 0;
        panel->dillo_pitch_paused = 0;
    } else if (panel->dillo_record == DILLO_REC_PITCH || panel->dillo_record == DILLO_REC_DIRECT) {
        panel->dillo_pitch_paused = 1;
    }
    panel->key_focus_notice = 1;
    key_focus_frame = frames;
    conductor_changed(panel);
}

static void screen_focus_keys(struct panel *panel, unsigned dillo, uint32_t frames)
{
    screen_focus_pending = 0;
    if (fm1_in.keys) screen_focus_pending = (uint8_t)(dillo + 1u);
    else conductor_focus_keys(panel, dillo, frames);
}

static void conductor_forget_drum_undo(struct panel *panel, unsigned part)
{
    uint8_t keep = (uint8_t)~(1u << part);
    panel->drums_restore &= keep;
    panel->drums_undo &= keep;
}

/* One backup per part. A clear retains its full restore through later edits. */
static void conductor_drum_reset(struct panel *panel, unsigned restore)
{
    static const drum_pattern blank;
    unsigned focus = drum_part(panel);
    unsigned bit = 1u << focus;
    drum_pattern *pattern = focus ? &panel->drums_b.pattern : &panel->drums_pattern;
    if (restore) {
        if (!((panel->drums_restore | panel->drums_undo) & bit)) return;
        *pattern = conductor_drum_backup[focus];
        if (panel->drums_undo & bit) {
            uint8_t *track = focus ? &panel->drums_b_track : &panel->drums_track;
            *track = panel->drums_undo_track[focus];
        }
        conductor_forget_drum_undo(panel, focus);
    } else {
        if (!memcmp(pattern, &blank, sizeof blank)) return;
        conductor_drum_backup[focus] = *pattern;
        *pattern = blank;
        panel->drums_restore |= (uint8_t)bit;
        panel->drums_undo &= (uint8_t)~bit;
    }
    conductor_changed(panel);
}

static const tg_saved_phrase conductor_start_phrase = {.steps = STARTUP_STEPS, .length = TG_PADS};

static const drum_pattern *conductor_a_pattern(const struct panel *panel, unsigned slot)
{
    return panel->drums_slots & (1u << slot) ? &conductor_drum_slots[slot] : &drum_demo;
}

static const drum_pattern *conductor_b_pattern(const struct panel *panel, unsigned slot)
{
    if (panel->drums_b_slots & (1u << slot)) return &conductor_drum_b_slots[slot];
#ifdef ELITE_PERCUSSION_727
    if (panel->drums_b.percussion_kit) {
        static const drum_pattern empty;
        return &empty;
    }
#endif
    return &drum_demo_b;
}

static const dillo_phrase *conductor_dillo_pattern(const struct panel *panel, unsigned slot)
{
    return panel->dillo_slots & (1u << slot) ? &conductor_dillo_slots[slot] : &dillo_demo;
}

static const tg_saved_phrase *conductor_telegram_pattern(const struct panel *panel, unsigned slot)
{
    return panel->telegram_slots & (1u << slot) ? &conductor_telegram_slots[slot] : &conductor_start_phrase;
}

static unsigned conductor_phrase_matches(const struct panel *panel, const tg_saved_phrase *phrase)
{
    return tg_pattern_length(&panel->sound) == phrase->length && panel->sound.phrase_gate == phrase->gate &&
           !memcmp(panel->sound.steps, phrase->steps, sizeof phrase->steps);
}

static unsigned conductor_pattern_differs(const struct panel *panel, unsigned lane, unsigned slot)
{
    switch (lane) {
    case MIX_808:
        return panel->drums_initialized &&
               memcmp(&panel->drums_pattern, conductor_a_pattern(panel, slot), sizeof(drum_pattern));
    case MIX_808_B:
        return panel->drums_b_initialized &&
               memcmp(&panel->drums_b.pattern, conductor_b_pattern(panel, slot), sizeof(drum_pattern));
    case MIX_DILLO:
        return panel->dillo_pattern.length &&
               memcmp(&panel->dillo_pattern, conductor_dillo_pattern(panel, slot), sizeof(dillo_phrase));
    }
    return !conductor_phrase_matches(panel, conductor_telegram_pattern(panel, slot));
}

static unsigned conductor_pattern_edited(const struct panel *panel, unsigned lane)
{
    const uint8_t slots[] = {[MIX_808] = panel->drums_slot, [MIX_808_B] = panel->drums_b.slot,
        [MIX_DILLO] = panel->dillo_slot, [MIX_TELEGRAM] = panel->telegram_slot};
    return conductor_pattern_differs(panel, lane, slots[lane]);
}

static unsigned conductor_unstored_pattern(const struct panel *panel, unsigned setup)
{
    for (unsigned i = 0; i < MIX_AUDIO_LANES; i++) {
        unsigned lane = conductor_lane_order[i];
        /* A hidden 808 B keeps its working pattern in the startup save as it is. */
        if (((setup && ELITE_DRUM_PARTS > 1) || lane != MIX_808_B) && conductor_pattern_edited(panel, lane))
            return lane + 1u;
    }
    return 0;
}

static unsigned conductor_capture_draft(const struct panel *panel, unsigned lane, conductor_draft *draft)
{
    if (!conductor_pattern_edited(panel, lane)) return 0;
    if (lane == MIX_TELEGRAM && panel->telegram_library) {
        const tg_saved_phrase *saved;
        if (!tg_library_find_phrase(&user_library, panel->telegram_library - 1u, &saved) &&
            conductor_phrase_matches(panel, saved)) return 0;
    }
    unsigned slot;
    switch (lane) {
    case MIX_808:
        draft->drum = panel->drums_pattern;
        slot = panel->drums_slot;
        break;
    case MIX_808_B:
        draft->drum = panel->drums_b.pattern;
        slot = panel->drums_b.slot;
        break;
    case MIX_DILLO:
        draft->dillo = panel->dillo_pattern;
        slot = panel->dillo_slot;
        break;
    default:
        memcpy(draft->telegram.steps, panel->sound.steps, sizeof draft->telegram.steps);
        draft->telegram.length = (uint8_t)tg_pattern_length(&panel->sound);
        draft->telegram.gate = panel->sound.phrase_gate;
        slot = panel->telegram_slot;
        break;
    }
    return slot + 1u;
}

static void conductor_stash_pattern(struct panel *panel, unsigned lane)
{
    unsigned source = conductor_capture_draft(panel, lane, &conductor_drafts[lane]);
    if (!source) return;
    panel->draft_slot[lane] = (uint8_t)source;
    if (lane == MIX_TELEGRAM) panel->telegram_draft_library = panel->telegram_library;
}

/* Rebinding the working pattern keeps its current audio cue generation. */
static void conductor_bind_pattern(struct panel *panel, unsigned lane, unsigned slot)
{
    uint8_t *current;
    switch (lane) {
    case MIX_808: current = &panel->drums_slot; break;
    case MIX_808_B: current = &panel->drums_b.slot; break;
    case MIX_DILLO: current = &panel->dillo_slot; break;
    case MIX_TELEGRAM:
        current = &panel->telegram_slot;
        panel->telegram_library = 0;
        break;
    default: return;
    }
    /* The same cues still launch, but a new slot changes the setup's tuple. */
    if (*current != slot) panel->scene_queued = 0;
    *current = (uint8_t)slot;
    if (panel->conductor_copy_needed == lane + 1u) panel->conductor_copy_needed = 0;
    conductor_changed(panel);
}

static void conductor_copy_pattern(struct panel *panel, unsigned lane, unsigned slot)
{
    unsigned bit = 1u << slot;
    switch (lane) {
    case MIX_808:
        if (!panel->drums_initialized) panel->drums_pattern = drum_demo;
        conductor_drum_slots[slot] = panel->drums_pattern;
        panel->drums_initialized = 1;
        panel->drums_slots |= bit;
        break;
    case MIX_808_B:
        if (!panel->drums_b_initialized) panel->drums_b.pattern = drum_demo_b;
        conductor_drum_b_slots[slot] = panel->drums_b.pattern;
        panel->drums_b_initialized = 1;
        panel->drums_b_slots |= bit;
        break;
    case MIX_DILLO:
        if (!panel->dillo_pattern.length) panel->dillo_pattern = dillo_demo;
        conductor_dillo_slots[slot] = panel->dillo_pattern;
        panel->dillo_slots |= bit;
        break;
    case MIX_TELEGRAM: {
        tg_saved_phrase *phrase = &conductor_telegram_slots[slot];
        memcpy(phrase->steps, panel->sound.steps, sizeof phrase->steps);
        phrase->length = (uint8_t)tg_pattern_length(&panel->sound);
        phrase->gate = panel->sound.phrase_gate;
        panel->telegram_slots |= bit;
        break;
    }
    default:
        return;
    }
    conductor_bind_pattern(panel, lane, slot);
}

static unsigned conductor_has_pattern(const struct panel *panel, unsigned lane, unsigned slot)
{
    const uint16_t saved[] = {[MIX_TELEGRAM] = panel->telegram_slots, [MIX_DILLO] = panel->dillo_slots,
        [MIX_808] = panel->drums_slots, [MIX_808_B] = panel->drums_b_slots};
    return slot < CONDUCTOR_SLOTS && ((saved[lane] | 1u) & (1u << slot));
}

/* Loading a pattern is independent of arming its lane or changing panel focus. */
static void conductor_replace_pattern(struct panel *panel, unsigned lane, unsigned slot, const void *pattern)
{
    /* A later lane request changes the section without dropping its other cues. */
    panel->scene_queued = 0;
    panel->cancel_cue = CANCEL_CUE_NONE;
#ifdef ELITE_BAND_PRESETS
    if (lane != MIX_808_B) panel->band_preset = 0;
#endif
    switch (lane) {
    case MIX_808:
        panel->drums_pattern = *(const drum_pattern *)pattern;
        panel->drums_initialized = 1;
        panel->drums_slot = (uint8_t)slot;
        conductor_forget_drum_undo(panel, 0);
        panel->drums_cue++;
        break;
    case MIX_808_B:
        panel->drums_b.pattern = *(const drum_pattern *)pattern;
        panel->drums_b_initialized = 1;
        panel->drums_b.slot = (uint8_t)slot;
        conductor_forget_drum_undo(panel, 1);
        panel->drums_b.cue++;
        break;
    case MIX_DILLO:
        panel->dillo_pattern = *(const dillo_phrase *)pattern;
#ifdef ELITE_BAND_PRESETS
        panel->studio_dillo_pending = 0;
        panel->dillo_sound_cue = 0;
#endif
        panel->dillo_undo = UNDO_NONE;
        panel->dillo_slot = (uint8_t)slot;
        panel->dillo_cue++;
        break;
    case MIX_TELEGRAM: {
        const tg_saved_phrase *phrase = pattern;
#ifdef ELITE_BAND_PRESETS
        panel->studio_telegram_pending = 0;
#endif
        memcpy(panel->sound.steps, phrase->steps, sizeof phrase->steps);
        panel->sound.length = phrase->length;
        panel->sound.phrase_gate = phrase->gate;
        panel->cursor = 0;
        panel->phrase_undo = UNDO_NONE;
        panel->sound.phrase_epoch++;
        panel->telegram_slot = (uint8_t)slot;
        panel->telegram_library = 0;
        panel->telegram_cue++;
        break;
    }
    }
    if (panel->conductor_copy_needed == lane + 1u) panel->conductor_copy_needed = 0;
}

static int conductor_cue_pattern(struct panel *panel, unsigned lane, unsigned slot)
{
    if (!conductor_has_pattern(panel, lane, slot)) return 0;
    const void *pattern;
    switch (lane) {
    case MIX_808: pattern = conductor_a_pattern(panel, slot); break;
    case MIX_808_B: pattern = conductor_b_pattern(panel, slot); break;
    case MIX_DILLO: pattern = conductor_dillo_pattern(panel, slot); break;
    default: pattern = conductor_telegram_pattern(panel, slot); break;
    }
    conductor_replace_pattern(panel, lane, slot, pattern);
    return 1;
}

static int conductor_select_pattern(struct panel *panel, unsigned lane, unsigned slot)
{
    if (!conductor_has_pattern(panel, lane, slot)) return 0;
    conductor_stash_pattern(panel, lane);
    conductor_cue_pattern(panel, lane, slot);
    conductor_changed(panel);
    return 1;
}

static void conductor_restore_draft(struct panel *panel, unsigned lane)
{
    if (!panel->draft_slot[lane]) return;
    unsigned slot = panel->draft_slot[lane] - 1u;
    conductor_draft draft = conductor_drafts[lane];
    unsigned library = panel->telegram_draft_library;
    panel->draft_slot[lane] = 0;
    /* Keep a different edit in the same lane when retrieving its draft. */
    conductor_stash_pattern(panel, lane);
    if (!conductor_has_pattern(panel, lane, slot)) slot = 0;
    const void *pattern = lane == MIX_TELEGRAM ? (const void *)&draft.telegram :
                          lane == MIX_DILLO ? (const void *)&draft.dillo : &draft.drum;
    conductor_replace_pattern(panel, lane, slot, pattern);
    if (lane == MIX_TELEGRAM) {
        panel->sound.transpose = 0;
        panel->telegram_library = (uint8_t)library;
    }
    panel->scene_active = 0;
    conductor_changed(panel);
}

static void conductor_toggle_pattern(struct panel *panel, unsigned lane)
{
    switch (lane) {
    case MIX_TELEGRAM:
        panel->sound.phrase_muted ^= 1u;
        break;
    case MIX_DILLO:
        if (!panel->dillo_pattern.length) conductor_cue_pattern(panel, MIX_DILLO, 0);
        panel->dillo_enabled ^= 1u;
        break;
    case MIX_808:
        if (!panel->drums_initialized) conductor_cue_pattern(panel, MIX_808, 0);
        panel->drums_enabled ^= 1u;
        if (!panel->drums_enabled) clear_drum_rolls(panel, DRUM_LATCH_A);
        break;
    case MIX_808_B:
        if (!panel->drums_b_initialized) conductor_cue_pattern(panel, MIX_808_B, 0);
        panel->drums_b.muted = panel->drums_b.enabled && !panel->drums_b.muted;
        panel->drums_b.enabled = 1;
        if (panel->drums_b.muted) clear_drum_rolls(panel, DRUM_LATCH_B);
        break;
    }
    conductor_changed(panel);
}

static unsigned conductor_pattern_enabled(const struct panel *panel, unsigned lane)
{
    switch (lane) {
    case MIX_TELEGRAM: return !panel->sound.phrase_muted;
    case MIX_DILLO: return panel->dillo_enabled;
    case MIX_808: return panel->drums_enabled;
    default: return panel->drums_b.enabled && !panel->drums_b.muted;
    }
}

static unsigned conductor_running(const struct panel *panel)
{
    return panel->sound.running || tg_clock_active(&panel->sound.clock);
}

static unsigned conductor_clock_lost(const struct panel *panel)
{
    const tg_clock_snapshot *clock = &panel->sound.clock;
    return clock->source == TG_CLOCK_USB && clock->state == TG_CLOCK_LOST;
}

static unsigned conductor_cues_at_bar(const struct panel *panel)
{
    return conductor_running(panel) || conductor_clock_lost(panel);
}

static void conductor_drums_b_pad(struct panel *panel, unsigned pad, unsigned store)
{
    unsigned slot = panel->conductor_bank * CONDUCTOR_ROW + pad;
    unsigned arm = !panel->drums_b_initialized || !conductor_cues_at_bar(panel);
    if (store) {
        conductor_copy_pattern(panel, MIX_808_B, slot);
    } else if (conductor_select_pattern(panel, MIX_808_B, slot)) {
        if (arm) panel->drums_b.enabled = 1;
    }
    panel->conductor_lane = PAGE_DRUMS;
}

static void open_drum_editor(struct panel *panel, unsigned arm_demo)
{
    if (drum_part(panel)) {
        if (!panel->drums_b_initialized) {
            panel->drums_b.pattern = drum_demo_b;
            panel->drums_b_initialized = 1;
        }
    } else if (!panel->drums_initialized) {
        panel->drums_pattern = drum_demo;
        panel->drums_initialized = 1;
        if (arm_demo) panel->drums_enabled = 1;
    }
    panel->page = PAGE_DRUMS;
}

static void conductor_pad(struct panel *panel, unsigned pad, unsigned store)
{
    unsigned slot = panel->conductor_bank * CONDUCTOR_ROW + pad % CONDUCTOR_ROW;
    if (pad < CONDUCTOR_ROW) {
        unsigned arm = !panel->drums_initialized || !conductor_cues_at_bar(panel);
        if (store) {
            conductor_copy_pattern(panel, MIX_808, slot);
        } else if (conductor_select_pattern(panel, MIX_808, slot) && arm) {
            panel->drums_enabled = 1;
        }
        panel->conductor_lane = PAGE_DRUMS;
    } else if (panel->conductor_telegram) {
        if (store) {
            conductor_copy_pattern(panel, MIX_TELEGRAM, slot);
        } else if (conductor_select_pattern(panel, MIX_TELEGRAM, slot)) {
            panel->sound.transpose = 0;
        }
        panel->conductor_lane = PAGE_STEPS;
    } else {
        unsigned arm = !panel->dillo_pattern.length || !conductor_cues_at_bar(panel);
        if (store) {
            conductor_copy_pattern(panel, MIX_DILLO, slot);
        } else if (conductor_select_pattern(panel, MIX_DILLO, slot) && arm) {
            panel->dillo_enabled = 1;
        }
        panel->conductor_lane = PAGE_DILLO;
    }
}

/* The Band's pads in lane order and colour. Two 808s: four groups of four (808 A 1-4,
 * 808 B 5-8, Dillo 9-12, Telegram 13-16). With one 808 its pads span 1-8.
 * The split synth groups page by four. A selected full synth row pages by eight. */
enum { BAND_GROUP = 4, BAND_BANKS = CONDUCTOR_SLOTS / BAND_GROUP };
enum { BAND_BOTTOM_SPLIT, BAND_BOTTOM_DILLO, BAND_BOTTOM_TELEGRAM, BAND_BOTTOM_MODES };
enum { BAND_DRUM_PADS = ELITE_DRUM_PARTS > 1 ? BAND_GROUP : 2 * BAND_GROUP };

static unsigned band_group_lane(unsigned pad)
{
    if (pad < BAND_DRUM_PADS) return MIX_808;
    return conductor_lane_order[1u + (pad - BAND_DRUM_PADS) / BAND_GROUP];
}

static unsigned band_pad_lane(const struct panel *panel, unsigned pad)
{
    if (BAND_DRUM_PADS == CONDUCTOR_ROW && pad >= CONDUCTOR_ROW && panel->band_bottom)
        return panel->band_bottom == BAND_BOTTOM_DILLO ? MIX_DILLO : MIX_TELEGRAM;
    return band_group_lane(pad);
}

static unsigned band_pad_slot(const struct panel *panel, unsigned pad)
{
    if (BAND_DRUM_PADS == CONDUCTOR_ROW && (pad < CONDUCTOR_ROW || panel->band_bottom))
        return panel->band_bank / 2u * CONDUCTOR_ROW + pad % CONDUCTOR_ROW;
    return panel->band_bank * BAND_GROUP + pad % BAND_GROUP;
}

/* Pad colours by lane: 808 A red, 808 B yellow, Dillo silver, Telegram orange. */
static const uint8_t band_lane_rgb[MIX_AUDIO_LANES][3] = {
    [MIX_808] = {100, 25, 18}, [MIX_808_B] = {95, 76, 19}, [MIX_DILLO] = {84, 81, 77}, [MIX_TELEGRAM] = {100, 50, 15}};

static unsigned band_lane_editor(unsigned lane)
{
    return lane == MIX_808 ? EDITOR_A : lane == MIX_808_B ? EDITOR_B :
           lane == MIX_DILLO ? EDITOR_DILLO : EDITOR_TELEGRAM;
}

static unsigned band_lane_page(unsigned lane)
{
    return lane == MIX_DILLO ? PAGE_DILLO : lane == MIX_TELEGRAM ? PAGE_STEPS : PAGE_DRUMS;
}

static void band_pad(struct panel *panel, unsigned pad, unsigned store)
{
    unsigned lane = band_pad_lane(panel, pad);
    unsigned slot = band_pad_slot(panel, pad);
    unsigned part_b = lane == MIX_808_B;
    unsigned fresh = lane == MIX_808 ? !panel->drums_initialized : part_b ? !panel->drums_b_initialized :
                     lane == MIX_DILLO ? !panel->dillo_pattern.length : 0;
    unsigned arm = fresh || !conductor_cues_at_bar(panel);
    unsigned acted = 0;
    if (store) {
        conductor_copy_pattern(panel, lane, slot);
        acted = 1;
    } else if (conductor_select_pattern(panel, lane, slot)) {
        acted = 1;
        if (lane == MIX_TELEGRAM) panel->sound.transpose = 0;
        else if (arm && lane == MIX_808) panel->drums_enabled = 1;
        else if (arm && part_b) panel->drums_b.enabled = 1;
        else if (arm && lane == MIX_DILLO) panel->dillo_enabled = 1;
    }
    /* SEQ then opens the 808 part last launched or captured; an empty slot changes nothing saved. */
    if (acted && (lane == MIX_808 || part_b))
        if (ELITE_DRUM_PARTS > 1) panel->drums_focus = (uint8_t)part_b; /* the cue or capture already marked the change */
    panel->conductor_lane = (uint8_t)band_lane_page(lane);
}

static conductor_mixer conductor_mixer_controls(const struct panel *panel)
{
    conductor_mixer mixer = panel->mixer;
    mixer.value[MIX_TELEGRAM][MIX_DELAY] = panel->sound.patch.value[TG_DELAY];
    mixer.value[MIX_TELEGRAM][MIX_ROOM] = panel->sound.patch.value[TG_REVERB];
    mixer.value[MIX_TELEGRAM][MIX_DRIVE] = panel->sound.patch.drive;
    return mixer;
}

static const char *panel_sound_name(const struct panel *panel);
/* The Studio Dillo recipes the Band presets already carry, browsable as sounds after the
 * factory, user and expansion pages. Zero without the Band bank. */
enum { DILLO_SLOT_NONE = 0xff };
static unsigned dillo_studio_sound_count(void);
static const bb_dillo_patch *dillo_studio_sound(unsigned at);
static const char *dillo_studio_sound_name(unsigned at);
#ifdef ELITE_BAND_PRESETS
static dillo_sound dillo_saved_sound(unsigned slot);
#else
static unsigned dillo_studio_sound_count(void) { return 0; }
static const bb_dillo_patch *dillo_studio_sound(unsigned at) { (void)at; return &bb_dillo_bank[0].sound; }
static const char *dillo_studio_sound_name(unsigned at) { (void)at; return "STUDIO"; }
#endif
static unsigned dillo_sound_total(void) { return DILLO_SOUND_SLOTS + dillo_studio_sound_count(); }
static unsigned dillo_sound_banks(void)
{
    return (dillo_sound_total() + DILLO_SOUND_PAGE_SLOTS - 1u) / DILLO_SOUND_PAGE_SLOTS;
}
#include "conductor_storage.inc"
#undef CONDUCTOR_POOL

struct conductor_row_state {
    unsigned occupied, queued, active, pending, enabled;
};

static struct conductor_row_state conductor_lane_state(const struct panel *panel, unsigned lane)
{
    switch (lane) {
    case MIX_808_B:
        return (struct conductor_row_state){panel->drums_b_slots | 1u, panel->drums_b.slot,
            panel->drums_b_active, panel->drums_b.cue != panel->drums_b_applied, panel->drums_b_playing};
    case MIX_808:
        return (struct conductor_row_state){panel->drums_slots | 1u, panel->drums_slot,
            panel->drums_active, panel->drums_cue != panel->drums_applied, panel->drums_playing};
    case MIX_TELEGRAM:
        return (struct conductor_row_state){panel->telegram_slots | 1u, panel->telegram_slot,
            panel->telegram_active, panel->telegram_cue != panel->telegram_applied, !panel->sound.phrase_muted};
    default:
        return (struct conductor_row_state){panel->dillo_slots | 1u, panel->dillo_slot,
            panel->dillo_active, panel->dillo_cue != panel->dillo_applied, panel->dillo_playing};
    }
}

static struct conductor_row_state conductor_row(const struct panel *panel, unsigned row)
{
    unsigned lane = row ? (panel->conductor_telegram ? MIX_TELEGRAM : MIX_DILLO) :
                          (drum_part(panel) ? MIX_808_B : MIX_808);
    return conductor_lane_state(panel, lane);
}

static unsigned conductor_edit_lane(const struct panel *panel)
{
    switch (panel->page) {
    case PAGE_STEPS: return MIX_TELEGRAM;
    case PAGE_DILLO: return MIX_DILLO;
    case PAGE_DRUMS: return drum_part(panel) ? MIX_808_B : MIX_808;
    default: return MIX_AUDIO_LANES;
    }
}

static unsigned conductor_edit_head(const struct panel *panel, unsigned head)
{
    unsigned lane = conductor_edit_lane(panel);
    if (lane == MIX_AUDIO_LANES) return head;
    struct conductor_row_state state = conductor_lane_state(panel, lane);
    return state.pending || !state.enabled ? TG_STEPS : head;
}

static unsigned conductor_scene_pending(const struct panel *panel)
{
    return panel->drums_cue != panel->drums_applied || panel->drums_b.cue != panel->drums_b_applied ||
           panel->dillo_cue != panel->dillo_applied || panel->telegram_cue != panel->telegram_applied;
}

typedef struct {
    conductor_draft pattern[MIX_AUDIO_LANES];
    uint8_t source[MIX_AUDIO_LANES], telegram_library;
} conductor_cancel_drafts;

static void conductor_prepare_cancel(const struct panel *panel, conductor_cancel_drafts *drafts)
{
    for (unsigned lane = 0; lane < MIX_AUDIO_LANES; lane++)
        drafts->source[lane] = (uint8_t)conductor_capture_draft(panel, lane, &drafts->pattern[lane]);
    drafts->telegram_library = panel->telegram_library;
}

/* The caller samples the applied state and publishes the rollback under the
 * same interrupt lock. A cue acknowledged at the bar is already current. */
static unsigned conductor_cancel_pending(struct panel *panel, conductor_patterns *request,
                                         const conductor_patterns *active, const tg_controls *voice)
{
    unsigned canceled = 0;
    if (request->drums_cue != active->drums_cue) {
        panel->drums_pattern = request->drums = active->drums;
        panel->drums_slot = request->drums_slot = active->drums_slot;
        panel->drums_cue = request->drums_cue = active->drums_cue;
        request->drum_init = (request->drum_init & ~1u) | (active->drum_init & 1u);
        panel->drums_initialized = active->drum_init & 1u;
        panel->drums_enabled = request->drums_on = request->drums_on && panel->drums_initialized;
        conductor_forget_drum_undo(panel, 0);
        canceled |= 1u << MIX_808;
    }
    if (request->drums_b.cue != active->drums_b.cue) {
        panel->drums_b.pattern = request->drums_b.pattern = active->drums_b.pattern;
        panel->drums_b.slot = request->drums_b.slot = active->drums_b.slot;
        panel->drums_b.cue = request->drums_b.cue = active->drums_b.cue;
#ifdef ELITE_PERCUSSION_727
        panel->drums_b.percussion_kit = request->drums_b.percussion_kit = active->drums_b.percussion_kit;
#endif
        request->drum_init = (request->drum_init & ~2u) | (active->drum_init & 2u);
        panel->drums_b_initialized = (active->drum_init >> 1) & 1u;
        panel->drums_b.enabled = request->drums_b.enabled = request->drums_b.enabled && panel->drums_b_initialized;
        /* With no outgoing B pattern, the mute belonged to the canceled cue. */
        panel->drums_b.muted = request->drums_b.muted = request->drums_b.muted && panel->drums_b_initialized;
        conductor_forget_drum_undo(panel, 1);
        canceled |= 1u << MIX_808_B;
    }
    if (request->dillo_cue != active->dillo_cue) {
#ifdef ELITE_BAND_PRESETS
        panel->studio_dillo_pending = 0;
        panel->dillo_sound_cue = 0;
#endif
        panel->dillo_pattern = request->dillo = active->dillo;
        panel->dillo_slot = request->dillo_slot = active->dillo_slot;
        panel->dillo_cue = request->dillo_cue = active->dillo_cue;
        panel->dillo_enabled = request->dillo_on = request->dillo_on && active->dillo.length;
        panel->dillo_undo = UNDO_NONE;
        canceled |= 1u << MIX_DILLO;
    }
    if (request->telegram_cue != active->telegram_cue) {
#ifdef ELITE_BAND_PRESETS
        panel->studio_telegram_pending = 0;
#endif
        memcpy(panel->sound.steps, voice->steps, sizeof voice->steps);
        panel->sound.length = voice->length;
        panel->sound.phrase_gate = voice->phrase_gate;
        panel->sound.phrase_epoch = voice->phrase_epoch;
        panel->sound.transpose = voice->transpose;
        panel->telegram_slot = request->telegram_slot = active->telegram_slot;
        panel->telegram_library = request->telegram_library = active->telegram_library;
        panel->telegram_cue = request->telegram_cue = active->telegram_cue;
        panel->cursor = 0;
        panel->phrase_undo = UNDO_NONE;
        canceled |= 1u << MIX_TELEGRAM;
    }
    if (canceled) {
        panel->scene_queued = request->scene = 0;
#ifdef ELITE_BAND_PRESETS
        panel->band_preset = 0;
#endif
        panel->conductor_copy_needed = 0;
    }
    return canceled;
}

static void conductor_finish_cancel(struct panel *panel, const conductor_cancel_drafts *drafts, unsigned canceled)
{
    panel->cancel_cue = canceled ? CANCEL_CUE_DONE : CANCEL_CUE_EMPTY;
    if (!canceled) return;
    if (panel->scene_status == SCENE_CUED) panel->scene_status = SCENE_NORMAL;
    for (unsigned lane = 0; lane < MIX_AUDIO_LANES; lane++) {
        if (!(canceled & (1u << lane)) || !drafts->source[lane]) continue;
        conductor_drafts[lane] = drafts->pattern[lane];
        panel->draft_slot[lane] = drafts->source[lane];
        if (lane == MIX_TELEGRAM) panel->telegram_draft_library = drafts->telegram_library;
    }
    conductor_changed(panel);
}

static unsigned conductor_queued_scene(const struct panel *panel)
{
    return conductor_scene_pending(panel) ? panel->scene_queued : 0;
}

/* Reconcile the section under the same lock that publishes the next request. */
static unsigned conductor_acknowledge_scene(struct panel *panel, const conductor_patterns *published)
{
    unsigned scene = published->scene;
    if (!scene || panel->drums_applied != published->drums_cue || panel->drums_b_applied != published->drums_b.cue ||
        panel->dillo_applied != published->dillo_cue || panel->telegram_applied != published->telegram_cue)
        return 0;

    /* A take may finish before the panel learns that its section has landed.
     * Only its Dillo request is still pending. A fresh setup cue changes all four. */
    if (panel->scene_queued == scene && panel->dillo_cue != published->dillo_cue &&
        panel->drums_cue == published->drums_cue && panel->drums_b.cue == published->drums_b.cue &&
        panel->telegram_cue == published->telegram_cue)
        panel->scene_queued = 0;
    return scene;
}

static void conductor_update_active_scene(struct panel *panel, unsigned applied_scene)
{
    if (applied_scene) panel->scene_active = (uint8_t)applied_scene;
    unsigned active = panel->scene_active;
    if (!active) return;
    const conductor_scene *scene = &conductor_scenes[active - 1u];
    if (!(panel->scene_slots & (1u << (active - 1u))) || panel->telegram_library_active ||
        panel->drums_active != scene->setup[CONDUCTOR_A_SLOT] ||
        panel->drums_b_active != scene->drums_b[CONDUCTOR_B_SLOT] ||
        panel->dillo_active != scene->setup[CONDUCTOR_DILLO_SLOT] ||
        panel->telegram_active != scene->setup[CONDUCTOR_TELEGRAM_SLOT])
        panel->scene_active = 0;
}

static void conductor_editor(struct panel *panel, unsigned target)
{
    if (target == EDITOR_A || target == EDITOR_B) {
        if (ELITE_DRUM_PARTS > 1) panel->drums_focus = target == EDITOR_B;
        panel->conductor_lane = PAGE_DRUMS;
    } else if (target == EDITOR_DILLO) {
        panel->conductor_lane = PAGE_DILLO;
        if (!panel->dillo_pattern.length) panel->dillo_pattern = dillo_demo;
    } else if (target == EDITOR_TELEGRAM) {
        panel->conductor_lane = PAGE_STEPS;
    }
    if (panel->conductor_lane == PAGE_DRUMS) open_drum_editor(panel, target == EDITOR_LAST);
    else if (panel->conductor_lane == PAGE_DILLO && panel->dillo_pattern.length) panel->page = PAGE_DILLO;
    else panel->page = PAGE_STEPS;
}
#else
enum { HOME_PAGE = PAGE_TONE };
#endif

enum { MAP_KNOB, MAP_FADER, MAP_PAD };
enum { FADER_PAGES = 4, CTRL_LEVEL = TG_CONTROL_LEVEL, CTRL_TEMPO = TG_CONTROL_TEMPO, CTRL_OSC_OCTAVE = TG_CONTROL_OSC_OCTAVE, CTRL_KEY_OCTAVE = TG_CONTROL_KEY_OCTAVE, CTRL_ENV_WIDE = TG_CONTROL_ENV_WIDE, CTRL_FILTER_LOW = TG_CONTROL_FILTER_LOW, CTRL_LFO_RETRIGGER = TG_CONTROL_LFO_RETRIGGER, CTRL_FILTER_IDLE_HOLD = TG_CONTROL_FILTER_IDLE_HOLD, CTRL_DRIVE = TG_CONTROL_DRIVE, CTRL_FILTER_ATTACK = TG_CONTROL_FILTER_ATTACK, CTRL_ACCENT_TONE = TG_CONTROL_ACCENT_TONE, CTRL_ACCENT_LEVEL = TG_CONTROL_ACCENT_LEVEL, CTRL_PWM_SOURCE = TG_CONTROL_PWM_SOURCE, CTRL_COUNT = TG_CONTROL_COUNT };

enum { MIDI_CHANNELS = 16, PROFILE_FIRST_KEY = 16, SCALE_FIRST_KEY = 24, SCALE_ROOTS = 12, RATE_FIRST_KEY = 24 };
enum {
    OCTAVE_KEY = 7, LATCH_KEY, GATE_UP_KEY, GATE_DOWN_KEY, TAP_KEY,
    SWING_UP_KEY, SWING_DOWN_KEY, TEMPO_UP_KEY, TEMPO_DOWN_KEY, SYNC_KEY
};
static const uint64_t arp_command_keys = ((UINT64_C(1) << (SYNC_KEY + 1)) - 1u) |
    (((UINT64_C(1) << TG_ARP_RATE_COUNT) - 1u) << RATE_FIRST_KEY);
static const uint16_t scale_intervals[] = {
    0xfff, /* Off */
    0xab5, /* Major */
    0x5ad, /* Natural minor */
    0x6ad, /* Dorian */
    0xad5, /* Lydian */
    0x295, /* Major pentatonic */
    0x4a9, /* Minor pentatonic */
};

static const uint8_t fx_map[] = {CTRL_DRIVE, TG_DELAY, TG_REVERB};

static int control_switch(unsigned control)
{
    return control >= CTRL_ENV_WIDE && control < CTRL_DRIVE;
}

static int control_octave(unsigned control)
{
    return control == CTRL_OSC_OCTAVE || control == CTRL_KEY_OCTAVE;
}

static int control_five_positions(unsigned control)
{
    return control_octave(control) || control == CTRL_PWM_SOURCE;
}

static int performance_page(unsigned page)
{
    return page == PAGE_TONE || page == PAGE_MANUAL || page == PAGE_FX || page == PAGE_GLOBAL || page == PAGE_FILTER_ENV || page == PAGE_ARP;
}

static void apply_layout(struct panel *panel, const tg_layout *layout)
{
    memcpy(panel->knob_map, layout->knobs, sizeof panel->knob_map);
    memcpy(panel->fader_map, layout->faders, sizeof panel->fader_map);
    memcpy(panel->pad_note, layout->pads, sizeof panel->pad_note);
    panel->pad_light = layout->brightness;
}
static void capture_layout(const struct panel *panel, tg_layout *layout)
{
    memcpy(layout->knobs, panel->knob_map, sizeof panel->knob_map);
    memcpy(layout->faders, panel->fader_map, sizeof panel->fader_map);
    memcpy(layout->pads, panel->pad_note, sizeof panel->pad_note);
    layout->brightness = panel->pad_light;
}
static unsigned panel_layout_edited(const struct panel *panel)
{
    tg_layout current;
    capture_layout(panel, &current);
    return memcmp(&current, &user_library.layouts[panel->profile], sizeof current) != 0;
}

static void select_profile(struct panel *panel, unsigned profile)
{
    if (profile == panel->profile) return;
    capture_layout(panel, &profile_drafts[panel->profile]);
    profile_draft_slots |= (uint8_t)(1u << panel->profile);
    panel->profile = (uint8_t)profile;
    const tg_layout *layout = profile_draft_slots & (1u << profile) ?
                              &profile_drafts[profile] : &user_library.layouts[profile];
    apply_layout(panel, layout);
    fader_caught = 0;
    panel->pads_down = panel->repeat_latched = 0;
    panel->focus_control = panel->arp_focus = 0;
}

static void save_layout(struct panel *panel)
{
    capture_layout(panel, &user_library.layouts[panel->profile]);
    user_library.profile = panel->profile;
    profile_draft_slots &= (uint8_t)~(1u << panel->profile);
    panel->store_request = panel->library_dirty = 1;
    panel->library_status = LIB_SESSION;
}
static void init_map(struct panel *panel)
{
#ifdef ELITE_DRUMS808
    panel->mixer = (conductor_mixer)CONDUCTOR_MIX_DEFAULT;
    drum_kit_default(&panel->drums_kit);
    drum_kit_default(&panel->drums_b.kit);
    panel->drums_b.mix[MIX_LEVEL] = 90;
    panel->drums_b.mix[MIX_PAN] = 64;
    panel->dillo_sound = (dillo_sound){.tone = bb_dillo_bank[DILLO_DEFAULT_SOUND].sound};
    panel->dillo_sound_slot = DILLO_DEFAULT_SOUND;
#endif
    tg_layout layout;
    tg_layout_default(&layout);
    layout.brightness = panel->pad_light;
    apply_layout(panel, &layout);
}

static unsigned control_value(const tg_controls *sound, unsigned control)
{
    if (control == CTRL_TEMPO && sound->clock.source == TG_CLOCK_USB) {
        unsigned bpm = sound->clock.period_us ? 2500000u / sound->clock.period_us : 0;
        return bpm > 999 ? 999 : bpm;
    }
    if (control == CTRL_PWM_SOURCE) return sound->patch.pwm;
    if (control == CTRL_ACCENT_TONE) return sound->patch.accent_tone;
    if (control == CTRL_ACCENT_LEVEL) return sound->patch.accent_level;
    if (control >= CTRL_FILTER_ATTACK)
        return tg_filter_adsr_value(&sound->patch, control - CTRL_FILTER_ATTACK);
    if (control == CTRL_DRIVE) return sound->patch.drive;
    if (control == CTRL_FILTER_IDLE_HOLD) return sound->patch.filter_idle_hold;
    if (control == CTRL_LFO_RETRIGGER) return sound->patch.lfo_retrigger;
    if (control == CTRL_FILTER_LOW) return sound->patch.filter_low;
    if (control == CTRL_ENV_WIDE) return sound->patch.env_wide;
    if (control == CTRL_OSC_OCTAVE) return (unsigned)(sound->patch.osc_octave+2);
    if (control == CTRL_KEY_OCTAVE) return (unsigned)(sound->patch.key_octave+2);
    return control == CTRL_LEVEL ? sound->master : control == CTRL_TEMPO ? sound->tempo :
           sound->patch.value[control];
}

static tg_keyboard keyboard, held_keyboard;
/* A shortcut owns its keys until release, even after its modifier is let go. */
static uint64_t transpose_down, transpose_keys;
static tg_latch held_chord, arp_chord;
static uint64_t key_order_mask;
static uint8_t key_order[TG_KEYS], key_order_count;
static uint8_t midi_pad_velocity[TG_PADS];

static void update_key_order(uint64_t keys)
{
    if (keys == key_order_mask) return;
    unsigned count = 0;
    for (unsigned i = 0; i < key_order_count; i++)
        if (keys & (UINT64_C(1) << key_order[i])) key_order[count++] = key_order[i];
    uint64_t added = keys & ~key_order_mask;
    for (unsigned key = 0; key < TG_KEYS; key++)
        if (added & (UINT64_C(1) << key)) key_order[count++] = (uint8_t)key;
    key_order_count = (uint8_t)count;
    key_order_mask = keys;
}
static uint8_t last_fader[4], faders_ready;
static uint16_t bend_center;
static uint8_t wheel_ready;
/* A matrix frame is 1.6 ms. Keep the gesture clock outside the displayed
 * panel snapshot, so waiting for a hold never forces extra LCD redraws. */
enum { HOME_HOLD_FRAMES = 375, ARP_HOLD_FRAMES = 250 }; /* 600 ms, 400 ms */
enum { CONTROL_FOCUS_FRAMES = 875 }; /* 1.4 s */
/* A struck pad glows on screen for about a third of a second, in four steps. */
enum { PAD_HIT_FRAMES = 200, PAD_HIT_STEP = PAD_HIT_FRAMES / 4 };
#ifdef ELITE_DRUMS808

static unsigned pad_hit_glow(const struct panel *panel, unsigned pad)
{
    if (panel->pad_hit != pad + 1u) return 0;
    uint32_t age = panel->view_frames - panel->pad_hit_frame;
    return age < PAD_HIT_FRAMES ? 4u - age / PAD_HIT_STEP : 0;
}

/* True while a pad's glow is fading and its step has moved on (one more frame clears it). */
static unsigned view_motion_due(const struct panel *panel, uint32_t previous)
{
    if (!panel->pad_hit) return 0;
    uint32_t age = panel->view_frames - panel->pad_hit_frame, before = previous - panel->pad_hit_frame;
    return age < PAD_HIT_FRAMES + PAD_HIT_STEP && age / PAD_HIT_STEP != before / PAD_HIT_STEP;
}
#else
static unsigned pad_hit_glow(const struct panel *panel, unsigned pad) { (void)panel; (void)pad; return 0; }
static unsigned view_motion_due(const struct panel *panel, uint32_t previous) { (void)panel; (void)previous; return 0; }
#endif
static uint32_t home_frame, control_focus_frame, arp_tap_frame, arp_press_frame;
static uint8_t arp_tap_seen;
static uint8_t focus_fader_position[4];

static void focus_control(struct panel *panel, unsigned control, unsigned slot,
                          unsigned pickup, unsigned position, uint32_t frames)
{
    panel->arp_focus = 0;
    panel->focus_control = (uint8_t)(control + 1u);
    panel->focus_slot = (uint8_t)slot;
    panel->focus_pickup = (uint8_t)pickup;
    panel->focus_position = (uint8_t)position;
    control_focus_frame = frames;
}
static uint8_t home_armed;
#ifdef ELITE_DRUMS808
static int library_back(struct panel *panel);
#endif

static void home_navigation(struct panel *panel, uint32_t pressed, uint32_t buttons, uint32_t frames)
{
    if (pressed & (1u << BTN_ASSIGN)) {
#ifdef ELITE_DRUMS808
        /* Library back takes priority. On the single-808 panel, BT returns
         * to Band and a tap on Band opens the instrument picker. */
        if (library_back(panel)) {
            if (ELITE_DRUM_PARTS == 1) panel->instruments_return = panel->page;
        }
        else if (panel->page == PAGE_INSTRUMENTS || panel->page == PAGE_ASSIGN) panel->page = panel->instruments_return;
        else {
            panel->instruments_return = panel->page;
            panel->page = ELITE_DRUM_PARTS == 1 && panel->page != PAGE_CONDUCTOR ? PAGE_CONDUCTOR : PAGE_INSTRUMENTS;
        }
#else
        panel->page = HOME_PAGE;
#endif
        panel->record = 0; /* Leaving entry must not keep writing invisible steps. */
        panel->save_mode = SAVE_NONE;
        home_frame = frames;
        home_armed = 1;
    }
    if (!(buttons & (1u << BTN_ASSIGN)))
        home_armed = 0;
    else if (home_armed && (uint32_t)(frames - home_frame) >= HOME_HOLD_FRAMES) {
        panel->page = PAGE_ASSIGN;
        fader_caught = 0;
        home_armed = 0;
    }
}

static unsigned bounded(int value, int low, int high)
{
    return (unsigned)(value < low ? low : value > high ? high : value);
}

static int wheel_bend(int raw, int center)
{
    /* The integer 1/4 input filter can settle three counts from either rail. */
    enum { LOW = 3, HIGH = 1020, DEAD = 8 };
    int above = raw >= center;
    int distance = above ? raw - center : center - raw;
    if (distance <= DEAD) return 0;
    int span = (above ? HIGH - center : center - LOW) - DEAD;
    int limit = above ? 511 : 512;
    int bend = (int)bounded(((distance - DEAD) * limit + span / 2) / span, 0, limit);
    return above ? bend : -bend;
}

static void set_control(tg_controls *sound, unsigned control, int value)
{
    if (control == CTRL_TEMPO) {
        if (sound->clock.source != TG_CLOCK_USB) sound->tempo = (uint16_t)bounded(value, 30, 300);
    }
    else if (control == CTRL_LEVEL)
        sound->master = (uint8_t)bounded(value, 0, 127);
    else if (control == CTRL_PWM_SOURCE) sound->patch.pwm = (uint8_t)bounded(value, 0, TG_PWM_SOURCE_COUNT - 1);
    else if (control == CTRL_ACCENT_TONE) sound->patch.accent_tone = (uint8_t)bounded(value, 0, 127);
    else if (control == CTRL_ACCENT_LEVEL) sound->patch.accent_level = (uint8_t)bounded(value, 0, 127);
    else if (control >= CTRL_FILTER_ATTACK)
        sound->patch.filter_adsr[control - CTRL_FILTER_ATTACK] = (uint8_t)(bounded(value, 0, 127) + 1u);
    else if (control == CTRL_DRIVE) sound->patch.drive = (uint8_t)bounded(value, 0, 127);
    else if (control == CTRL_FILTER_IDLE_HOLD) sound->patch.filter_idle_hold=(uint8_t)bounded(value,0,1);
    else if (control == CTRL_LFO_RETRIGGER) sound->patch.lfo_retrigger=(uint8_t)bounded(value,0,1);
    else if (control == CTRL_FILTER_LOW) sound->patch.filter_low=(uint8_t)bounded(value,0,1);
    else if (control == CTRL_ENV_WIDE) sound->patch.env_wide=(uint8_t)bounded(value,0,1);
    else if (control == CTRL_OSC_OCTAVE) sound->patch.osc_octave=(int8_t)((int)bounded(value,0,4)-2);
    else if (control == CTRL_KEY_OCTAVE) sound->patch.key_octave=(int8_t)((int)bounded(value,0,4)-2);
    else {
        sound->patch.value[control] = (uint8_t)bounded(value, 0, 127);
        if (control == TG_GLIDE && !sound->patch.glide_mode) sound->patch.glide_mode = TG_GLIDE_AUTO;
    }
}

static void select_preset(struct panel *panel, unsigned index)
{
    panel->compare_active = 0;
    panel->user_sound = 0;
    panel->imported_sound = panel->scene_sound = 0;
#ifdef ELITE_BAND_PRESETS
    panel->band_sound = panel->studio_sound = 0;
#endif
#ifdef ELITE_DRUMS808
    panel->startup_sound = 0;
#endif
    panel->preset = (uint8_t)index;
    panel->sound.patch = tg_presets[index].patch;
    panel->sound.sound_epoch += panel->sound.patch.filter_idle_hold;
    fader_caught = 0;
}

static struct patch_snapshot snapshot_patch(const struct panel *panel)
{
    return (struct patch_snapshot){.patch = panel->sound.patch, .imported_slot = panel->imported_slot,
        .preset = panel->preset, .user_slot = panel->user_slot, .user_sound = panel->user_sound,
        .imported_sound = panel->imported_sound, .scene_sound = panel->scene_sound,
#ifdef ELITE_DRUMS808
        .startup_sound = panel->startup_sound,
#endif
#ifdef ELITE_BAND_PRESETS
        .band_sound = panel->band_sound, .studio_sound = panel->studio_sound,
#endif
    };
}

static void remember_patch(struct panel *panel, const struct patch_snapshot *previous)
{
    if (previous->preset == panel->preset && previous->user_sound == panel->user_sound &&
        previous->imported_sound == panel->imported_sound && previous->scene_sound == panel->scene_sound &&
        previous->user_slot == panel->user_slot &&
        previous->imported_slot == panel->imported_slot &&
#ifdef ELITE_DRUMS808
        previous->startup_sound == panel->startup_sound &&
#endif
#ifdef ELITE_BAND_PRESETS
        previous->band_sound == panel->band_sound && previous->studio_sound == panel->studio_sound &&
#endif
        !memcmp(&previous->patch, &panel->sound.patch, sizeof previous->patch))
        return;
    panel->alternate_patch = *previous;
    panel->compare_available = 1;
}

static void compare_patch(struct panel *panel)
{
    struct patch_snapshot current = snapshot_patch(panel);
    const struct patch_snapshot *alternate = &panel->alternate_patch;
    panel->sound.patch = alternate->patch;
    panel->preset = alternate->preset;
    panel->user_slot = alternate->user_slot;
    panel->user_sound = alternate->user_sound;
    panel->imported_slot = alternate->imported_slot;
    panel->imported_sound = alternate->imported_sound;
    panel->scene_sound = alternate->scene_sound;
#ifdef ELITE_BAND_PRESETS
    panel->band_sound = alternate->band_sound;
    panel->studio_sound = alternate->studio_sound;
#endif
#ifdef ELITE_DRUMS808
    panel->startup_sound = alternate->startup_sound;
#endif
    panel->alternate_patch = current;
    panel->sound.sound_epoch += panel->sound.patch.filter_idle_hold;
    panel->compare_active = 1;
    if (!panel->store_request) panel->library_status = LIB_NORMAL;
    fader_caught = 0;
}

#ifdef ELITE_DRUMS808
/* Factory 808 rhythms from the Big Beat Studio library (tools/drum_presets.py). Read-only ROM:
 * a load replaces the focused part's working pattern, like a draft recall, and stays an unsaved
 * edit of its slot until REC copies it. The previous working pattern is kept as the lane's draft. */
#include "drum_presets.inc"
enum { DRUM_PRESET_PAGE = 16, DRUM_PRESET_BANKS = (DRUM_PRESET_COUNT + DRUM_PRESET_PAGE - 1) / DRUM_PRESET_PAGE };

static const drum_pattern *drum_working_pattern(const struct panel *panel)
{
    return drum_part(panel) ? &panel->drums_b.pattern : &panel->drums_pattern;
}

/* The preset the focused part is playing unchanged, plus one; 0 when edited or from a slot. */
static unsigned drum_preset_playing(const struct panel *panel)
{
    const drum_pattern *working = drum_working_pattern(panel);
    for (unsigned i = 0; i < DRUM_PRESET_COUNT; i++)
        if (!memcmp(working, &drum_presets[i], sizeof *working)) return i + 1u;
    return 0;
}

static void open_drum_presets(struct panel *panel)
{
    panel->rhythm_source = 0; /* RHYTHM_STUDIO */
    unsigned playing = drum_preset_playing(panel);
    panel->drum_preset_bank = playing ? (uint8_t)((playing - 1u) / DRUM_PRESET_PAGE) : panel->drum_preset_bank;
    panel->page = PAGE_DRUM_PRESETS;
}

static void conductor_load_drum_pattern(struct panel *panel, unsigned lane, const drum_pattern *pattern)
{
    unsigned part_b = lane == MIX_808_B;
    unsigned initialized = part_b ? panel->drums_b_initialized : panel->drums_initialized;
    unsigned arm = !initialized || !conductor_cues_at_bar(panel);
    conductor_stash_pattern(panel, lane);
    conductor_replace_pattern(panel, lane, part_b ? panel->drums_b.slot : panel->drums_slot, pattern);
    if (arm) {
        if (part_b) panel->drums_b.enabled = 1;
        else panel->drums_enabled = 1;
    }
    panel->scene_active = 0;
    conductor_changed(panel);
}

static void conductor_drum_preset_pad(struct panel *panel, unsigned pad)
{
    unsigned index = panel->drum_preset_bank * DRUM_PRESET_PAGE + pad;
    if (index >= DRUM_PRESET_COUNT) return;
    conductor_load_drum_pattern(panel, drum_part(panel) ? MIX_808_B : MIX_808, &drum_presets[index]);
    panel->conductor_lane = PAGE_DRUMS;
}

#ifdef ELITE_PERCUSSION_727
/* The Library supplies a 16-step pattern in LATIN row order. */
static void conductor_cue_layer_rhythm(struct panel *panel, const drum_pattern *pattern)
{
    conductor_load_drum_pattern(panel, MIX_808_B, pattern);
    panel->drums_b.percussion_kit = P727_LATIN + 1u;
}
#endif

/* Jukebox lane cells: off, held, hit, accented hit. */
enum { LANE_OFF, LANE_HOLD, LANE_HIT, LANE_ACCENT };

#ifdef ELITE_SYNTH_PRESETS
struct synth_preset_collection {
    const char *name;
    uint16_t start, count;
};
#include "factory_synth_phrases.inc"

#ifdef ELITE_BAND_PRESETS
struct band_preset_sound {
    char name[TG_NAME_BYTES];
    bb_dillo_patch dillo;
    tg_patch telegram;
    uint16_t tempo;
    uint8_t swing_x2, levels[2];
};
struct band_preset {
    char name[TG_NAME_BYTES];
    uint16_t drums, dillo, telegram;
    uint8_t sound, alternate;
};
struct band_preset_group {
    char name[TG_NAME_BYTES];
    uint16_t start, count;
};
#include "factory_band_presets.inc"

static unsigned conductor_band_count(void)
{
    return sizeof factory_band_presets / sizeof factory_band_presets[0];
}

static unsigned conductor_band_groups(void)
{
    return sizeof factory_band_groups / sizeof factory_band_groups[0];
}

/* Factory phrase indexes here are zero-based, including Studio offsets. */
static const bb_dillo_patch *conductor_studio_dillo_patch(unsigned phrase)
{
    unsigned at = phrase - studio_dillo_start;
    return at < sizeof studio_dillo_index ? &studio_dillo_sounds[studio_dillo_index[at]] : NULL;
}

static const tg_patch *conductor_studio_telegram_patch(unsigned phrase)
{
    unsigned at = phrase - studio_telegram_start;
    return at < sizeof studio_telegram_index ? &studio_telegram_sounds[studio_telegram_index[at]] : NULL;
}

static void conductor_prepare_studio(const struct panel *panel, conductor_patterns *patterns, tg_controls *controls)
{
    if (panel->studio_dillo_pending) {
        const bb_dillo_patch *patch = conductor_studio_dillo_patch(panel->studio_dillo_pending - 1u);
        patterns->dillo_sound = (dillo_sound){.tone = *patch};
        patterns->studio_cues |= CUE_DILLO;
    } else if (panel->dillo_sound_cue) {
        patterns->dillo_sound = dillo_saved_sound(panel->dillo_sound_cue - 1u);
        patterns->studio_cues |= CUE_DILLO;
    }
    if (panel->studio_telegram_pending) {
        controls->patch = *conductor_studio_telegram_patch(panel->studio_telegram_pending - 1u);
        controls->sound_epoch += controls->patch.filter_idle_hold;
        patterns->studio_cues |= CUE_TELEGRAM;
    }
}

static unsigned dillo_studio_sound_count(void)
{
    return sizeof studio_dillo_sounds / sizeof studio_dillo_sounds[0];
}

static const bb_dillo_patch *dillo_studio_sound(unsigned at)
{
    return &studio_dillo_sounds[at];
}

/* Named after the first Studio pattern that was written for it. */
static const char *dillo_studio_sound_name(unsigned at)
{
    for (unsigned i = 0; i < sizeof studio_dillo_index; i++)
        if (studio_dillo_index[i] == at) return factory_dillo_phrases[studio_dillo_start + i].name;
    return "STUDIO";
}

#include "dillo_pattern_sounds.inc"
/* Freestanding PI32 links without libc string functions. */
static int dillo_name_differs(const char *a, const char *b, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        if (a[i] != b[i]) return 1;
        if (!a[i]) return 0;
    }
    return 0;
}
/* The sound an acid pattern was paired with, one-based; zero when unpaired or the bank moved. */
static unsigned dillo_pattern_sound(unsigned phrase)
{
    unsigned lo = 0, hi = sizeof dillo_pattern_sounds / sizeof dillo_pattern_sounds[0];
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2u;
        if (dillo_pattern_sounds[mid].phrase < phrase) lo = mid + 1u; else hi = mid;
    }
    if (lo == sizeof dillo_pattern_sounds / sizeof dillo_pattern_sounds[0] || dillo_pattern_sounds[lo].phrase != phrase ||
        dillo_name_differs(dillo_pattern_sounds[lo].name, factory_dillo_phrases[phrase].name, TG_NAME_BYTES) ||
        dillo_pattern_sounds[lo].slot >= dillo_sound_total())
        return 0;
    return dillo_pattern_sounds[lo].slot + 1u;
}

static void conductor_finish_studio(struct panel *panel)
{
    if (panel->dillo_sound_cue && !panel->studio_dillo_pending && panel->dillo_applied == panel->dillo_cue) {
        panel->dillo_sound_slot = (uint8_t)(panel->dillo_sound_cue - 1u);
        panel->dillo_sound = dillo_saved_sound(panel->dillo_sound_slot);
        panel->dillo_sound_cue = 0;
    }
    if (panel->studio_dillo_pending && panel->dillo_applied == panel->dillo_cue) {
        panel->dillo_sound = (dillo_sound){.tone = *conductor_studio_dillo_patch(panel->studio_dillo_pending - 1u)};
        panel->studio_dillo_pending = 0;
    }
    if (panel->studio_telegram_pending && panel->telegram_applied == panel->telegram_cue) {
        struct patch_snapshot previous = snapshot_patch(panel);
        panel->sound.patch = *conductor_studio_telegram_patch(panel->studio_telegram_pending - 1u);
        panel->sound.sound_epoch += panel->sound.patch.filter_idle_hold;
        panel->studio_sound = panel->studio_telegram_pending;
        panel->studio_telegram_pending = panel->band_sound = 0;
        panel->startup_sound = panel->scene_sound = panel->user_sound = panel->imported_sound = 0;
        panel->compare_active = 0;
        fader_caught = 0;
        remember_patch(panel, &previous);
    }
}

/* The foreground publishes these three generations together. Only the
 * existing working patterns are replaced. Library slots are copied by REC. */
static int conductor_cue_band_preset(struct panel *panel, unsigned index)
{
    if (index >= conductor_band_count()) return 0;
    const struct band_preset *row = &factory_band_presets[index];
    static const tg_saved_phrase rest_lead = {.length = DRUM_STEPS};
    static const dillo_phrase rest_bass = {.length = DRUM_STEPS};
    conductor_stash_pattern(panel, MIX_808);
    conductor_stash_pattern(panel, MIX_DILLO);
    conductor_stash_pattern(panel, MIX_TELEGRAM);
    conductor_replace_pattern(panel, MIX_808, panel->drums_slot, &factory_band_drums[row->drums]);
    conductor_replace_pattern(panel, MIX_DILLO, panel->dillo_slot,
        row->dillo == UINT16_MAX ? &rest_bass : &factory_dillo_phrases[row->dillo].phrase);
    conductor_replace_pattern(panel, MIX_TELEGRAM, panel->telegram_slot,
        row->telegram == UINT16_MAX ? &rest_lead : &factory_telegram_phrases[row->telegram]);
    drum_kit_default(&panel->band_kit);
    static const uint8_t alternate[] = {D8S_LC, D8S_MC, D8S_HC, D8S_CL, D8S_MA};
    for (unsigned bit = 0; bit < sizeof alternate; ++bit)
        if (row->alternate & (1u << bit)) drum_kit_select_sound(&panel->band_kit, alternate[bit]);
    panel->band_preset = (uint16_t)(index + 1u);
    panel->scene_active = 0;
    conductor_changed(panel);
    return 1;
}

static void conductor_band_controls(const struct band_preset_sound *sound, tg_controls *controls)
{
    if (controls->clock.source == TG_CLOCK_INTERNAL) controls->tempo = sound->tempo;
    controls->step_swing = (uint8_t)(sound->swing_x2 - 100u);
    controls->patch = sound->telegram;
    controls->transpose = 0;
    controls->phrase_muted = 0;
}

static void conductor_prepare_band(const struct panel *panel, conductor_patterns *patterns, tg_controls *controls)
{
    const struct band_preset *row = &factory_band_presets[panel->band_preset - 1u];
    const struct band_preset_sound *sound = &factory_band_sounds[row->sound];
    patterns->band_outgoing_tempo = controls->tempo;
    patterns->band_outgoing_swing = controls->step_swing;
    conductor_band_controls(sound, controls);
    patterns->dillo_sound = (dillo_sound){.tone = sound->dillo};
    patterns->kit = panel->band_kit;
    patterns->dillo_on = patterns->drums_on = 1;
}

static void conductor_finish_band(struct panel *panel)
{
    if (!panel->band_preset || panel->drums_applied != panel->drums_cue ||
        panel->dillo_applied != panel->dillo_cue || panel->telegram_applied != panel->telegram_cue) return;
    const struct band_preset *row = &factory_band_presets[panel->band_preset - 1u];
    const struct band_preset_sound *sound = &factory_band_sounds[row->sound];
    conductor_band_controls(sound, &panel->sound);
    panel->dillo_sound = (dillo_sound){.tone = sound->dillo};
    panel->drums_kit = panel->band_kit;
    panel->dillo_enabled = panel->drums_enabled = 1;
    panel->band_sound = panel->band_preset;
    panel->band_preset = panel->studio_sound = 0;
    panel->kit_slot = panel->startup_sound = panel->scene_sound = panel->user_sound = panel->imported_sound = 0;
    panel->compare_active = panel->compare_available = 0;
    fader_caught = 0;
}
#endif

static unsigned synth_preset_sources(const struct panel *panel)
{
    return panel->synth_preset_dillo ?
        sizeof factory_dillo_collections / sizeof factory_dillo_collections[0] :
        sizeof factory_telegram_collections / sizeof factory_telegram_collections[0];
}

#ifdef ELITE_ACID_DUETS
enum { DUET_PADS = 8, DUET_COUNT = sizeof factory_synth_duets / sizeof factory_synth_duets[0] };

/* Acid Duets is the last source in both browsers. Each group can span pages. */
static unsigned synth_preset_duets(const struct panel *panel)
{
    return panel->synth_preset_group == synth_preset_sources(panel);
}

static unsigned duet_groups(void)
{
    return sizeof factory_synth_duet_pages / sizeof factory_synth_duet_pages[0];
}

static unsigned duet_group_start(unsigned page, unsigned *count)
{
    *count = factory_synth_duet_pages[page].count;
    return factory_synth_duet_pages[page].start;
}

/* Both pad rows show the group's parts: one-based duet index, or zero. */
static unsigned duet_at(const struct panel *panel, unsigned pad)
{
    unsigned count, start = duet_group_start(panel->synth_preset_page, &count);
    unsigned part = pad % DUET_PADS;
    return part < count ? start + part + 1u : 0;
}
#else
static unsigned synth_preset_duets(const struct panel *panel) { (void)panel; return 0; }
#endif

static unsigned synth_preset_groups(const struct panel *panel)
{
#ifdef ELITE_ACID_DUETS
    return synth_preset_sources(panel) + 1u;
#else
    return synth_preset_sources(panel);
#endif
}

static const struct synth_preset_collection *synth_preset_group(const struct panel *panel)
{
    return &(panel->synth_preset_dillo ? factory_dillo_collections :
                                       factory_telegram_collections)[panel->synth_preset_group];
}

static unsigned synth_preset_pages(const struct panel *panel)
{
#ifdef ELITE_ACID_DUETS
    if (synth_preset_duets(panel)) return duet_groups();
#endif
    return (synth_preset_group(panel)->count + TG_PADS - 1u) / TG_PADS;
}

static unsigned synth_preset_at(const struct panel *panel, unsigned pad)
{
    if (synth_preset_duets(panel)) return 0;
    const struct synth_preset_collection *group = synth_preset_group(panel);
    unsigned offset = panel->synth_preset_page * TG_PADS + pad;
    if (offset >= group->count) return 0;
    unsigned index = group->start + offset;
#ifdef ELITE_SYNTH_BROWSE_INDEX
    index = (panel->synth_preset_dillo ? factory_dillo_browse : factory_telegram_browse)[index];
#endif
    return index + 1u;
}

/* PATTERNS pages run on through every source in order, so no button is needed to change
 * source: past a source's last page comes the next source's first. Duets page by group. */
static void synth_preset_step(struct panel *panel, int delta)
{
    if (synth_preset_duets(panel)) {
        unsigned pages = synth_preset_pages(panel);
        panel->synth_preset_page = (uint8_t)((panel->synth_preset_page + pages + (unsigned)(delta % (int)pages)) % pages);
        return;
    }
    unsigned sources = synth_preset_sources(panel);
    for (; delta > 0; delta--) {
        if (panel->synth_preset_page + 1u < synth_preset_pages(panel)) panel->synth_preset_page++;
        else {
            panel->synth_preset_group = (uint8_t)((panel->synth_preset_group + 1u) % sources);
            panel->synth_preset_page = 0;
        }
    }
    for (; delta < 0; delta++) {
        if (panel->synth_preset_page) panel->synth_preset_page--;
        else {
            panel->synth_preset_group = (uint8_t)((panel->synth_preset_group + sources - 1u) % sources);
            panel->synth_preset_page = (uint8_t)(synth_preset_pages(panel) - 1u);
        }
    }
}

static void open_synth_presets(struct panel *panel, unsigned dillo)
{
    if (panel->synth_preset_dillo != dillo) {
        panel->synth_preset_group = 0;
        panel->synth_preset_page = 0;
    }
    panel->synth_preset_selected = 0;
    panel->synth_duet_selected = 0;
    panel->synth_cursor = panel->synth_info = 0;
    panel->synth_preset_return = panel->page;
    panel->synth_preset_dillo = (uint8_t)dillo;
    panel->page = PAGE_SYNTH_PRESETS;
    panel->globe_tap_pending = 0;
}

static void conductor_synth_preset_pad(struct panel *panel, unsigned pad)
{
    unsigned index = synth_preset_at(panel, pad);
    if (!index) return;
    if (panel->synth_preset_dillo) {
        conductor_stash_pattern(panel, MIX_DILLO);
        conductor_replace_pattern(panel, MIX_DILLO, panel->dillo_slot, &factory_dillo_phrases[index - 1u].phrase);
#ifdef ELITE_BAND_PRESETS
        if (conductor_studio_dillo_patch(index - 1u)) panel->studio_dillo_pending = (uint16_t)index;
        else panel->dillo_sound_cue = (uint8_t)dillo_pattern_sound(index - 1u);
#endif
    } else {
        conductor_stash_pattern(panel, MIX_TELEGRAM);
        conductor_replace_pattern(panel, MIX_TELEGRAM, panel->telegram_slot, &factory_telegram_phrases[index - 1u]);
#ifdef ELITE_BAND_PRESETS
        if (conductor_studio_telegram_patch(index - 1u)) panel->studio_telegram_pending = (uint16_t)index;
#endif
        panel->sound.transpose = 0;
    }
    panel->synth_preset_selected = (uint16_t)index;
    panel->scene_active = 0;
    conductor_changed(panel);
}
/* ---- Jukebox on the 303 and 101 shelves: K1 spins one entry at a time, across pages and
 * sources; PARA (INFO) in the Library turns the card. ---- */
static unsigned synth_page_count(const struct panel *panel)
{
#ifdef ELITE_ACID_DUETS
    if (synth_preset_duets(panel)) {
        unsigned count;
        duet_group_start(panel->synth_preset_page, &count);
        return count;
    }
#endif
    unsigned count = 0;
    while (count < TG_PADS && synth_preset_at(panel, count)) count++;
    return count;
}

/* The entry under the cursor: a phrase or duet index plus one. */
static unsigned synth_selected(const struct panel *panel)
{
    unsigned count = synth_page_count(panel);
    if (!count) return 0;
    unsigned pad = panel->synth_cursor < count ? panel->synth_cursor : count - 1u;
#ifdef ELITE_ACID_DUETS
    if (synth_preset_duets(panel)) return duet_at(panel, pad);
#endif
    return synth_preset_at(panel, pad);
}

static void synth_spin(struct panel *panel, int delta)
{
    unsigned count = synth_page_count(panel);
    if (panel->synth_cursor >= count) panel->synth_cursor = (uint8_t)(count ? count - 1u : 0);
    for (unsigned guard = 0; delta > 0 && guard < 4096u; guard++, delta--) {
        if (panel->synth_cursor + 1u < synth_page_count(panel)) panel->synth_cursor++;
        else { synth_preset_step(panel, 1); panel->synth_cursor = 0; }
    }
    for (unsigned guard = 0; delta < 0 && guard < 4096u; guard++, delta++) {
        if (panel->synth_cursor) panel->synth_cursor--;
        else {
            synth_preset_step(panel, -1);
            unsigned n = synth_page_count(panel);
            panel->synth_cursor = (uint8_t)(n ? n - 1u : 0);
        }
    }
}

static const char *synth_entry_name(const struct panel *panel, unsigned index)
{
#ifdef ELITE_ACID_DUETS
    if (synth_preset_duets(panel)) return factory_dillo_phrases[factory_synth_duets[index].bass].name;
#endif
    return panel->synth_preset_dillo ? factory_dillo_phrases[index].name : factory_telegram_phrases[index].name;
}

/* One 16-step row: LANE_* per step and its pitch (0 for none). */
static void synth_phrase_row(unsigned dillo, unsigned phrase, uint8_t lane[DRUM_STEPS], uint8_t pitch[DRUM_STEPS])
{
    memset(lane, 0, DRUM_STEPS);
    memset(pitch, 0, DRUM_STEPS);
    if (dillo) {
        const dillo_phrase *p = &factory_dillo_phrases[phrase].phrase;
        unsigned last = 0;
        for (unsigned step = 0; step < DRUM_STEPS && step < p->length; step++) {
            const dillo_step *e = &p->steps[step];
            if (e->event == DILLO_NOTE) last = e->pitch;
            lane[step] = e->event == DILLO_NOTE ? (e->flags & DILLO_ACCENT ? LANE_ACCENT : LANE_HIT) :
                         e->event == DILLO_TIE ? LANE_HOLD : LANE_OFF;
            pitch[step] = e->event == DILLO_REST ? 0 : (uint8_t)last;
        }
    } else {
        const tg_saved_phrase *p = &factory_telegram_phrases[phrase];
        for (unsigned step = 0; step < DRUM_STEPS && step < p->length; step++) {
            unsigned v = p->steps[step];
            lane[step] = !v ? LANE_OFF : v & 128u ? LANE_HOLD : LANE_HIT;
            pitch[step] = (uint8_t)(v & 127u);
        }
    }
}

/* The pitch classes a phrase uses, its first note as root, and how it sits on the beat. */
struct synth_notes { uint16_t used; uint8_t root, on_one, notes, slides; };
static void synth_phrase_notes(unsigned dillo, unsigned phrase, struct synth_notes *out)
{
    unsigned have = out->notes != 0;
    if (dillo) {
        const dillo_phrase *p = &factory_dillo_phrases[phrase].phrase;
        for (unsigned step = 0; step < p->length && step < DILLO_STEPS; step++) {
            const dillo_step *e = &p->steps[step];
            if (e->event != DILLO_NOTE) continue;
            out->used |= (uint16_t)(1u << (e->pitch % 12u));
            if (!have) { out->root = (uint8_t)(e->pitch % 12u); have = 1; }
            if (!step) out->on_one = 1;
            if (e->flags & DILLO_SLIDE) out->slides++;
            out->notes++;
        }
    } else {
        const tg_saved_phrase *p = &factory_telegram_phrases[phrase];
        for (unsigned step = 0; step < p->length && step < TG_STEPS; step++) {
            unsigned v = p->steps[step];
            if (!v || (v & 128u)) continue;
            out->used |= (uint16_t)(1u << (v % 12u));
            if (!have) { out->root = (uint8_t)(v % 12u); have = 1; }
            if (!step) out->on_one = 1;
            if (out->notes < 255u) out->notes++;
        }
    }
}

#ifdef ELITE_ACID_DUETS
/* One foreground publication carries both phrase cues. Sounds and levels are
 * audition metadata, so selecting a pair does not interrupt the outgoing tone. */
static int conductor_cue_duet(struct panel *panel, unsigned index)
{
    if (index >= sizeof factory_synth_duets / sizeof factory_synth_duets[0]) return 0;
    conductor_stash_pattern(panel, MIX_DILLO);
    conductor_stash_pattern(panel, MIX_TELEGRAM);
    conductor_replace_pattern(panel, MIX_DILLO, panel->dillo_slot,
                              &factory_dillo_phrases[factory_synth_duets[index].bass].phrase);
    conductor_replace_pattern(panel, MIX_TELEGRAM, panel->telegram_slot,
                              &factory_telegram_phrases[factory_synth_duets[index].lead]);
    panel->sound.transpose = 0;
    panel->scene_active = 0;
    conductor_changed(panel);
    return 1;
}

/* Acid Duets pads. Pads 1-8 cue the part's bass and lead together at the next bar,
 * with the lead's own patch and level; CHORD keeps the current sound and mix.
 * Pads 9-16 load only this browser's half of the part. Stopped on internal clock,
 * any pad starts the transport so the choice is heard at once. Keyboard focus
 * never moves. REC + a Band pad keeps a copy, as for any phrase. */
static void conductor_duet_pad(struct panel *panel, unsigned pad, unsigned keep_sound)
{
    unsigned index = duet_at(panel, pad);
    if (!index) return;
    unsigned duet = index - 1u;
    if (pad < DUET_PADS) {
        conductor_cue_duet(panel, duet);
        if (!keep_sound) {
#ifdef ELITE_BAND_PRESETS
            panel->dillo_sound_cue = (uint8_t)dillo_pattern_sound(factory_synth_duets[duet].bass);
#endif
            struct patch_snapshot previous = snapshot_patch(panel);
            select_preset(panel, factory_synth_duets[duet].patch);
            remember_patch(panel, &previous);
            panel->mixer.value[MIX_TELEGRAM][MIX_LEVEL] = factory_synth_duets[duet].level;
        }
    } else if (panel->synth_preset_dillo) {
        conductor_stash_pattern(panel, MIX_DILLO);
        conductor_replace_pattern(panel, MIX_DILLO, panel->dillo_slot,
                                  &factory_dillo_phrases[factory_synth_duets[duet].bass].phrase);
#ifdef ELITE_BAND_PRESETS
        if (!keep_sound) panel->dillo_sound_cue = (uint8_t)dillo_pattern_sound(factory_synth_duets[duet].bass);
#endif
    } else {
        conductor_stash_pattern(panel, MIX_TELEGRAM);
        conductor_replace_pattern(panel, MIX_TELEGRAM, panel->telegram_slot,
                                  &factory_telegram_phrases[factory_synth_duets[duet].lead]);
        panel->sound.transpose = 0;
    }
    if (pad >= DUET_PADS) {
        panel->scene_active = 0;
        conductor_changed(panel);
    }
    /* Whatever was just chosen is heard: a muted part comes back. */
    if (pad < DUET_PADS || panel->synth_preset_dillo) panel->dillo_enabled = 1;
    if (pad < DUET_PADS || !panel->synth_preset_dillo) panel->sound.phrase_muted = 0;
    if (!panel->sound.running && panel->sound.clock.source != TG_CLOCK_USB) panel->sound.running = 1;
    panel->synth_duet_selected = (uint8_t)index;
}
#endif
#endif

/* One sixteen-name page serves three read-only lists: the Studio 808 rhythms, the 727
 * LATIN layer rhythms (B's transport, LATIN row order) and the BB1 Band presets (all
 * three parts at the downbeat). A Band page never mixes two song or era groups. */
enum { RHYTHM_STUDIO, RHYTHM_LAYER, RHYTHM_BAND };
#ifdef ELITE_PERCUSSION_727
#include "layer_rhythms.inc"
#endif

#ifdef ELITE_BAND_PRESETS
/* Band page -> first preset and count on it; the page's group through *group. */
static unsigned band_page_rows(unsigned page, unsigned *count, unsigned *group)
{
    for (unsigned g = 0; g < conductor_band_groups(); g++) {
        unsigned rows = factory_band_groups[g].count;
        unsigned pages = (rows + DRUM_PRESET_PAGE - 1u) / DRUM_PRESET_PAGE;
        if (page < pages) {
            unsigned first = page * DRUM_PRESET_PAGE;
            *count = rows - first < DRUM_PRESET_PAGE ? rows - first : DRUM_PRESET_PAGE;
            if (group) *group = g;
            return factory_band_groups[g].start + first;
        }
        page -= pages;
    }
    *count = 0;
    return 0;
}

static unsigned band_pages(void)
{
    unsigned pages = 0;
    for (unsigned g = 0; g < conductor_band_groups(); g++)
        pages += (factory_band_groups[g].count + DRUM_PRESET_PAGE - 1u) / DRUM_PRESET_PAGE;
    return pages;
}

static unsigned band_page_of(unsigned index)
{
    for (unsigned page = 0, count; page < band_pages(); page++) {
        unsigned start = band_page_rows(page, &count, NULL);
        if (index >= start && index < start + count) return page;
    }
    return 0;
}
#endif

static unsigned rhythm_banks(const struct panel *panel)
{
    switch (panel->rhythm_source) {
#ifdef ELITE_PERCUSSION_727
    case RHYTHM_LAYER: return (LAYER_RHYTHM_COUNT + DRUM_PRESET_PAGE - 1) / DRUM_PRESET_PAGE;
#endif
#ifdef ELITE_BAND_PRESETS
    case RHYTHM_BAND: return band_pages();
#endif
    default: return DRUM_PRESET_BANKS;
    }
}

/* The list entry under a pad, plus one; zero for an empty pad. */
static unsigned rhythm_at(const struct panel *panel, unsigned pad)
{
    unsigned index = panel->drum_preset_bank * DRUM_PRESET_PAGE + pad;
    switch (panel->rhythm_source) {
#ifdef ELITE_PERCUSSION_727
    case RHYTHM_LAYER: return index < LAYER_RHYTHM_COUNT ? index + 1u : 0;
#endif
#ifdef ELITE_BAND_PRESETS
    case RHYTHM_BAND: {
        unsigned count, start = band_page_rows(panel->drum_preset_bank, &count, NULL);
        return pad < count ? start + pad + 1u : 0;
    }
#endif
    default: return index < DRUM_PRESET_COUNT ? index + 1u : 0;
    }
}

static const char *rhythm_name(const struct panel *panel, unsigned index)
{
    switch (panel->rhythm_source) {
#ifdef ELITE_PERCUSSION_727
    case RHYTHM_LAYER: return layer_rhythm_names[index];
#endif
#ifdef ELITE_BAND_PRESETS
    case RHYTHM_BAND: return factory_band_presets[index].name;
#endif
    default: return drum_preset_names[index];
    }
}

/* The Band page's song or era; NULL for the rhythm lists. */
static const char *rhythm_group(const struct panel *panel)
{
#ifdef ELITE_BAND_PRESETS
    if (panel->rhythm_source == RHYTHM_BAND) {
        unsigned count, group = 0;
        band_page_rows(panel->drum_preset_bank, &count, &group);
        return factory_band_groups[group].name;
    }
#else
    (void)panel;
#endif
    return NULL;
}

/* Playing unchanged (rhythms), or last loaded (Band), plus one. */
static unsigned rhythm_playing(const struct panel *panel)
{
    switch (panel->rhythm_source) {
#ifdef ELITE_PERCUSSION_727
    case RHYTHM_LAYER:
        for (unsigned i = 0; i < LAYER_RHYTHM_COUNT; i++)
            if (!memcmp(&panel->drums_b.pattern, &layer_rhythms[i], sizeof layer_rhythms[i])) return i + 1u;
        return 0;
#endif
#ifdef ELITE_BAND_PRESETS
    case RHYTHM_BAND: return panel->band_preset ? panel->band_preset : panel->band_sound;
#endif
    default: return drum_preset_playing(panel);
    }
}

/* Waiting for the downbeat (Band only), plus one. */
static unsigned rhythm_pending(const struct panel *panel)
{
#ifdef ELITE_BAND_PRESETS
    if (panel->rhythm_source == RHYTHM_BAND) return panel->band_preset;
#else
    (void)panel;
#endif
    return 0;
}

static void open_rhythms(struct panel *panel, unsigned source)
{
    panel->rhythm_source = (uint8_t)source;
    unsigned playing = rhythm_playing(panel);
    panel->drum_preset_bank = 0;
#ifdef ELITE_BAND_PRESETS
    if (source == RHYTHM_BAND) {
        if (playing) panel->drum_preset_bank = (uint8_t)band_page_of(playing - 1u);
    } else
#endif
    if (playing) panel->drum_preset_bank = (uint8_t)((playing - 1u) / DRUM_PRESET_PAGE);
    panel->page = PAGE_DRUM_PRESETS;
    panel->rhythm_cursor = 0;
    panel->rhythm_info = 0;
    for (unsigned pad = 0; playing && pad < DRUM_PRESET_PAGE; pad++)
        if (rhythm_at(panel, pad) == playing) panel->rhythm_cursor = (uint8_t)pad;
}

static void rhythm_pad(struct panel *panel, unsigned pad)
{
    unsigned index = rhythm_at(panel, pad);
    if (!index) return;
    switch (panel->rhythm_source) {
#ifdef ELITE_PERCUSSION_727
    case RHYTHM_LAYER: conductor_cue_layer_rhythm(panel, &layer_rhythms[index - 1u]); break;
#endif
#ifdef ELITE_BAND_PRESETS
    case RHYTHM_BAND:
        if (!conductor_cue_band_preset(panel, index - 1u)) return;
        /* Browsing is heard at once: stopped on internal clock, a pad starts the band. */
        if (!panel->sound.running && panel->sound.clock.source != TG_CLOCK_USB) panel->sound.running = 1;
        break;
#endif
    default: conductor_drum_preset_pad(panel, pad); break;
    }
    panel->rhythm_cursor = (uint8_t)pad;
    panel->rhythm_info = 1; /* It lands on what it is. */
}

/* ---- Preset jukebox: K1 spins one entry at a time across pages; PARA (INFO) turns the
 * selected entry's card from the list to what it is, then to how to play it. ---- */
enum { JUKEBOX_LIST, JUKEBOX_ABOUT, JUKEBOX_PLAY, JUKEBOX_VIEWS };

static unsigned rhythm_total(const struct panel *panel)
{
    switch (panel->rhythm_source) {
#ifdef ELITE_PERCUSSION_727
    case RHYTHM_LAYER: return LAYER_RHYTHM_COUNT;
#endif
#ifdef ELITE_BAND_PRESETS
    case RHYTHM_BAND: return conductor_band_count();
#endif
    default: return DRUM_PRESET_COUNT;
    }
}

static unsigned rhythm_page_count(const struct panel *panel)
{
    unsigned count = 0;
    while (count < DRUM_PRESET_PAGE && rhythm_at(panel, count)) count++;
    return count;
}

/* The entry under the cursor, plus one; entries are numbered in list order across pages. */
static unsigned rhythm_selected(const struct panel *panel)
{
    unsigned count = rhythm_page_count(panel);
    if (!count) return 0;
    return rhythm_at(panel, panel->rhythm_cursor < count ? panel->rhythm_cursor : count - 1u);
}

static void rhythm_spin(struct panel *panel, int delta)
{
    unsigned banks = rhythm_banks(panel);
    unsigned count = rhythm_page_count(panel);
    if (panel->rhythm_cursor >= count) panel->rhythm_cursor = (uint8_t)(count ? count - 1u : 0);
    for (; delta > 0; delta--) {
        if (panel->rhythm_cursor + 1u < rhythm_page_count(panel)) panel->rhythm_cursor++;
        else if (panel->drum_preset_bank + 1u < banks) { panel->drum_preset_bank++; panel->rhythm_cursor = 0; }
        else break;
    }
    for (; delta < 0; delta++) {
        if (panel->rhythm_cursor) panel->rhythm_cursor--;
        else if (panel->drum_preset_bank) {
            panel->drum_preset_bank--;
            panel->rhythm_cursor = (uint8_t)(rhythm_page_count(panel) - 1u);
        } else break;
    }
}

static const drum_pattern *rhythm_pattern(const struct panel *panel, unsigned index)
{
    switch (panel->rhythm_source) {
#ifdef ELITE_PERCUSSION_727
    case RHYTHM_LAYER: return &layer_rhythms[index];
#endif
#ifdef ELITE_BAND_PRESETS
    case RHYTHM_BAND: return &factory_band_drums[factory_band_presets[index].drums];
#endif
    default: return &drum_presets[index];
    }
}

/* Three sixteen-step rows that show an entry's shape. A Band preset shows its 808, 303 and
 * 101; a rhythm shows its low, middle and high drums. */
static void rhythm_lanes(const struct panel *panel, unsigned index, uint8_t lane[3][DRUM_STEPS])
{
    memset(lane, 0, 3u * DRUM_STEPS);
    const drum_pattern *pattern = rhythm_pattern(panel, index);
    static const uint8_t studio_row[D8_NUM] = {0, 1, 0, 1, 2, 1, 1, 2, 2, 2, 2};
    static const uint8_t latin_row[D8_NUM] = {0, 2, 2, 1, 2, 0, 1, 1, 0, 2, 0};
    unsigned band = 0;
#ifdef ELITE_BAND_PRESETS
    band = panel->rhythm_source == RHYTHM_BAND;
#endif
    for (unsigned t = 0; t < D8_NUM; t++)
        for (unsigned step = 0; step < DRUM_STEPS; step++) {
            if (!(pattern->hits[t] >> step & 1u)) continue;
            unsigned row = band ? 0 : panel->rhythm_source == RHYTHM_LAYER ? latin_row[t] : studio_row[t];
            unsigned v = pattern->accents[t] >> step & 1u ? LANE_ACCENT : LANE_HIT;
            if (lane[row][step] < v) lane[row][step] = (uint8_t)v;
        }
#ifdef ELITE_BAND_PRESETS
    if (!band) return;
    const struct band_preset *row = &factory_band_presets[index];
    if (row->dillo != UINT16_MAX) {
        const dillo_phrase *bass = &factory_dillo_phrases[row->dillo].phrase;
        for (unsigned step = 0; step < DRUM_STEPS && step < bass->length; step++) {
            const dillo_step *e = &bass->steps[step];
            lane[1][step] = e->event == DILLO_NOTE ? (e->flags & DILLO_ACCENT ? LANE_ACCENT : LANE_HIT) :
                            e->event == DILLO_TIE ? LANE_HOLD : LANE_OFF;
        }
    }
    if (row->telegram != UINT16_MAX) {
        const tg_saved_phrase *lead = &factory_telegram_phrases[row->telegram];
        for (unsigned step = 0; step < DRUM_STEPS && step < lead->length; step++)
            lane[2][step] = !lead->steps[step] ? LANE_OFF : lead->steps[step] & 128u ? LANE_HOLD : LANE_HIT;
    }
#endif
}

#ifdef ELITE_BAND_PRESETS
/* How to play a Band preset: the pitch classes its 303 and 101 use, the bass's first note as
 * the root, and the two facts a player needs most. */
struct band_notes { uint16_t used; uint8_t root, bass_on_one, lead_notes; };
static struct band_notes band_preset_notes(unsigned index)
{
    struct band_notes notes = {0, 0, 0, 0};
    const struct band_preset *row = &factory_band_presets[index];
    unsigned have_root = 0;
    if (row->dillo != UINT16_MAX) {
        const dillo_phrase *bass = &factory_dillo_phrases[row->dillo].phrase;
        for (unsigned step = 0; step < bass->length && step < DILLO_STEPS; step++) {
            if (bass->steps[step].event != DILLO_NOTE) continue;
            unsigned pc = bass->steps[step].pitch % 12u;
            notes.used |= (uint16_t)(1u << pc);
            if (!have_root) { notes.root = (uint8_t)pc; have_root = 1; }
            if (!step) notes.bass_on_one = 1;
        }
    }
    if (row->telegram != UINT16_MAX) {
        const tg_saved_phrase *lead = &factory_telegram_phrases[row->telegram];
        for (unsigned step = 0; step < lead->length && step < TG_STEPS; step++) {
            unsigned v = lead->steps[step];
            if (!v || (v & 128u)) continue;
            notes.used |= (uint16_t)(1u << (v % 12u));
            if (!have_root) { notes.root = (uint8_t)(v % 12u); have_root = 1; }
            if (notes.lead_notes < 255u) notes.lead_notes++;
        }
    }
    return notes;
}
#endif

/* The pending cue on this list, if any, so the screen can count it in. */
static unsigned rhythm_cue_waiting(const struct panel *panel)
{
#ifdef ELITE_BAND_PRESETS
    if (panel->rhythm_source == RHYTHM_BAND) return panel->band_preset != 0;
#endif
    if (panel->rhythm_source == RHYTHM_LAYER) return panel->drums_b.cue != panel->drums_b_applied;
    return panel->drums_cue != panel->drums_applied;
}

/* The instrument picker behind BT: one pad per instrument, keys follow the synth chosen. */
#if ELITE_DRUM_PARTS > 1
enum { PICK_TELEGRAM, PICK_DILLO, PICK_808_A, PICK_808_B, PICK_BAND, PICK_COUNT };
#else
enum { PICK_TELEGRAM, PICK_DILLO, PICK_808_A, PICK_BAND, PICK_COUNT, PICK_808_B }; /* B has no pad */
#endif

static unsigned instrument_of_page(unsigned page, const struct panel *panel)
{
    switch (page) {
    case PAGE_DILLO: case PAGE_DILLO_SOUND: return PICK_DILLO;
#ifdef ELITE_SYNTH_PRESETS
    case PAGE_SYNTH_PRESETS: return panel->synth_preset_dillo ? PICK_DILLO : PICK_TELEGRAM;
#endif
    case PAGE_DRUM_PRESETS: if (panel->rhythm_source == RHYTHM_BAND) return PICK_BAND; /* fall through */
    case PAGE_DRUMS: case PAGE_KITS: return drum_part(panel) ? PICK_808_B : PICK_808_A;
    case PAGE_CONDUCTOR: case PAGE_MIX: case PAGE_SCENES: return PICK_BAND;
    default: return PICK_TELEGRAM;
    }
}

static void pick_instrument(struct panel *panel, unsigned pick, uint32_t frames)
{
    switch (pick) {
    case PICK_TELEGRAM:
        panel->page = PAGE_TONE;
        screen_focus_keys(panel, 0, frames);
        break;
    case PICK_DILLO:
        if (!panel->dillo_pattern.length) {
            panel->dillo_pattern = dillo_demo;
            panel->dillo_undo = UNDO_NONE;
            panel->dillo_enabled = 1;
        }
        panel->page = PAGE_DILLO;
        screen_focus_keys(panel, 1, frames);
        break;
    case PICK_808_A:
    case PICK_808_B:
        if (ELITE_DRUM_PARTS > 1 && drum_part(panel) != (pick == PICK_808_B)) {
            panel->drums_focus = (uint8_t)(pick == PICK_808_B);
            conductor_changed(panel);
        }
        open_drum_editor(panel, 1);
        break;
    case PICK_BAND:
        panel->page = PAGE_CONDUCTOR;
        break;
    default:
        return;
    }
}

enum { KIT_NORMAL, KIT_CAPTURED, KIT_RECALLED, KIT_EMPTY };

static drum_kit conductor_get_kit(unsigned slot)
{
    if (slot) return conductor_kits[slot - 1u];
    drum_kit kit;
    drum_kit_default(&kit);
    return kit;
}

static int conductor_kit_edited(const struct panel *panel)
{
    drum_kit saved = conductor_get_kit(panel->kit_slot);
    return memcmp(&saved, &panel->drums_kit, sizeof saved) != 0;
}

static void conductor_store_kit(struct panel *panel, unsigned slot, unsigned part_b)
{
    conductor_kits[slot - 1u] = part_b ? panel->drums_b.kit : panel->drums_kit;
    panel->kit_slots |= (uint8_t)(1u << (slot - 1u));
    if (part_b) panel->drums_b_kit_slot = (uint8_t)slot;
    else panel->kit_slot = (uint8_t)slot;
    panel->kit_status = KIT_CAPTURED;
    conductor_changed(panel);
}

static void conductor_kit_pad(struct panel *panel, unsigned slot, unsigned store)
{
    if (slot > DRUM_USER_KITS || (!slot && store)) return;
    drum_kit *kit = drum_part(panel) ? &panel->drums_b.kit : &panel->drums_kit;
    uint8_t *current = drum_part(panel) ? &panel->drums_b_kit_slot : &panel->kit_slot;
    if (store) {
        conductor_store_kit(panel, slot, drum_part(panel));
    } else if (!conductor_kit_exists(panel->kit_slots, slot)) {
        panel->kit_status = KIT_EMPTY;
    } else {
        *kit = conductor_get_kit(slot);
        *current = (uint8_t)slot;
        panel->kit_status = KIT_RECALLED;
        conductor_changed(panel);
    }
}

static dillo_sound dillo_saved_sound(unsigned slot)
{
    if (slot < BB_DILLO_PATCHES) return (dillo_sound){.tone = bb_dillo_bank[slot].sound};
    if (slot < DILLO_SOUND_PAGE_SLOTS) return conductor_dillo_sounds[slot - BB_DILLO_PATCHES];
    if (slot < DILLO_CLASSIC_START) return dillo_expansion[slot - DILLO_SOUND_PAGE_SLOTS].sound;
    if (slot < DILLO_DEVIL_START) return dillo_classic[slot - DILLO_CLASSIC_START].sound;
    if (slot < DILLO_SOUND_SLOTS) return dillo_devilfish[slot - DILLO_DEVIL_START].sound;
    return (dillo_sound){.tone = *dillo_studio_sound(slot - DILLO_SOUND_SLOTS)};
}

static int dillo_sound_edited(const struct panel *panel)
{
    dillo_sound saved = dillo_saved_sound(panel->dillo_sound_slot);
    return memcmp(&saved, &panel->dillo_sound, sizeof saved) != 0;
}

static void conductor_dillo_sound_pad(struct panel *panel, unsigned slot, unsigned store)
{
    if (slot >= dillo_sound_total() || (store && (slot < BB_DILLO_PATCHES || slot >= DILLO_SOUND_PAGE_SLOTS)))
        return;
    if (store) {
        unsigned user = slot - BB_DILLO_PATCHES;
        conductor_dillo_sounds[user] = panel->dillo_sound;
        panel->dillo_sound_slots |= (uint8_t)(1u << user);
        panel->dillo_sound_status = KIT_CAPTURED;
    } else if (!conductor_dillo_sound_exists(panel->dillo_sound_slots, slot)) {
        panel->dillo_sound_status = KIT_EMPTY;
        return;
    } else {
        panel->dillo_sound = dillo_saved_sound(slot);
        panel->dillo_sound_status = KIT_RECALLED;
    }
    panel->dillo_sound_slot = (uint8_t)slot;
    panel->conductor_edits++;
    panel->conductor_dirty = 1;
}

static void open_dillo_sound(struct panel *panel)
{
    panel->dillo_sound_home = panel->page == PAGE_CONDUCTOR;
    panel->dillo_sound_bank = (uint8_t)(panel->dillo_sound_slot < dillo_sound_total() ?
                                        panel->dillo_sound_slot / DILLO_SOUND_PAGE_SLOTS : 0);
    panel->page = PAGE_DILLO_SOUND;
    panel->dillo_sound_status = KIT_NORMAL;
}

static int conductor_save_page(unsigned page)
{
    return page == PAGE_CONDUCTOR || page == PAGE_SCENES || page == PAGE_KITS || page == PAGE_DILLO_SOUND;
}

static int conductor_selection_grid(unsigned page)
{
    return page == PAGE_CONDUCTOR || page == PAGE_MIX || page == PAGE_SCENES || page == PAGE_KITS ||
           page == PAGE_DILLO_SOUND || page == PAGE_PATCH || page == PAGE_DRUM_PRESETS ||
           page == PAGE_INSTRUMENTS || page == PAGE_SYNTH_PRESETS;
}

static unsigned conductor_free_slot(unsigned used, unsigned limit)
{
    for (unsigned slot = 1; slot < limit; slot++)
        if (!(used & (1u << slot))) return slot;
    return 0;
}

static unsigned conductor_plan_kit(const drum_kit *working, unsigned saved, unsigned *reserved)
{
    for (unsigned slot = 0; slot <= DRUM_USER_KITS; slot++) {
        if (!conductor_kit_exists(saved, slot)) continue;
        drum_kit kit = conductor_get_kit(slot);
        if (!memcmp(&kit, working, sizeof kit)) return slot;
    }
    unsigned slot = conductor_free_slot(*reserved, DRUM_USER_KITS + 1u);
    if (!slot) return DRUM_USER_KITS + 1u;
    *reserved |= 1u << slot;
    return slot;
}

static unsigned conductor_plan_dillo_sound(const struct panel *panel)
{
    for (unsigned slot = 0; slot < DILLO_SOUND_SLOTS; slot++) {
        if (!conductor_dillo_sound_exists(panel->dillo_sound_slots, slot)) continue;
        dillo_sound saved = dillo_saved_sound(slot);
        if (!memcmp(&saved, &panel->dillo_sound, sizeof saved)) return slot;
    }
    unsigned user = conductor_free_slot((unsigned)panel->dillo_sound_slots << 1, DILLO_USER_SOUNDS + 1u);
    return user ? BB_DILLO_PATCHES + user - 1u : DILLO_SLOT_NONE;
}

static void conductor_bind_dillo_sound(struct panel *panel, unsigned slot)
{
    if (conductor_dillo_sound_exists(panel->dillo_sound_slots, slot)) panel->dillo_sound_slot = (uint8_t)slot;
    else conductor_dillo_sound_pad(panel, slot, 1);
}

/* Plan all allocations before making any changes to the banks. */
static int conductor_fork_variation(struct panel *panel)
{
    const uint16_t used[] = {[MIX_808] = panel->drums_slots, [MIX_808_B] = panel->drums_b_slots,
        [MIX_DILLO] = panel->dillo_slots, [MIX_TELEGRAM] = panel->telegram_slots};
    unsigned capture_b_pattern = ELITE_DRUM_PARTS > 1;
#ifdef ELITE_PERCUSSION_727
    capture_b_pattern |= panel->layer_available;
#endif
    uint8_t pattern[MIX_AUDIO_LANES] = {0};
    for (unsigned lane = 0; lane < MIX_AUDIO_LANES; lane++) {
        if ((lane == MIX_808_B && !capture_b_pattern) || !conductor_pattern_edited(panel, lane)) continue;
        unsigned slot = 0;
        for (; slot < CONDUCTOR_SLOTS; slot++)
            if (((used[lane] | 1u) & (1u << slot)) && !conductor_pattern_differs(panel, lane, slot)) break;
        if (slot == CONDUCTOR_SLOTS) {
            slot = conductor_free_slot(used[lane], CONDUCTOR_SLOTS);
            if (!slot) {
                panel->scene_status = SCENE_PATTERN_FULL;
                return 0;
            }
        }
        pattern[lane] = (uint8_t)(slot + 1u);
    }

    unsigned kits = (unsigned)panel->kit_slots << 1;
    unsigned kit_a = panel->kit_slot, kit_b = panel->drums_b_kit_slot, dillo = panel->dillo_sound_slot;
    if (conductor_kit_edited(panel))
        kit_a = conductor_plan_kit(&panel->drums_kit, panel->kit_slots, &kits);
    if (ELITE_DRUM_PARTS > 1 && panel->drums_b_initialized) {
        drum_kit saved_b = conductor_get_kit(kit_b);
        if (memcmp(&panel->drums_b.kit, &saved_b, sizeof saved_b)) {
            if (!memcmp(&panel->drums_b.kit, &panel->drums_kit, sizeof saved_b))
                kit_b = kit_a;
            else
                kit_b = conductor_plan_kit(&panel->drums_b.kit, panel->kit_slots, &kits);
        }
    }
    if (kit_a > DRUM_USER_KITS || kit_b > DRUM_USER_KITS) {
        panel->scene_status = SCENE_KITS_FULL;
        return 0;
    }
    if (dillo_sound_edited(panel)) {
        dillo = conductor_plan_dillo_sound(panel);
        if (dillo == DILLO_SLOT_NONE) {
            panel->scene_status = SCENE_SOUNDS_FULL;
            return 0;
        }
    }

    for (unsigned lane = 0; lane < MIX_AUDIO_LANES; lane++) {
        if (!pattern[lane]) continue;
        unsigned slot = pattern[lane] - 1u;
        if ((used[lane] | 1u) & (1u << slot)) conductor_bind_pattern(panel, lane, slot);
        else conductor_copy_pattern(panel, lane, slot);
    }
    if (conductor_kit_exists(panel->kit_slots, kit_a)) panel->kit_slot = (uint8_t)kit_a;
    else conductor_store_kit(panel, kit_a, 0);
    /* An existing kit or one just captured by A needs only a slot reference. */
    if (ELITE_DRUM_PARTS > 1) {
        if (conductor_kit_exists(panel->kit_slots, kit_b)) panel->drums_b_kit_slot = (uint8_t)kit_b;
        else conductor_store_kit(panel, kit_b, 1);
    }
    conductor_bind_dillo_sound(panel, dillo);
    return 1;
}

static unsigned conductor_user_sound_bit(unsigned slot)
{
    return slot >= BB_DILLO_PATCHES && slot < DILLO_SOUND_PAGE_SLOTS ?
           1u << (slot - BB_DILLO_PATCHES) : 0;
}

static void conductor_setup_assets(conductor_assets *assets, const uint8_t *setup, const uint8_t *b,
                                   unsigned kit, unsigned sound)
{
    assets->pattern[MIX_808] |= 1u << setup[CONDUCTOR_A_SLOT];
    assets->pattern[MIX_808_B] |= 1u << b[CONDUCTOR_B_SLOT];
    assets->pattern[MIX_DILLO] |= 1u << setup[CONDUCTOR_DILLO_SLOT];
    assets->pattern[MIX_TELEGRAM] |= 1u << setup[CONDUCTOR_TELEGRAM_SLOT];
    /* Kit masks use the displayed kit identity. Factory kit zero is reserved. */
    assets->kits |= (1u << kit) | (1u << b[CONDUCTOR_B_KIT_SLOT]);
    assets->sounds |= conductor_user_sound_bit(sound);
}

static conductor_assets conductor_reclaim_assets(const struct panel *panel, unsigned remove)
{
    conductor_assets keep = {.pattern = {
        [MIX_808] = 1u | (1u << panel->drums_slot) | (1u << panel->drums_active),
        [MIX_808_B] = 1u | (1u << panel->drums_b.slot) | (1u << panel->drums_b_active),
        [MIX_DILLO] = 1u | (1u << panel->dillo_slot) | (1u << panel->dillo_active),
        [MIX_TELEGRAM] = 1u | (1u << panel->telegram_slot) | (1u << panel->telegram_active),
    }, .kits = 1u | (1u << panel->kit_slot) | (1u << panel->drums_b_kit_slot),
       .sounds = conductor_user_sound_bit(panel->dillo_sound_slot)};
    /* A displaced edit still refers to its original bank slot. */
    for (unsigned lane = 0; lane < MIX_AUDIO_LANES; lane++)
        if (panel->draft_slot[lane]) keep.pattern[lane] |= 1u << (panel->draft_slot[lane] - 1u);
    if (conductor_snapshot_present) {
        const uint8_t *startup = conductor_snapshot;
        conductor_setup_assets(&keep, startup, startup + CONDUCTOR_SOUNDS_BYTES + 2u,
                               startup[CONDUCTOR_START_KIT], startup[CONDUCTOR_START_DILLO_SOUND]);
    }
    for (unsigned slot = 0; slot < CONDUCTOR_SCENES; slot++) {
        if (slot == remove || !(panel->scene_slots & (1u << slot))) continue;
        const conductor_scene *scene = &conductor_scenes[slot];
        conductor_setup_assets(&keep, scene->setup, scene->drums_b, scene->kit_slot, scene->dillo_sound_slot);
    }

    const conductor_scene *scene = &conductor_scenes[remove];
    conductor_assets release = {.pattern = {0}};
    conductor_setup_assets(&release, scene->setup, scene->drums_b, scene->kit_slot, scene->dillo_sound_slot);
    if (ELITE_DRUM_PARTS == 1) {
        /* The removal preview only lists the three primary pattern lanes. */
        release.pattern[MIX_808_B] = 0;
        release.kits &= ~(1u << scene->drums_b[CONDUCTOR_B_KIT_SLOT]);
    }
    for (unsigned lane = 0; lane < MIX_AUDIO_LANES; lane++) release.pattern[lane] &= ~keep.pattern[lane];
    release.kits &= ~keep.kits;
    release.sounds &= ~keep.sounds;
    return release;
}

static unsigned conductor_scene_in_use(const struct panel *panel, unsigned slot)
{
    unsigned number = slot + 1u;
    return panel->scene_active == number || conductor_queued_scene(panel) == number || panel->scene_sound == number;
}

static void conductor_remove_setup(struct panel *panel, unsigned slot)
{
    if (!(panel->scene_slots & (1u << slot))) return;
    if (conductor_scene_in_use(panel, slot)) {
        panel->scene_status = SCENE_REMOVE_IN_USE;
        return;
    }
    conductor_assets release = conductor_reclaim_assets(panel, slot);
    if (memcmp(&release, &panel->scene_cleanup_assets, sizeof release)) {
        panel->scene_status = SCENE_REMOVE_CHANGED;
        return;
    }
    panel->drums_slots &= ~release.pattern[MIX_808];
    panel->drums_b_slots &= ~release.pattern[MIX_808_B];
    panel->dillo_slots &= ~release.pattern[MIX_DILLO];
    panel->telegram_slots &= ~release.pattern[MIX_TELEGRAM];
    panel->kit_slots &= ~(release.kits >> 1);
    panel->dillo_sound_slots &= ~release.sounds;
    panel->scene_slots &= (uint8_t)~(1u << slot);
    if (panel->scene_queued == slot + 1u) panel->scene_queued = 0;
    if (panel->alternate_patch.scene_sound == slot + 1u) panel->compare_available = 0;
    panel->scene_status = SCENE_REMOVED;
    conductor_changed(panel);
}

enum { SCENE_CLEANUP_PICK = 1, SCENE_CLEANUP_CONFIRM = 2 };
enum { SCENE_LOAD, SCENE_STORE, SCENE_FORK, SCENE_MIX };

static void conductor_scene_pad(struct panel *panel, unsigned slot, unsigned action)
{
    if (slot >= CONDUCTOR_SCENES) return;
    panel->scene_cursor = (uint8_t)slot;
    conductor_scene *scene = &conductor_scenes[slot];
    if (action == SCENE_LOAD && (panel->scene_slots & (1u << slot))) {
        for (unsigned lane = 0; lane < MIX_AUDIO_LANES; lane++) conductor_stash_pattern(panel, lane);
    }
    if (action == SCENE_STORE || action == SCENE_FORK) {
        panel->conductor_copy_needed = 0;
        if (action == SCENE_FORK) {
            if (panel->scene_slots & (1u << slot)) {
                panel->scene_status = SCENE_OCCUPIED;
                return;
            }
            if (!conductor_fork_variation(panel)) return;
        } else {
            unsigned dillo = panel->dillo_sound_slot;
            if (conductor_kit_edited(panel)) {
                panel->scene_status = SCENE_SAVE_KIT;
                return;
            }
            if (dillo_sound_edited(panel)) {
#ifdef ELITE_SINGLE_808
                /* A factory startup sound needs a compatible saved slot
                 * when the player explicitly captures its first setup. */
                dillo_sound factory = dillo_saved_sound(DILLO_DEFAULT_SOUND);
                factory.tone.drive = CONDUCTOR_DILLO_START_DRIVE;
                if (dillo == DILLO_DEFAULT_SOUND && !memcmp(&factory, &panel->dillo_sound, sizeof factory))
                    dillo = conductor_plan_dillo_sound(panel);
#endif
                if (dillo == panel->dillo_sound_slot || dillo == DILLO_SLOT_NONE) {
                    panel->scene_status = SCENE_SAVE_DILLO;
                    return;
                }
            }
            drum_kit b_kit = conductor_get_kit(panel->drums_b_kit_slot);
            if (ELITE_DRUM_PARTS > 1 && panel->drums_b_initialized && memcmp(&panel->drums_b.kit, &b_kit, sizeof b_kit)) {
                panel->scene_status = SCENE_SAVE_B_KIT;
                return;
            }
            panel->conductor_copy_needed = (uint8_t)conductor_unstored_pattern(panel, 1);
            if (panel->conductor_copy_needed) {
                panel->scene_status = SCENE_NORMAL;
                return;
            }
#ifdef ELITE_PERCUSSION_727
            if (panel->layer_available && conductor_pattern_edited(panel, MIX_808_B)) {
                unsigned target = 1;
                for (; target < CONDUCTOR_SLOTS; ++target)
                    if ((panel->drums_b_slots & (1u << target)) &&
                        !conductor_pattern_differs(panel, MIX_808_B, target)) break;
                if (target == CONDUCTOR_SLOTS) target = conductor_free_slot(panel->drums_b_slots, CONDUCTOR_SLOTS);
                if (!target) {
                    panel->scene_status = SCENE_PATTERN_FULL;
                    return;
                }
                conductor_copy_pattern(panel, MIX_808_B, target);
            }
#endif
            conductor_bind_dillo_sound(panel, dillo);
        }
        if (panel->scene_queued == slot + 1u) panel->scene_queued = 0;
        conductor_capture_b_setup(panel, scene->drums_b);
        conductor_capture_performance(panel, scene->performance);
        scene->kit_slot = panel->kit_slot;
        scene->dillo_sound_slot = panel->dillo_sound_slot;
        conductor_capture_setup(panel, scene->setup);
        scene->mixer = conductor_mixer_controls(panel);
        scene->patch = panel->sound.patch;
        panel->scene_slots |= (uint8_t)(1u << slot);
        if (panel->alternate_patch.scene_sound == slot + 1u) panel->compare_available = 0;
        panel->scene_status = action == SCENE_FORK ? SCENE_VARIATION : SCENE_CAPTURED;
        panel->conductor_edits++;
        panel->conductor_dirty = 1;
    } else if (!(panel->scene_slots & (1u << slot))) {
        panel->scene_status = SCENE_EMPTY;
    } else if (action == SCENE_MIX) {
        conductor_apply_mix(panel, &scene->mixer);
        memcpy(panel->drums_b.mix, scene->drums_b + CONDUCTOR_B_MIX, MIX_CONTROLS);
        panel->scene_status = SCENE_MIX_RECALLED;
        conductor_changed(panel);
    } else if (conductor_cues_at_bar(panel)) {
        /* A setup can introduce lanes absent from an older arrangement.
         * Existing lanes keep the player's live enable and mute settings. */
        unsigned flags = scene->setup[CONDUCTOR_FLAGS], b_flags = scene->drums_b[0];
        if (!panel->drums_initialized && (flags & CONDUCTOR_A_ENABLED)) panel->drums_enabled = 1;
        if (!panel->dillo_pattern.length && (flags & CONDUCTOR_DILLO_ENABLED)) panel->dillo_enabled = 1;
        if (!panel->drums_b_initialized && (b_flags & (B_ENABLED | B_MUTED)) == B_ENABLED)
            panel->drums_b.enabled = 1;
        conductor_cue_setup_patterns(panel, scene->setup);
        /* An older setup with no B part must not claim its factory slot. */
        if (panel->drums_b_initialized || (b_flags & B_INITIALIZED))
            conductor_cue_pattern(panel, MIX_808_B, scene->drums_b[CONDUCTOR_B_SLOT]);
#ifdef ELITE_PERCUSSION_727
        if (panel->layer_available)
            panel->drums_b.percussion_kit = (uint8_t)conductor_b_layer_kit(b_flags);
#endif
        panel->scene_queued = (uint8_t)(slot + 1u);
        conductor_changed(panel);
        panel->scene_status = SCENE_CUED;
    } else {
        struct patch_snapshot previous = snapshot_patch(panel);
        conductor_apply_setup(panel, scene->setup, &scene->mixer);
        conductor_apply_b_setup(panel, scene->drums_b);
        panel->scene_queued = (uint8_t)(slot + 1u);
        if (scene->performance[0] != CONDUCTOR_PERFORMANCE_LEGACY) {
            conductor_read_performance(panel, scene->performance);
            memset(&arp_chord, 0, sizeof arp_chord);
            arp_tap_seen = 0;
        }
        panel->drums_b.kit = conductor_get_kit(panel->drums_b_kit_slot);
        panel->kit_slot = scene->kit_slot;
        panel->drums_kit = conductor_get_kit(scene->kit_slot);
        panel->dillo_sound_slot = scene->dillo_sound_slot;
        panel->dillo_sound = dillo_saved_sound(scene->dillo_sound_slot);
        panel->sound.patch = scene->patch;
        panel->sound.sound_epoch += scene->patch.filter_idle_hold;
        panel->user_sound = panel->imported_sound = 0;
        panel->scene_sound = (uint8_t)(slot + 1u);
        panel->startup_sound = 0;
        panel->compare_active = 0;
        fader_caught = 0;
        remember_patch(panel, &previous);
        panel->conductor_copy_needed = 0;
        panel->scene_status = SCENE_RECALLED;
        panel->conductor_startup_dirty = CONDUCTOR_STARTUP_CHECK;
    }
}
#endif

static const char *panel_sound_name(const struct panel *panel)
{
#ifdef ELITE_DRUMS808
    static const char *const names[CONDUCTOR_SCENES] = {
        "SETUP 1", "SETUP 2", "SETUP 3", "SETUP 4", "SETUP 5", "SETUP 6", "SETUP 7", "SETUP 8"
    };
    if (panel->scene_sound) return names[panel->scene_sound - 1u];
    if (panel->startup_sound) return conductor_startup_label;
#ifdef ELITE_BAND_PRESETS
    if (panel->band_sound && !panel->user_sound && !panel->imported_sound)
        return factory_band_sounds[factory_band_presets[panel->band_sound - 1u].sound].name;
    if (panel->studio_sound && !panel->user_sound && !panel->imported_sound)
        return factory_telegram_phrases[panel->studio_sound - 1u].name;
#endif
#endif
    const tg_imported_preset *imported = panel->imported_sound ? tg_imported_at(panel->imported_slot) : 0;
    return panel->user_sound ? user_library.patches[panel->user_slot].name : imported ? imported->sound.name : tg_presets[panel->preset].name;
}

static const tg_patch *panel_saved_patch(const struct panel *panel)
{
#ifdef ELITE_DRUMS808
    if (panel->scene_sound) return &conductor_scenes[panel->scene_sound - 1u].patch;
    if (panel->startup_sound) return &conductor_startup_sound.patch;
#ifdef ELITE_BAND_PRESETS
    if (panel->band_sound && !panel->user_sound && !panel->imported_sound)
        return &factory_band_sounds[factory_band_presets[panel->band_sound - 1u].sound].telegram;
    if (panel->studio_sound && !panel->user_sound && !panel->imported_sound)
        return conductor_studio_telegram_patch(panel->studio_sound - 1u);
#endif
#endif
    const tg_imported_preset *imported = panel->imported_sound ? tg_imported_at(panel->imported_slot) : 0;
    return panel->user_sound ? &user_library.patches[panel->user_slot].patch :
           imported ? &imported->sound.patch : &tg_presets[panel->preset].patch;
}

static unsigned panel_sound_edited(const struct panel *panel)
{
    return memcmp(panel_saved_patch(panel), &panel->sound.patch, sizeof(tg_patch)) != 0;
}

static const tg_imported_preset *library_imported(const struct panel *panel, unsigned slot)
{
    return tg_imported_at(tg_imported_match_index(panel->import_category, slot));
}

static void open_sound_bank(struct panel *panel)
{
    panel->compare_active = 0;
    panel->library_kind = LIB_IMPORTED;
    panel->import_category = tg_imported_match_count(TG_IMPORT_FIRST) ? TG_IMPORT_FIRST : TG_IMPORT_ALL;
    panel->preset_bank = panel->library_slot = 0;
    panel->library_status = LIB_NORMAL;
}

static void library_category(struct panel *panel, int delta)
{
    uint8_t available[TG_IMPORT_CATEGORY_COUNT];
    unsigned count = 0, current = 0;
    for (unsigned c = 0; c < TG_IMPORT_CATEGORY_COUNT; c++) {
        if (!tg_imported_match_count(c)) continue;
        if (c == panel->import_category) current = count;
        available[count++] = (uint8_t)c;
    }
    if (!count) return;
    unsigned next = (unsigned)((int)current + delta % (int)count + (int)count) % count;
    if (available[next] == panel->import_category) return;
    panel->compare_active = 0;
    panel->import_category = available[next];
    panel->preset_bank = 0;
    panel->library_slot = 0;
    panel->library_status = LIB_NORMAL;
}

#ifdef ELITE_DRUMS808
/* LIBRARY. PATCH opens the library of the instrument in front of you, at the shelf
 * last used there; PATCH again steps to its next shelf, and BT goes back to where
 * you were. Shelves never need GLOBE or PARA. The shelf names run along the hint row. */
enum library_shelf {
    SHELF_SOUNDS, SHELF_BANK, SHELF_PATTERNS, SHELF_DUETS, SHELF_DILLO_SOUNDS, SHELF_DILLO_PATTERNS,
    SHELF_DILLO_DUETS, SHELF_RHYTHMS, SHELF_KITS, SHELF_SETUPS, SHELF_LAYER, SHELF_SONGS, SHELF_COUNT,
    LIBRARY_SHELVES_MAX = 4
};
static const char *const library_shelf_name[SHELF_COUNT] = {
    [SHELF_SOUNDS] = "SOUNDS", [SHELF_BANK] = "BANK", [SHELF_PATTERNS] = "PATTERNS", [SHELF_DUETS] = "DUETS",
    [SHELF_DILLO_SOUNDS] = "SOUNDS", [SHELF_DILLO_PATTERNS] = "PATTERNS", [SHELF_DILLO_DUETS] = "DUETS",
    [SHELF_RHYTHMS] = "RHYTHMS", [SHELF_KITS] = "KITS", [SHELF_SETUPS] = "SETUPS", [SHELF_LAYER] = "727",
    [SHELF_SONGS] = "SONGS"
};

static unsigned library_pick(const struct panel *panel)
{
    unsigned pick = instrument_of_page(panel->library_open ? panel->library_return : panel->page, panel);
    return pick < PICK_COUNT ? pick : PICK_TELEGRAM;
}

static unsigned library_shelves(const struct panel *panel, uint8_t shelf[LIBRARY_SHELVES_MAX])
{
    unsigned n = 0;
    switch (library_pick(panel)) {
    case PICK_DILLO:
        shelf[n++] = SHELF_DILLO_SOUNDS;
#ifdef ELITE_SYNTH_PRESETS
        shelf[n++] = SHELF_DILLO_PATTERNS;
#endif
#ifdef ELITE_ACID_DUETS
        shelf[n++] = SHELF_DILLO_DUETS;
#endif
        break;
    case PICK_808_A:
        shelf[n++] = SHELF_RHYTHMS;
#ifdef ELITE_PERCUSSION_727
        if (panel->layer_available) shelf[n++] = SHELF_LAYER;
#endif
        shelf[n++] = SHELF_KITS;
        break;
    case PICK_BAND:
        /* The sounds of whichever synth has the keys, then the Band's own shelves. */
        shelf[n++] = panel->conductor_dillo_keys ? SHELF_DILLO_SOUNDS : SHELF_SOUNDS;
#ifdef ELITE_BAND_PRESETS
        shelf[n++] = SHELF_SONGS;
#endif
        shelf[n++] = SHELF_SETUPS;
#ifdef ELITE_ACID_DUETS
        shelf[n++] = panel->conductor_dillo_keys ? SHELF_DILLO_DUETS : SHELF_DUETS;
#endif
        break;
    default:
        shelf[n++] = SHELF_SOUNDS;
        if (tg_imported_count()) shelf[n++] = SHELF_BANK;
#ifdef ELITE_SYNTH_PRESETS
        shelf[n++] = SHELF_PATTERNS;
#endif
#ifdef ELITE_ACID_DUETS
        shelf[n++] = SHELF_DUETS;
#endif
        break;
    }
    return n;
}

static int library_page(unsigned page)
{
    return page == PAGE_PATCH || page == PAGE_SYNTH_PRESETS || page == PAGE_DILLO_SOUND ||
           page == PAGE_DRUM_PRESETS || page == PAGE_KITS || page == PAGE_SCENES;
}

/* Open only while the page is still the shelf PATCH put there; any other way out closes it. */
static int library_active(const struct panel *panel)
{
    return panel->library_open && panel->page == panel->library_at && library_page(panel->page);
}

static void library_enter(struct panel *panel, unsigned shelf)
{
    panel->library_shelf = (uint8_t)shelf;
    panel->save_mode = SAVE_NONE;
    switch (shelf) {
    case SHELF_SOUNDS:
    case SHELF_BANK:
        panel->page = PAGE_PATCH;
        panel->library_kind = LIB_FACTORY;
        panel->preset_bank = panel->preset / TG_PADS;
        panel->library_slot = panel->preset;
        panel->library_status = LIB_NORMAL;
        if (shelf == SHELF_BANK) open_sound_bank(panel);
        break;
#ifdef ELITE_SYNTH_PRESETS
    case SHELF_PATTERNS:
    case SHELF_DILLO_PATTERNS:
    case SHELF_DUETS:
    case SHELF_DILLO_DUETS: {
        unsigned dillo = shelf == SHELF_DILLO_PATTERNS || shelf == SHELF_DILLO_DUETS;
        unsigned duets = shelf == SHELF_DUETS || shelf == SHELF_DILLO_DUETS;
        unsigned kept_group = panel->synth_preset_group, kept_page = panel->synth_preset_page;
        unsigned same = panel->synth_preset_dillo == dillo;
        open_synth_presets(panel, dillo);
        unsigned sources = synth_preset_sources(panel);
        if (same && (kept_group == sources) == duets) {
            panel->synth_preset_group = (uint8_t)kept_group;
            panel->synth_preset_page = (uint8_t)kept_page;
        } else {
            panel->synth_preset_group = (uint8_t)(duets ? sources : 0);
            panel->synth_preset_page = 0;
        }
        break;
    }
#endif
    case SHELF_DILLO_SOUNDS: open_dillo_sound(panel); break;
    case SHELF_RHYTHMS: open_drum_presets(panel); break;
    case SHELF_LAYER: open_rhythms(panel, RHYTHM_LAYER); break;
    case SHELF_SONGS: open_rhythms(panel, RHYTHM_BAND); break;
    case SHELF_KITS: panel->page = PAGE_KITS; panel->kit_status = KIT_NORMAL; break;
    case SHELF_SETUPS: panel->page = PAGE_SCENES; panel->scene_status = SCENE_NORMAL; break;
    default: break;
    }
    panel->library_at = panel->page;
}

/* PATCH opens the library here on its first shelf, steps to the next shelf when it is
 * open, and after the last shelf goes back to where it was pressed. BT goes back at once. */
static int library_back(struct panel *panel);

static void library_patch(struct panel *panel)
{
    uint8_t shelf[LIBRARY_SHELVES_MAX];
    if (!library_active(panel)) {
        panel->library_return = panel->page;
        panel->library_open = 1;
        library_shelves(panel, shelf);
        library_enter(panel, shelf[0]);
        return;
    }
    unsigned n = library_shelves(panel, shelf), at = 0;
    while (at < n && shelf[at] != panel->library_shelf) at++;
    if (at + 1u >= n) library_back(panel);
    else library_enter(panel, shelf[at + 1u]);
}

/* BT inside the library goes back to where PATCH was pressed. */
static int library_back(struct panel *panel)
{
    if (!library_active(panel)) return 0;
    panel->page = panel->library_return;
    panel->library_open = 0;
    panel->save_mode = SAVE_NONE;
    return 1;
}
#endif

static unsigned library_occupied(const struct panel *panel, unsigned slot)
{
    if (panel->library_kind == LIB_IMPORTED) return library_imported(panel, slot) != 0;
    if (panel->library_kind == LIB_FACTORY) return slot < TG_PRESETS;
    if (slot >= TG_USER_SLOTS) return 0;
    const uint8_t *map = panel->library_kind == LIB_SOUNDS ? user_library.patches_present : user_library.phrases_present;
    return (map[slot / 8u] >> (slot % 8u)) & 1u;
}

static unsigned library_selected(const struct panel *panel, unsigned slot)
{
    if (panel->library_kind == LIB_IMPORTED) return panel->imported_sound &&
        panel->imported_slot == tg_imported_match_index(panel->import_category, slot);
    if (panel->library_kind == LIB_SOUNDS && panel->compare_active)
        return panel->user_sound && panel->user_slot == slot;
    return panel->library_kind == LIB_FACTORY ? !panel->user_sound && !panel->imported_sound && panel->preset == slot : panel->library_slot == slot;
}

static void library_name(struct panel *panel)
{
    const char *source = panel->library_kind == LIB_SOUNDS ? panel_sound_name(panel) : "PHRASE";
    memset(panel->save_name, ' ', TG_NAME_BYTES - 1);
    panel->save_name[TG_NAME_BYTES - 1] = 0;
    for (unsigned i = 0; i < TG_NAME_BYTES - 1 && source[i]; i++) {
        char c = source[i];
        panel->save_name[i] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
    }
    panel->name_cursor = 0;
}

static void library_confirm(struct panel *panel)
{
    char name[TG_NAME_BYTES];
    memcpy(name, panel->save_name, sizeof name);
    for (int i = TG_NAME_BYTES - 2; i >= 0 && name[i] == ' '; i--) name[i] = 0;
    if (!name[0]) memcpy(name, "UNTITLED", 9);
    unsigned slot = panel->library_slot;
    int rc = panel->library_kind == LIB_SOUNDS ? tg_library_put_patch(&user_library, slot, name, &panel->sound) :
                                               tg_library_put_phrase(&user_library, slot, name, &panel->sound);
    panel->save_mode = SAVE_NONE;
    panel->library_status = rc ? LIB_FAILED : LIB_SESSION;
    if (!rc) {
        panel->store_request = 1; /* Foreground storage service must confirm durability. */
        panel->library_dirty = 1;
        if (panel->library_kind == LIB_SOUNDS) {
            if (panel->alternate_patch.user_sound && panel->alternate_patch.user_slot == slot)
                panel->compare_available = 0;
            panel->imported_sound = panel->scene_sound = 0;
#ifdef ELITE_DRUMS808
            panel->startup_sound = 0;
#endif
            panel->user_sound = 1;
            panel->user_slot = (uint8_t)slot;
        }
    }
}

static void library_pad(struct panel *panel, unsigned pad)
{
    panel->compare_active = 0;
    unsigned slot = panel->preset_bank * TG_PADS + pad;
    panel->library_slot = (uint16_t)slot;
    panel->library_status = LIB_NORMAL;
    if (panel->save_mode) {
        panel->save_mode = SAVE_CONFIRM;
        return;
    }
#ifdef ELITE_DRUMS808
    if (panel->library_kind == LIB_PHRASES) {
        const tg_saved_phrase *phrase;
        int rc = tg_library_find_phrase(&user_library, slot, &phrase);
        if (rc) {
            panel->library_status = rc == TG_STORE_EMPTY ? LIB_EMPTY : LIB_FAILED;
            return;
        }
        conductor_stash_pattern(panel, MIX_TELEGRAM);
        conductor_replace_pattern(panel, MIX_TELEGRAM, panel->telegram_slot, phrase);
        panel->telegram_library = (uint8_t)(slot + 1u);
        panel->sound.transpose = 0;
        conductor_changed(panel);
        return;
    }
#endif
    struct patch_snapshot previous = snapshot_patch(panel);
    if (panel->library_kind == LIB_IMPORTED) {
        const tg_imported_preset *item = library_imported(panel, slot);
        if (!item) { panel->library_status = LIB_EMPTY; return; }
        panel->sound.patch = item->sound.patch;
        panel->sound.sound_epoch += panel->sound.patch.filter_idle_hold;
        panel->imported_sound = 1; panel->user_sound = panel->scene_sound = 0;
#ifdef ELITE_DRUMS808
        panel->startup_sound = 0;
#endif
        panel->imported_slot = (uint16_t)tg_imported_match_index(panel->import_category, slot); fader_caught = 0;
    } else if (panel->library_kind == LIB_FACTORY) select_preset(panel, slot);
    else {
        int rc = panel->library_kind == LIB_SOUNDS ? tg_library_get_patch(&user_library, slot, &panel->sound) :
                                                   tg_library_get_phrase(&user_library, slot, &panel->sound);
        if (rc) panel->library_status = rc == TG_STORE_EMPTY ? LIB_EMPTY : LIB_FAILED;
        else if (panel->library_kind == LIB_SOUNDS) {
            panel->sound.sound_epoch += panel->sound.patch.filter_idle_hold;
            panel->imported_sound = panel->scene_sound = 0;
#ifdef ELITE_DRUMS808
            panel->startup_sound = 0;
#endif
            panel->user_sound = 1; panel->user_slot = (uint8_t)slot; fader_caught = 0;
        } else {
            panel->cursor = 0;
            panel->sound.transpose = 0;
            panel->sound.phrase_epoch++;
            panel->phrase_undo = UNDO_NONE;
        }
    }
    if (panel->library_kind != LIB_PHRASES && panel->library_status == LIB_NORMAL)
        remember_patch(panel, &previous);
}

static void manual_page(struct panel *panel, int delta)
{
    unsigned bank = (panel->fader_bank + FADER_PAGES + delta) % FADER_PAGES;
    if (bank != panel->fader_bank)
        fader_caught = 0;
    panel->fader_bank = (uint8_t)bank;
    panel->page = PAGE_MANUAL;
}

static int untransposed_key_note(const struct panel *panel, unsigned key)
{
    return 36 + (int)key + 12 * panel->octave;
}

static int scale_contains(const struct panel *panel, unsigned note)
{
    unsigned interval = (note + SCALE_ROOTS - panel->scale_root) % SCALE_ROOTS;
    return (scale_intervals[panel->scale_type] >> interval) & 1u;
}

static unsigned key_note(const struct panel *panel, unsigned key)
{
    unsigned note = bounded(untransposed_key_note(panel, key) + panel->key_transpose, 1, 127);
    if (!panel->scale_type || scale_contains(panel, note)) return note;
    /* Nearest scale tone. Equidistant notes resolve down unless out of range. */
    for (unsigned gap = 1; gap < SCALE_ROOTS; gap++) {
        if (note > gap && scale_contains(panel, note - gap)) return note - gap;
        if (note + gap <= 127 && scale_contains(panel, note + gap)) return note + gap;
    }
    return note;
}

/* In the editor the visible page belongs to the cursor. Elsewhere the
 * pads follow playback, and pressing one opens that same step to edit. */
static unsigned step_page_base(const struct panel *panel, unsigned head)
{
    unsigned step = panel->page != PAGE_STEPS && head < TG_STEPS ? head : panel->cursor;
    return (step / TG_PADS) * TG_PADS;
}

static void save_phrase_undo(struct panel *panel, unsigned action)
{
    if (panel->record == REC_NEW) return; /* One undo for the whole new take. */
    memcpy(panel->undo_steps, panel->sound.steps, sizeof panel->undo_steps);
    panel->undo_length = panel->sound.length;
    panel->undo_cursor = panel->cursor;
    panel->undo_gate = panel->sound.phrase_gate;
    panel->phrase_undo = (uint8_t)action;
}

static void enter_step(struct panel *panel, unsigned note, unsigned tie)
{
    panel->record_key = 0;
    uint8_t entry = (uint8_t)(note | (note ? tie : 0));
    int extends = panel->cursor >= tg_pattern_length(&panel->sound);
    if (extends || panel->sound.steps[panel->cursor] != entry)
        save_phrase_undo(panel, UNDO_STEP);
    panel->sound.steps[panel->cursor] = entry;
    if (extends)
        panel->sound.length = panel->cursor + 1u;
    if (panel->cursor + 1u < TG_STEPS)
        panel->cursor++;
    else
        panel->record = 0;
}

static void phrase_edit_arrow(struct panel *panel, int arrow)
{
    if (arrow < 0) {
        if (panel->phrase_undo == UNDO_NONE) return;
        memcpy(panel->sound.steps, panel->undo_steps, sizeof panel->undo_steps);
        panel->sound.length = panel->undo_length;
        panel->cursor = panel->undo_cursor;
        panel->sound.phrase_gate = panel->undo_gate;
        panel->phrase_undo = UNDO_NONE;
        return;
    }
    unsigned length = tg_pattern_length(&panel->sound);
    if (length < 2 || panel->phrase_undo == UNDO_SHIFT) return;

    save_phrase_undo(panel, UNDO_SHIFT);
    uint8_t *steps = panel->sound.steps;
    uint8_t last = steps[length - 1];
    for (unsigned i = length - 1; i > 0; i--) steps[i] = steps[i - 1];
    steps[0] = last;
}

static void arp_sync(struct panel *panel, unsigned source)
{
    if (panel->sound.clock.source == source) return;
    panel->sound.clock.source = (uint8_t)source;
    panel->sound.running = 0;
    panel->clock_toggle = 0;
    arp_tap_seen = 0;
}

static unsigned arp_latch_allowed(const tg_controls *sound)
{
#ifdef ELITE_DRUMS808
    /* Conductor advances the phrase grid independently of the live arp. */
    (void)sound;
    return 1;
#else
    return !sound->running;
#endif
}

static void arp_encoder(struct panel *panel, unsigned knob, int delta)
{
    tg_controls *sound = &panel->sound;
    switch (knob) {
    case 0: sound->arp = (uint8_t)bounded(sound->arp + delta, TG_ARP_OFF, TG_ARP_REPEAT); break;
    case 1: {
        int rate = sound->arp_rate ? sound->arp_rate : 5;
        sound->arp_rate = (uint8_t)bounded(rate + delta, 1, TG_ARP_RATE_COUNT);
        break;
    }
    case 2: {
        int gate = sound->arp_gate ? sound->arp_gate : 75;
        sound->arp_gate = (uint8_t)bounded(gate + 5 * delta, 5, 100);
        break;
    }
    case 3: sound->arp_octaves = (uint8_t)bounded(sound->arp_octaves + delta, 0, 3); break;
    case 4: sound->arp_swing = (uint8_t)bounded(sound->arp_swing + 2 * delta, 0, 25); break;
    case 5: panel->arp_latch = delta > 0; break;
    case 6:
        if (sound->clock.source == TG_CLOCK_INTERNAL)
            sound->arp_tempo = (uint16_t)bounded((int)tg_arp_tempo(sound) + delta, 30, 300);
        break;
    case 7: arp_sync(panel, delta > 0 ? TG_CLOCK_USB : TG_CLOCK_INTERNAL); break;
    }
}

#ifdef ELITE_DILLO
#ifdef ELITE_DRUMS808
static void dillo_queue_take(struct panel *panel)
{
    /* An edit to the incoming Dillo slot stays with its queued setup. */
    if (panel->dillo_cue == panel->dillo_applied) panel->scene_queued = 0;
    panel->dillo_cue++;
}
#endif

static void dillo_edit_arrow(struct panel *panel, int arrow)
{
    if (arrow < 0 && panel->dillo_undo == UNDO_NEW) {
        panel->dillo_pattern = panel->dillo_undo_take;
        panel->dillo_cursor = panel->dillo_undo_cursor;
        panel->dillo_undo = UNDO_NONE;
#ifdef ELITE_DRUMS808
        dillo_queue_take(panel);
#endif
        return;
    }
    if (arrow < 0 && panel->dillo_undo > UNDO_SHIFT) {
        panel->dillo_cursor = panel->dillo_undo_cursor;
        panel->dillo_pattern.steps[panel->dillo_cursor] = panel->dillo_undo_step;
        panel->dillo_pattern.length = panel->dillo_undo_length;
        panel->dillo_undo = UNDO_NONE;
        return;
    }
    unsigned length = panel->dillo_pattern.length;
    if (length < 2 || length > DILLO_STEPS) return;
    dillo_step *steps = panel->dillo_pattern.steps;
    unsigned span = (length - 1u) * sizeof *steps;
    if (arrow > 0) {
        dillo_step last = steps[length - 1u];
        memmove(steps + 1, steps, span);
        steps[0] = last;
        panel->dillo_undo = UNDO_SHIFT;
        panel->dillo_undo_cursor = panel->dillo_cursor;
        if (panel->dillo_cursor < length)
            panel->dillo_cursor = (uint8_t)((panel->dillo_cursor + 1u) % length);
    } else if (panel->dillo_undo == UNDO_SHIFT) {
        dillo_step first = steps[0];
        memmove(steps, steps + 1, span);
        steps[length - 1u] = first;
        panel->dillo_cursor = panel->dillo_undo_cursor;
        panel->dillo_undo = UNDO_NONE;
    }
}

static void dillo_edit_step(struct panel *panel, dillo_step next, unsigned length, unsigned action)
{
    dillo_step *step = &panel->dillo_pattern.steps[panel->dillo_cursor];
    if (!memcmp(step, &next, sizeof next) && panel->dillo_pattern.length == length) return;
    if (action == UNDO_STEP || panel->dillo_undo != action || panel->dillo_undo_cursor != panel->dillo_cursor) {
        panel->dillo_undo_step = *step;
        panel->dillo_undo_length = panel->dillo_pattern.length;
        panel->dillo_undo_cursor = panel->dillo_cursor;
        panel->dillo_undo = (uint8_t)action;
    }
    *step = next;
    panel->dillo_pattern.length = (uint8_t)length;
}

static dillo_step dillo_pitched_step(dillo_step step, unsigned pitch)
{
    if (step.event != DILLO_NOTE)
        step = (dillo_step){.event = DILLO_NOTE, .gate_percent = 65};
    step.pitch = (uint8_t)pitch;
    return step;
}

static void dillo_pitch(struct panel *panel, unsigned pitch, unsigned action)
{
    dillo_step step = dillo_pitched_step(panel->dillo_pattern.steps[panel->dillo_cursor], pitch);
    unsigned length = panel->dillo_pattern.length;
    if (length <= panel->dillo_cursor) length = panel->dillo_cursor + 1u;
    dillo_edit_step(panel, step, length, action);
}

static void dillo_edit_turns(struct panel *panel, const int delta[4])
{
    dillo_step step = panel->dillo_pattern.steps[panel->dillo_cursor];
    unsigned length = panel->dillo_pattern.length, action = UNDO_NONE;
    /* Pitch creates a note before applying the other turns in this scan. */
    if (delta[3]) {
        step = dillo_pitched_step(step, bounded((step.event == DILLO_NOTE ? step.pitch : 36) + delta[3], 0, 127));
        if (length <= panel->dillo_cursor) length = panel->dillo_cursor + 1u;
        action = UNDO_PITCH;
    }
    if (delta[1] && step.event != DILLO_REST) {
        step.gate_percent = (uint8_t)bounded(step.gate_percent + delta[1] * 5, 5, 100);
        action = action ? UNDO_STEP : UNDO_GATE;
    }
    if (step.event == DILLO_NOTE) {
        if (delta[0]) {
            step.flags = (uint8_t)(delta[0] > 0 ? step.flags | DILLO_ACCENT : step.flags & ~DILLO_ACCENT);
            action = UNDO_STEP;
        }
        if (delta[2]) {
            step.flags = (uint8_t)(delta[2] > 0 ? step.flags | DILLO_SLIDE : step.flags & ~DILLO_SLIDE);
            action = UNDO_STEP;
        }
    }
    if (action) dillo_edit_step(panel, step, length, action);
}

static void dillo_rec(struct panel *panel, unsigned classic)
{
    panel->dillo_pitch_replace = 0;
#ifdef ELITE_DRUMS808
    panel->dillo_entry_key = panel->dillo_pitch_paused = 0;
#endif
    if (classic) {
        panel->dillo_record = DILLO_REC_PITCH;
        panel->dillo_pitch_count = panel->dillo_time_count = panel->dillo_used = 0;
        panel->dillo_take_flags = panel->dillo_take_error = 0;
        panel->dillo_take_gate = 65;
    } else if (panel->dillo_record == DILLO_REC_PITCH) {
        panel->dillo_record = DILLO_REC_TIME;
        panel->dillo_take_error = 0;
    } else if (panel->dillo_record == DILLO_REC_TIME) {
        dillo_phrase take;
        if (dillo_phrase_from_passes(&take, panel->dillo_pitches,
                                    panel->dillo_pitch_count, panel->dillo_time, panel->dillo_time_count)) {
            panel->dillo_undo_take = panel->dillo_pattern;
            panel->dillo_undo_cursor = panel->dillo_cursor;
            panel->dillo_undo = UNDO_NEW;
            /* The Conductor bar stays at 16 steps. An early end leaves rests. */
            take.length = DILLO_STEPS;
            panel->dillo_pattern = take;
#ifdef ELITE_DRUMS808
            dillo_queue_take(panel);
#endif
            panel->dillo_record = DILLO_REC_OFF;
            panel->dillo_cursor = 0;
        } else panel->dillo_take_error = 1;
    } else {
        panel->dillo_record = panel->dillo_record ? DILLO_REC_OFF : DILLO_REC_DIRECT;
    }
}

/* Pitch Pass contains only notes, while Time Pass also contains ties and rests. */
static unsigned dillo_time_rank(const struct panel *panel, unsigned step)
{
    unsigned rank = 0;
    for (unsigned i = 0; i < step; i++)
        rank += panel->dillo_time[i].event == DILLO_NOTE;
    return rank;
}

static void dillo_time_pad(struct panel *panel, unsigned pad)
{
    unsigned edit = panel->dillo_time_edit;
    dillo_step *step = edit ? &panel->dillo_time[edit - 1u] : NULL;
    if (pad == 3 || pad == 4) {
        unsigned flag = pad == 3 ? DILLO_ACCENT : DILLO_SLIDE;
        if (!edit) panel->dillo_take_flags ^= flag;
        else if (step->event == DILLO_NOTE) step->flags ^= flag;
        return;
    }
    if (pad > 2 || (!edit && panel->dillo_time_count >= DILLO_STEPS)) return;
    unsigned event = pad == 0 ? DILLO_NOTE : pad == 1 ? DILLO_REST : DILLO_TIE;
    if (edit && event == step->event) return;
    if (event == DILLO_NOTE && panel->dillo_used >= panel->dillo_pitch_count) {
        panel->dillo_take_error = 1;
        return;
    }
    if (edit && (event == DILLO_NOTE || step->event == DILLO_NOTE)) {
        /* Keep the assigned prefix in time order and the later notes on their pitches. */
        unsigned rank = dillo_time_rank(panel, edit - 1u);
        uint8_t *pitches = panel->dillo_pitches;
        if (event == DILLO_NOTE) {
            uint8_t pitch = pitches[panel->dillo_used];
            memmove(pitches + rank + 1u, pitches + rank, panel->dillo_used - rank);
            pitches[rank] = pitch;
        } else {
            memmove(pitches + rank, pitches + rank + 1u, panel->dillo_pitch_count - rank - 1u);
            panel->dillo_pitch_count--;
            panel->dillo_used--;
        }
    }
    if (event == DILLO_NOTE) panel->dillo_used++;
    if (!edit) step = &panel->dillo_time[panel->dillo_time_count++];
    *step = (dillo_step){
        .event = (uint8_t)event,
        .gate_percent = event == DILLO_REST ? 0 : panel->dillo_take_gate,
        .flags = event == DILLO_NOTE ? panel->dillo_take_flags : 0,
    };
    panel->dillo_take_error = 0;
}
#endif

#ifdef ELITE_DRUMS808
static uint8_t *conductor_mix_value(struct panel *panel, unsigned lane, unsigned control)
{
    return lane == MIX_808_B ? &panel->drums_b.mix[control] : &panel->mixer.value[lane][control];
}

/* KNOB BANK + pad 1-16 picks one of the sixteen sounds. A conga, the claves or the maracas
 * flips its track's kit switch, as on the TR-808, and every pick plays the sound when stopped. */
static void drum_pick_sound(struct panel *panel, unsigned sound)
{
    unsigned part = drum_part(panel);
    drum_kit *kit = part ? &panel->drums_b.kit : &panel->drums_kit;
    uint8_t *track = part ? &panel->drums_b_track : &panel->drums_track;
    *track = (uint8_t)drum_sound_track(sound);
    /* Only 808 A's switches are saved: a second 808 plays each track's usual sound. */
    if (!part && drum_kit_select_sound(kit, sound)) conductor_changed(panel);
    if (!conductor_running(panel)) {
        drum_audition *strike = &panel->drum_auditions[part];
        strike->track = *track;
        strike->accent = 0;
        strike->generation++;
    }
}

static void step_drum_voice(struct panel *panel, int delta)
{
    uint8_t *voice = drum_part(panel) ? &panel->drums_b_track : &panel->drums_track;
    *voice = (uint8_t)((*voice + D8_NUM + delta % D8_NUM) % D8_NUM);
}

static uint16_t step_dillo_cutoff(uint16_t hz, int delta)
{
    int rate = hz / 32;
    return (uint16_t)bounded(hz + delta * (rate < 5 ? 5 : rate), 20, 16000);
}

static void conductor_mixer_knobs(struct panel *panel)
{
    tg_controls *sound = &panel->sound;
    unsigned lane = panel->mixer_lane;
    for (unsigned knob = 0; knob < 8; knob++) {
        int delta = fm1_enc_take(knob);
        if (!delta) continue;
        panel->strip_knob = (uint8_t)(knob + 1u);
        panel->strip_frame = panel->view_frames;
        if (knob < MIX_CONTROLS) {
            uint8_t *value = conductor_mix_value(panel, lane, knob);
            if (lane == MIX_TELEGRAM && knob >= MIX_DELAY)
                value = knob == MIX_DRIVE ? &sound->patch.drive :
                        &sound->patch.value[knob == MIX_DELAY ? TG_DELAY : TG_REVERB];
            uint8_t next = (uint8_t)bounded(*value + delta, 0, 127);
            if (knob == MIX_LEVEL && *value != next) fader_caught &= ~(1u << conductor_lane_position(lane));
            *value = next;
            conductor_changed(panel);
        } else if (knob == 7) {
            set_control(sound, CTRL_LEVEL, bounded(sound->master + delta, 0, 127));
            conductor_changed(panel);
        }
    }
}
#endif

static void read_controls_once(struct panel *panel, unsigned head);

static void read_controls(struct panel *panel, unsigned head)
{
    read_controls_once(panel, head);
#ifdef ELITE_DRUMS808
    /* Leaving the shelf by any route closes LIBRARY, so a later visit there is a plain page. */
    if (panel->library_open && panel->page != panel->library_at) panel->library_open = 0;
#endif
}

static void read_controls_once(struct panel *panel, unsigned head)
{
    tg_controls *sound = &panel->sound;
#ifdef ELITE_DRUMS808
    unsigned was_running = sound->running, old_clock_source = sound->clock.source;
    unsigned prior_dillo_audition = panel->dillo_audition, dillo_audition_strike = 0;
    unsigned prior_dillo_entry = panel->dillo_entry_key;
#ifdef ELITE_DRUMS808
    if (screen_focus_pending && !fm1_in.keys) {
        conductor_focus_keys(panel, screen_focus_pending - 1u, fm1_in.frames);
        screen_focus_pending = 0;
    }
#endif
    unsigned prior_key_focus = panel->conductor_dillo_keys;
    unsigned prior_bank = panel->band_bank, prior_drum_focus = drum_part(panel), prior_bottom = panel->band_bottom;
    unsigned prior_seq_picker = panel->conductor_seq_pending;
    unsigned prior_dillo_bank = panel->dillo_sound_bank;
    unsigned prior_library_kind = panel->library_kind, prior_preset_bank = panel->preset_bank;
    unsigned prior_import_category = panel->import_category;
#ifdef ELITE_SYNTH_PRESETS
    unsigned prior_synth_group = panel->synth_preset_group, prior_synth_page = panel->synth_preset_page;
#endif
#endif
    uint32_t pressed = fm1_input_edges(0);
    unsigned confirm_requested = 0, library_pad_pressed = 0, layout_requested = 0;
    unsigned cleared_step = 0, knob_focused = 0;
    int step_pitch_delta = 0;
    unsigned prior_page = panel->page;
#ifdef ELITE_DILLO
    unsigned prior_dillo_record = panel->dillo_record;
    unsigned dillo_take_restored = 0, dillo_pad_command = 0;
    int dillo_step_deltas[4] = {0};
#endif
    uint64_t down;
    uint32_t buttons, frames;
    uint16_t wheels[2], pedal;
    uint8_t faders[4], velocities[TG_KEYS];
    uint32_t irq = fm1_icfg();
    fm1_irq_off();
    down = fm1_in.keys;
    buttons = fm1_in.buttons;
    frames = fm1_in.frames;
#ifdef ELITE_DRUMS808
    panel->held_buttons = buttons;
    panel->view_frames = frames;
#endif
    pedal = fm1_in.pedal;
    for (unsigned i = 0; i < 2; i++) wheels[i] = fm1_in.wheel[i];
    for (unsigned i = 0; i < 4; i++) faders[i] = fm1_in.fader[i];
    for (unsigned i = 0; i < TG_KEYS; i++) velocities[i] = fm1_in.key_vel[i];
    fm1_icfg_set(irq);

    unsigned globe_held = buttons & (1u << BTN_GLOBE);
    unsigned arp_held = buttons & (1u << BTN_ARP);
    if (pressed & (1u << BTN_GLOBE)) {
        panel->globe_tap_pending = 1;
#ifdef ELITE_DRUMS808
        panel->globe_frame = frames;
#endif
    }
    if (pressed & (1u << BTN_ARP)) {
        panel->arp_tap_pending = 1;
        arp_press_frame = frames;
    }
    unsigned repeat_held = buttons & (1u << BTN_REST);
    unsigned knob_bank_pressed = pressed & (1u << SMK_BTN_KNOB_BANK);
#ifdef ELITE_DRUMS808
    if (panel->key_focus_notice && frames - key_focus_frame >= KEY_FOCUS_NOTICE_FRAMES)
        panel->key_focus_notice = 0;
    const uint32_t focus_buttons = (1u << BTN_GLOBE) |
        (1u << (panel->focus_chord == FOCUS_CHORD_DRUMS ? BTN_FX : SMK_BTN_KNOB_BANK));
    if (!(buttons & focus_buttons)) panel->focus_chord = 0;
    if (globe_held && knob_bank_pressed && panel->page != PAGE_GLOBAL &&
        panel->page != PAGE_ASSIGN && !panel->save_mode && panel->focus_chord != FOCUS_CHORD_DRUMS) {
        conductor_focus_keys(panel, !panel->conductor_dillo_keys, frames);
        panel->focus_chord = FOCUS_CHORD_KEYS;
    }
    if (panel->focus_chord) {
        /* The performance shortcut owns both buttons until release. */
        buttons &= ~focus_buttons;
        pressed &= ~focus_buttons;
        panel->globe_tap_pending = 0;
        globe_held = 0;
        knob_bank_pressed = pressed & (1u << SMK_BTN_KNOB_BANK);
    }
#endif
    unsigned plain_pad_toggle = repeat_held && knob_bank_pressed && performance_page(panel->page) &&
                                !(buttons & (1u << BTN_SEQ));
    if (knob_bank_pressed && !plain_pad_toggle) {
#ifdef ELITE_DRUMS808
        if (panel->page == PAGE_CONDUCTOR && panel->conductor_dillo_keys) open_dillo_sound(panel);
        else if (panel->page == PAGE_DILLO_SOUND) panel->dillo_sound_page ^= 1u;
        else if (panel->page != PAGE_DRUMS)
#endif
        {
            if (panel->page == PAGE_CONDUCTOR) panel->page = PAGE_TONE;
            else panel->knob_bank ^= 1u;
        }
    }
    if (pressed & (1u << SMK_BTN_FADER_BANK)) {
        if (panel->page == PAGE_ASSIGN) {
            panel->fader_bank = (panel->fader_bank + 1u) % FADER_PAGES;
            panel->assign_source = MAP_FADER;
            panel->assign_slot = 0;
            fader_caught = 0;
        } else {
            manual_page(panel, panel->page == PAGE_MANUAL ? 1 : 0);
        }
    }
    if (pressed & (1u << BTN_PATCH)) {
        panel->compare_active = 0;
#ifdef ELITE_DRUMS808
#ifdef ELITE_SYNTH_PRESETS
        unsigned instrument = instrument_of_page(panel->page, panel);
#endif
        if (!(globe_held && !library_active(panel))) {
            library_patch(panel);
            panel->globe_tap_pending = 0;
        } else
#ifdef ELITE_SYNTH_PRESETS
        if (panel->page == PAGE_SYNTH_PRESETS) {
            panel->page = panel->synth_preset_return;
            panel->globe_tap_pending = 0;
        } else if (globe_held && !panel->save_mode &&
                   (panel->page == PAGE_TONE || panel->page == PAGE_STEPS || panel->page == PAGE_PATCH ||
                    instrument == PICK_DILLO || panel->page == PAGE_CONDUCTOR)) {
            open_synth_presets(panel, instrument == PICK_DILLO ||
                               (panel->page == PAGE_CONDUCTOR && panel->conductor_dillo_keys));
        } else
#endif
        if (panel->page == PAGE_DILLO_SOUND) {
            panel->page = panel->dillo_sound_home ? PAGE_CONDUCTOR : PAGE_DILLO;
        } else if (panel->page == PAGE_DRUMS) {
            open_drum_presets(panel);
        } else if (panel->page == PAGE_DRUM_PRESETS) {
            panel->page = PAGE_DRUMS;
        } else if (panel->page == PAGE_DILLO || (panel->page == PAGE_CONDUCTOR && panel->conductor_dillo_keys)) {
            open_dillo_sound(panel);
        } else
#endif
        if (prior_page == PAGE_PATCH && panel->page == PAGE_PATCH && panel->library_kind == LIB_FACTORY &&
            !panel->save_mode && tg_imported_count()) {
            panel->patch_bank_pending = 1;
        } else {
            panel->page = PAGE_PATCH;
            panel->library_kind = LIB_FACTORY;
            panel->preset_bank = panel->preset / TG_PADS;
            panel->library_slot = panel->preset;
            panel->save_mode = SAVE_NONE;
            panel->library_status = LIB_NORMAL;
#ifdef ELITE_DRUMS808
            if (prior_page != PAGE_PATCH && tg_imported_count()) open_sound_bank(panel);
#endif
        }
    }
    unsigned seq_held = buttons & (1u << BTN_SEQ);
    if (pressed & (1u << BTN_SEQ)) {
#ifdef ELITE_DRUMS808
        if (panel->page == PAGE_CONDUCTOR) {
            if (seq_held) panel->conductor_seq_pending = EDITOR_LAST;
            else conductor_editor(panel, EDITOR_LAST);
        } else
#endif
        if (panel->page != PAGE_STEPS) panel->page = PAGE_STEPS;
        else if (seq_held) panel->seq_exit_pending = 1;
        else panel->page = PAGE_TONE;
#ifdef ELITE_DRUMS808
        if (panel->page == PAGE_STEPS || panel->page == PAGE_TONE) screen_focus_keys(panel, 0, frames);
#endif
    }
    if (panel->seq_exit_pending) {
        if (panel->page != PAGE_STEPS || (pressed & ~(1u << BTN_SEQ))) {
            panel->seq_exit_pending = 0;
        } else if (!seq_held) {
            panel->seq_exit_pending = 0;
            panel->page = PAGE_TONE;
        }
    }
    unsigned dillo_shortcut = 0;
#ifdef ELITE_DILLO
    dillo_shortcut = seq_held && (panel->page == PAGE_STEPS || panel->page == PAGE_CONDUCTOR) && (pressed & (1u << BTN_PARA));
    if (dillo_shortcut) {
        if (!panel->dillo_pattern.length) {
            panel->dillo_pattern = dillo_demo;
            panel->dillo_undo = UNDO_NONE;
            panel->dillo_enabled = 1;
        }
        panel->page = PAGE_DILLO;
        panel->seq_exit_pending = 0;
#ifdef ELITE_DRUMS808
        screen_focus_keys(panel, 1, frames); /* The keys play the synth on screen. */
#endif
    } else if (panel->page == PAGE_DILLO && (pressed & (1u << BTN_PARA))) {
        if (panel->dillo_record >= DILLO_REC_PITCH) panel->dillo_record = DILLO_REC_OFF;
#ifdef ELITE_DRUMS808
        else conductor_toggle_pattern(panel, MIX_DILLO);
#else
        else panel->dillo_enabled ^= 1u;
#endif
    }
#endif
    unsigned drums_shortcut = 0, drum_focus_shortcut = 0;
#ifdef ELITE_DRUMS808
    drums_shortcut = seq_held && (panel->page == PAGE_STEPS || panel->page == PAGE_CONDUCTOR) && (pressed & (1u << BTN_FX));
    drum_focus_shortcut = !seq_held && globe_held && panel->page == PAGE_CONDUCTOR && (pressed & (1u << BTN_FX));
    if (drum_focus_shortcut) {
        if (ELITE_DRUM_PARTS > 1) {
            panel->drums_focus ^= 1u;
            conductor_changed(panel);
            panel->conductor_lane = PAGE_DRUMS;
        } else {
            panel->band_bottom = (uint8_t)((panel->band_bottom + 1u) % BAND_BOTTOM_MODES);
            if (panel->band_bottom) panel->band_bank &= ~1u;
        }
        panel->focus_chord = FOCUS_CHORD_DRUMS;
        panel->globe_tap_pending = 0;
        globe_held = 0;
    }
    if (drums_shortcut) {
        open_drum_editor(panel, 1);
        panel->seq_exit_pending = 0;
    } else if (panel->page == PAGE_DRUM_PRESETS && library_active(panel) && !globe_held && (pressed & (1u << BTN_PARA))) {
        panel->rhythm_info = (uint8_t)((panel->rhythm_info + 1u) % JUKEBOX_VIEWS); /* INFO */
    } else if ((panel->page == PAGE_DRUMS || panel->page == PAGE_DRUM_PRESETS) && !globe_held && (pressed & (1u << BTN_PARA))) {
        conductor_toggle_pattern(panel, drum_part(panel) ? MIX_808_B : MIX_808);
    }
#endif
#ifdef ELITE_DRUMS808
    if (panel->page != PAGE_CONDUCTOR || (pressed & ~(1u << BTN_SEQ)))
        panel->conductor_seq_pending = 0;
#endif
    if ((pressed & (1u << BTN_FX)) && !drums_shortcut && !drum_focus_shortcut &&
        !(pressed & ((1u << BTN_ASSIGN) | (1u << BTN_PATCH) | (1u << BTN_SEQ) | (1u << SMK_BTN_FADER_BANK)))) {
        if (panel->page == PAGE_FX || panel->page == PAGE_MIX) panel->page = panel->fx_return;
#ifdef ELITE_DRUMS808
        else if (panel->page == PAGE_CONDUCTOR || panel->page == PAGE_DILLO || panel->page == PAGE_DILLO_SOUND || panel->page == PAGE_DRUMS ||
                 panel->page == PAGE_DRUM_PRESETS) {
            panel->fx_return = panel->page;
            if (panel->page == PAGE_DILLO || panel->page == PAGE_DILLO_SOUND) panel->mixer_lane = MIX_DILLO;
            if (panel->page == PAGE_DRUMS || panel->page == PAGE_DRUM_PRESETS)
                panel->mixer_lane = drum_part(panel) ? MIX_808_B : MIX_808;
            panel->page = PAGE_MIX;
        }
#endif
        else {
            panel->fx_return = panel->page;
            panel->page = PAGE_FX;
        }
    }
    if (panel->page != PAGE_STEPS) panel->record = 0;
    uint32_t navigation = (1u << BTN_ASSIGN) | (1u << BTN_PATCH) | (1u << BTN_SEQ) |
                          (1u << BTN_PARA) | (1u << BTN_FX) | (1u << SMK_BTN_FADER_BANK) |
                          (1u << SMK_BTN_LEFT) | (1u << SMK_BTN_RIGHT);
#ifdef ELITE_DRUMS808
    unsigned cleanup_edge = 0;
    unsigned draft_edge = 0, drum_undo_edge = 0;
    const uint32_t remove_chord = (1u << BTN_REC) | (1u << SMK_BTN_LEFT);
    const uint32_t draft_chord = (1u << BTN_REC) | (1u << SMK_BTN_RIGHT);
    if (prior_page == PAGE_CONDUCTOR && panel->page == PAGE_CONDUCTOR &&
        (buttons & (1u << BTN_REC)) && (pressed & (1u << SMK_BTN_RIGHT)) &&
        !repeat_held && !seq_held && !globe_held && !(pressed & (navigation & ~draft_chord))) {
        panel->draft_view ^= 1u;
        pressed &= ~draft_chord;
        draft_edge = 1;
    } else if (panel->draft_view && (panel->page != PAGE_CONDUCTOR ||
               (pressed & (navigation | (1u << BTN_REST) | (1u << BTN_GLOBE))))) {
        panel->draft_view = 0;
        draft_edge = 1;
    }
    if (prior_page == panel->page &&
        (panel->page == PAGE_CONDUCTOR || (panel->page == PAGE_SCENES && conductor_cues_at_bar(panel))) &&
        (buttons & (1u << BTN_REC)) && (pressed & (1u << SMK_BTN_LEFT)) &&
        !seq_held && !repeat_held && !globe_held &&
        !(pressed & ((navigation & ~remove_chord) | (1u << BTN_PLAY)))) {
        panel->cancel_cue = CANCEL_CUE_REQUESTED;
        panel->scene_cleanup = 0;
        pressed &= ~remove_chord;
        cleanup_edge = 1;
    } else if (prior_page == PAGE_SCENES && panel->page == PAGE_SCENES &&
        (buttons & (1u << BTN_REC)) && (pressed & (1u << SMK_BTN_LEFT)) &&
        !(pressed & ((navigation & ~remove_chord) | (1u << BTN_PLAY)))) {
        panel->scene_cleanup = 0;
        panel->conductor_copy_needed = 0;
        if (conductor_cues_at_bar(panel)) panel->scene_status = SCENE_REMOVE_STOP;
        else if (panel->store_request) panel->scene_status = SCENE_REMOVE_BUSY;
        else {
            panel->scene_status = SCENE_NORMAL;
            panel->scene_cleanup = SCENE_CLEANUP_PICK;
        }
        pressed &= ~remove_chord;
        cleanup_edge = 1;
    } else if (panel->scene_cleanup) {
        if (panel->page != PAGE_SCENES || conductor_cues_at_bar(panel) ||
            (pressed & ~(1u << BTN_REC))) {
            panel->scene_cleanup = 0;
            cleanup_edge = 1;
        } else if (pressed & (1u << BTN_REC)) {
            if (panel->store_request) panel->scene_status = SCENE_REMOVE_BUSY;
            else if (panel->scene_cleanup >= SCENE_CLEANUP_CONFIRM)
                conductor_remove_setup(panel, panel->scene_cleanup - SCENE_CLEANUP_CONFIRM);
            panel->scene_cleanup = 0;
            pressed &= ~(1u << BTN_REC);
            cleanup_edge = 1;
        }
    }
    if (cleanup_edge || draft_edge) panel->conductor_rec_pending = 0;
    if (!panel->draft_view && conductor_save_page(panel->page) &&
        (pressed & (1u << BTN_REC)) && !(pressed & navigation))
        panel->conductor_rec_pending = 1;
    if (panel->page == PAGE_DRUMS && (pressed & (1u << BTN_REC)) && !(pressed & navigation) &&
        !repeat_held && !seq_held && (buttons & (1u << BTN_TIE)))
        conductor_drum_reset(panel, 0);
#endif
    if ((pressed & (1u << BTN_REC)) && !(pressed & navigation) && panel->page != PAGE_DRUMS && panel->page != PAGE_CONDUCTOR && panel->page != PAGE_MIX && panel->page != PAGE_SCENES && panel->page != PAGE_KITS && panel->page != PAGE_DILLO_SOUND &&
        panel->page != PAGE_DRUM_PRESETS && panel->page != PAGE_INSTRUMENTS && panel->page != PAGE_SYNTH_PRESETS) {
#ifdef ELITE_DILLO
        if (panel->page == PAGE_DILLO) dillo_rec(panel, buttons & (1u << BTN_TIE));
        else
#endif
        if (panel->page == PAGE_ASSIGN) {
            layout_requested = 1;
        } else if (panel->page == PAGE_PATCH && !(pressed & (1u << BTN_ASSIGN))) {
            if (panel->save_mode == SAVE_CONFIRM) confirm_requested = 1;
            else if (!panel->save_mode) {
                if (panel->library_kind == LIB_FACTORY || panel->library_kind == LIB_IMPORTED) {
                    panel->library_kind = LIB_SOUNDS; panel->preset_bank = 0; panel->library_slot = 0;
                }
                panel->save_mode = SAVE_PICK;
                panel->library_status = LIB_NORMAL;
                library_name(panel);
            }
        } else if (!(pressed & (1u << BTN_ASSIGN))) {
            if (panel->page == PAGE_STEPS && !panel->record && (buttons & (1u << BTN_TIE))) {
                if (!sound->running && !(pressed & (1u << BTN_PLAY))) {
                    save_phrase_undo(panel, UNDO_NEW);
                    memset(sound->steps, TG_REST, sizeof sound->steps);
                    sound->length = 1;
                    panel->cursor = 0;
                    panel->record = REC_NEW;
                }
            } else {
                panel->record = panel->record ? REC_OFF : REC_EDIT;
            }
            panel->record_key = 0;
            panel->page = PAGE_STEPS;
        }
    }
    unsigned reset_transpose = !panel->record && (buttons & (1u << BTN_SEQ)) &&
                               (pressed & (1u << BTN_REST));
    if (reset_transpose) sound->transpose = 0;
    /* NOTE REPEAT gives CHORD to the drum roll accent on Conductor. */
    if ((pressed & (1u << BTN_TIE)) &&
        (performance_page(panel->page) || (panel->page == PAGE_CONDUCTOR && !repeat_held))) {
        if (panel->page == PAGE_ARP) panel->arp_latch ^= 1u;
        else panel->hold ^= 1u;
    }
    if (pressed & (1u << BTN_PLAY)) {
        if (buttons & (1u << BTN_SEQ)) {
            sound->clock.source ^= 1u;
            sound->running = 0;
        } else if (sound->clock.source == TG_CLOCK_USB) {
            panel->clock_toggle = 1;
#ifdef ELITE_DRUMS808
            /* Let the later pad dispatch see the local transport change too. */
            sound->running = !tg_clock_active(&sound->clock);
            sound->clock.state = sound->running ? TG_CLOCK_WAITING : TG_CLOCK_STOPPED;
#endif
        } else sound->running ^= 1u;
        if (sound->clock.source != TG_CLOCK_USB || !sound->arp) panel->hold = 0; /* Keep a held arp chord available to USB transport. */
    }
    if (panel->record) panel->hold = 0;

    int arrow = !!(pressed & (1u << SMK_BTN_RIGHT)) - !!(pressed & (1u << SMK_BTN_LEFT));
    if (arrow && panel->page == PAGE_ASSIGN) {
        if (panel->assign_source == MAP_PAD) {
            uint8_t *note = &panel->pad_note[panel->assign_slot];
            *note = (uint8_t)bounded(*note + arrow, 12, 108);
        } else {
            uint8_t *mapping = panel->assign_source == MAP_FADER ?
                &panel->fader_map[panel->fader_bank][panel->assign_slot] :
                &panel->knob_map[panel->knob_bank][panel->assign_slot];
            *mapping = (uint8_t)((*mapping + CTRL_COUNT + arrow) % CTRL_COUNT);
            fader_caught = 0;
        }
    } else if (arrow < 0 && panel->page == PAGE_PATCH && !panel->save_mode &&
               panel->library_kind != LIB_PHRASES && (buttons & (1u << BTN_TIE))) {
        if (panel->compare_available) compare_patch(panel);
    } else if (arrow && panel->page == PAGE_PATCH) {
        panel->compare_active = 0;
        unsigned banks = panel->library_kind == LIB_IMPORTED ? (tg_imported_match_count(panel->import_category) + TG_PADS - 1u) / TG_PADS :
                         panel->library_kind == LIB_FACTORY ? TG_PRESET_BANKS : TG_USER_SLOTS / TG_PADS;
        if (!banks) banks = 1;
        panel->preset_bank = (uint8_t)((panel->preset_bank + banks + arrow) % banks);
        panel->library_slot = (uint16_t)(panel->preset_bank * TG_PADS + panel->library_slot % TG_PADS);
        if (panel->library_kind == LIB_IMPORTED) {
            unsigned count = tg_imported_match_count(panel->import_category);
            if (count && panel->library_slot >= count) panel->library_slot = (uint16_t)(count - 1);
            panel->library_status = LIB_NORMAL;
        }
        if (panel->save_mode) panel->save_mode = SAVE_PICK;
        panel->library_status = LIB_NORMAL;
    }
    else if (arrow && panel->page == PAGE_MANUAL)
        manual_page(panel, arrow);
    else if (arrow && panel->page == PAGE_STEPS) {
        if (!panel->record && (buttons & (1u << BTN_TIE))) {
            phrase_edit_arrow(panel, arrow);
        } else {
            panel->record_key = 0;
            int page = panel->cursor / TG_PADS;
            unsigned target_page = (page + TG_PAGES + arrow) % TG_PAGES;
            panel->cursor = (uint8_t)bounded(target_page * TG_PADS + panel->cursor % TG_PADS, 0, TG_STEPS - 1);
        }
    }
#ifdef ELITE_DILLO
    else if (arrow && panel->page == PAGE_DILLO) {
        if ((buttons & (1u << BTN_TIE)) &&
            (!panel->dillo_record || (panel->dillo_record == DILLO_REC_DIRECT && arrow < 0))) {
            dillo_take_restored = arrow < 0 && panel->dillo_undo == UNDO_NEW;
            dillo_edit_arrow(panel, arrow);
        } else if (panel->dillo_record < DILLO_REC_PITCH)
            panel->dillo_cursor = (uint8_t)((panel->dillo_cursor + DILLO_STEPS + arrow) % DILLO_STEPS);
        else if (arrow < 0) {
            if (panel->dillo_record == DILLO_REC_PITCH) {
                if (panel->dillo_pitch_replace) panel->dillo_pitch_replace = 0;
                else if (panel->dillo_pitch_count > panel->dillo_used) panel->dillo_pitch_count--;
            } else if (buttons & (1u << BTN_TIE)) {
                panel->dillo_record = DILLO_REC_PITCH;
                panel->dillo_pitch_replace = 0;
            } else if (panel->dillo_time_edit) panel->dillo_time_edit = 0;
            else if (panel->dillo_time_count &&
                       panel->dillo_time[--panel->dillo_time_count].event == DILLO_NOTE) {
                panel->dillo_used--;
            }
            panel->dillo_take_error = 0;
        }
    }
#endif
#ifdef ELITE_DRUMS808
    else if (arrow && panel->page == PAGE_DILLO_SOUND)
        panel->dillo_sound_page ^= 1u;
    else if (arrow && panel->page == PAGE_SCENES) {
        panel->scene_cursor = (uint8_t)((panel->scene_cursor + CONDUCTOR_SCENES + arrow) % CONDUCTOR_SCENES);
        panel->scene_status = SCENE_NORMAL;
    } else if (arrow && panel->page == PAGE_MIX) {
        unsigned position = conductor_lane_position(panel->mixer_lane);
        panel->mixer_lane = conductor_lane_order[(position + BAND_LANES + arrow) % BAND_LANES];
    }
    else if (arrow && panel->page == PAGE_CONDUCTOR)
    {
        int delta = panel->band_bottom ? 2 * arrow : arrow;
        panel->band_bank = (uint8_t)((panel->band_bank + BAND_BANKS + delta) % BAND_BANKS);
        panel->conductor_bank = (uint8_t)(panel->band_bank >= BAND_BANKS / 2u); /* the saved half */
    }
    else if (arrow && panel->page == PAGE_DRUMS) {
        if (arrow < 0 && !repeat_held && !seq_held && (buttons & (1u << BTN_TIE))) {
            conductor_drum_reset(panel, 1);
            drum_undo_edge = 1;
        } else step_drum_voice(panel, arrow);
    }
    else if (arrow && panel->page == PAGE_DRUM_PRESETS)
        { panel->drum_preset_bank = (uint8_t)((panel->drum_preset_bank + rhythm_banks(panel) + arrow) % rhythm_banks(panel)); panel->rhythm_cursor = 0; }
#ifdef ELITE_SYNTH_PRESETS
    else if (arrow && panel->page == PAGE_SYNTH_PRESETS) {
        synth_preset_step(panel, arrow);
        panel->synth_cursor = 0;
    } else if (panel->page == PAGE_SYNTH_PRESETS && library_active(panel) && (pressed & (1u << BTN_PARA))) {
        panel->synth_info = (uint8_t)((panel->synth_info + 1u) % 3u); /* INFO */
        panel->globe_tap_pending = 0;
    } else if (panel->page == PAGE_SYNTH_PRESETS && (pressed & (1u << BTN_PARA))) {
        /* In LIBRARY, PATTERNS and DUETS are separate shelves: PARA only jumps between sources. */
        unsigned groups = library_active(panel) ? synth_preset_sources(panel) : synth_preset_groups(panel);
        if (library_active(panel) && synth_preset_duets(panel)) groups = 0;
        if (groups) panel->synth_preset_group = (uint8_t)((panel->synth_preset_group + 1u) % groups);
        panel->synth_preset_page = 0;
        panel->globe_tap_pending = 0;
    }
#endif
#endif
    else if (arrow && panel->page != PAGE_FX && panel->page != PAGE_GLOBAL &&
             panel->page != PAGE_FILTER_ENV && panel->page != PAGE_ARP && panel->page != PAGE_KITS &&
             panel->page != PAGE_DRUM_PRESETS && panel->page != PAGE_INSTRUMENTS && panel->page != PAGE_SYNTH_PRESETS)
        sound->patch.sub_mode = (sound->patch.sub_mode + 3 + arrow) % 3u;

#ifdef ELITE_DRUMS808
    if ((pressed & (1u << BTN_PARA)) && globe_held &&
        (panel->page == PAGE_CONDUCTOR || panel->page == PAGE_SCENES)) {
        panel->page = panel->page == PAGE_CONDUCTOR ? PAGE_SCENES : PAGE_CONDUCTOR;
        panel->scene_status = SCENE_NORMAL;
        panel->globe_tap_pending = 0;
    } else if ((pressed & (1u << BTN_PARA)) && globe_held &&
               (panel->page == PAGE_DRUMS || panel->page == PAGE_KITS)) {
        panel->page = panel->page == PAGE_DRUMS ? PAGE_KITS : PAGE_DRUMS;
        panel->kit_status = KIT_NORMAL;
        panel->globe_tap_pending = 0;
    } else if ((pressed & (1u << BTN_PARA)) && !globe_held && panel->page == PAGE_DILLO_SOUND) {
        panel->dillo_sound_bank = (uint8_t)((panel->dillo_sound_bank + 1u) % dillo_sound_banks());
        panel->dillo_sound_status = KIT_NORMAL;
    } else if ((pressed & (1u << BTN_PARA)) && panel->page == PAGE_CONDUCTOR) {
        /* PARA mutes: on the Band, the instrument whose pads were touched last. */
        unsigned lane = panel->conductor_lane == PAGE_DILLO ? MIX_DILLO :
                        panel->conductor_lane == PAGE_STEPS ? MIX_TELEGRAM :
                        drum_part(panel) ? MIX_808_B : MIX_808;
        conductor_toggle_pattern(panel, lane);
    }
#endif
    if ((pressed & (1u << BTN_PARA)) && !dillo_shortcut && panel->page != PAGE_GLOBAL && panel->page != PAGE_ARP && panel->page != PAGE_DILLO && panel->page != PAGE_DRUMS && panel->page != PAGE_CONDUCTOR && panel->page != PAGE_MIX && panel->page != PAGE_SCENES && panel->page != PAGE_KITS && panel->page != PAGE_DILLO_SOUND &&
        panel->page != PAGE_DRUM_PRESETS && panel->page != PAGE_INSTRUMENTS && panel->page != PAGE_SYNTH_PRESETS) {
        if (panel->page == PAGE_PATCH) {
            panel->library_kind = (panel->library_kind + 1u) % (tg_imported_count() ? 4u : 3u);
            panel->compare_active = 0;
            panel->preset_bank = 0;
            panel->library_slot = 0;
            panel->save_mode = SAVE_NONE;
            panel->library_status = LIB_NORMAL;
        } else if (buttons & (1u << SMK_BTN_KNOB_BANK)) {
            sound->patch.pwm = (sound->patch.pwm + 1u) % TG_PWM_SOURCE_COUNT;
            fader_caught = 0;
        }
        else if (buttons & (1u << SMK_BTN_FADER_BANK)) sound->patch.amp_gate ^= 1u;
        else if (panel->page == PAGE_FILTER_ENV || (panel->page == PAGE_MANUAL && panel->fader_bank == 1)) {
            panel->page = panel->page == PAGE_FILTER_ENV ? PAGE_MANUAL : PAGE_FILTER_ENV;
            fader_caught = 0;
        }
#ifdef ELITE_DRUMS808
        /* PARA mutes the instrument on screen: on Telegram's home and steps, its phrase. */
        else if (panel->page == PAGE_TONE || panel->page == PAGE_STEPS)
            conductor_toggle_pattern(panel, MIX_TELEGRAM);
#endif
        else sound->patch.trigger = (sound->patch.trigger + 1u) % 3u;
    }

    int oct = !!(pressed & (1u << FM1_BTN_OCT_UP)) - !!(pressed & (1u << FM1_BTN_OCT_DOWN));
    if (oct) {
        int value = panel->octave + oct;
        panel->octave = (int8_t)(value < -2 ? -2 : value > 2 ? 2 : value);
    }

    home_navigation(panel, pressed, buttons, frames);
    if (panel->patch_bank_pending) {
        if (panel->page != PAGE_PATCH || panel->library_kind != LIB_FACTORY || panel->save_mode ||
            (pressed & (navigation & ~(1u << BTN_PATCH))))
            panel->patch_bank_pending = 0;
        else if (!(buttons & (1u << BTN_PATCH))) {
            panel->patch_bank_pending = 0;
            open_sound_bank(panel);
        }
    }
    if (!arp_held && panel->arp_tap_pending && prior_page == PAGE_ARP) {
        panel->arp_tap_pending = 0;
        if (panel->page == PAGE_ARP) panel->page = PAGE_TONE;
    }
    if (arp_held && !globe_held && !repeat_held && panel->arp_tap_pending &&
        !(pressed & (navigation | (1u << BTN_REC) | (1u << BTN_SCALE))) &&
        (performance_page(panel->page) || panel->page == PAGE_CONDUCTOR) && panel->page != PAGE_GLOBAL &&
        (uint32_t)(frames - arp_press_frame) >= ARP_HOLD_FRAMES &&
        !(down & ~transpose_down & arp_command_keys)) {
        panel->page = PAGE_ARP;
        panel->arp_tap_pending = 0;
    }
    if (prior_page != panel->page || panel->record || (pressed & (1u << BTN_ASSIGN)))
        panel->key_transpose_armed = 0;
    else if ((pressed & (1u << BTN_SCALE)) &&
             (performance_page(panel->page) || panel->page == PAGE_STEPS || panel->page == PAGE_CONDUCTOR))
        panel->key_transpose_armed ^= 1u;
    unsigned dismiss_focus = prior_page != panel->page ||
        ((pressed & (1u << BTN_PLAY)) && (buttons & (1u << BTN_SEQ))) ||
        (pressed & (navigation | (1u << BTN_REC) | (1u << SMK_BTN_KNOB_BANK) | (1u << BTN_SCALE)));
    if (dismiss_focus || (uint32_t)(frames - control_focus_frame) >= CONTROL_FOCUS_FRAMES)
        panel->focus_control = panel->arp_focus = 0;
    unsigned focus_allowed = !dismiss_focus && performance_page(panel->page);
    if (panel->page != PAGE_PATCH) panel->save_mode = SAVE_NONE;

    panel->repeat_edit = repeat_held && focus_allowed && !panel->record &&
                         !(buttons & ((1u << BTN_SEQ) | (1u << SMK_BTN_KNOB_BANK)));
    if (!performance_page(panel->page) || reset_transpose) {
        panel->repeat_gesture = panel->repeat_latched = 0;
    } else if (plain_pad_toggle) {
        panel->pad_keys ^= 1u;
        panel->repeat_on = panel->repeat_gesture = 0;
        panel->pads_down = panel->repeat_latched = 0;
    } else if (pressed & (1u << BTN_REST)) panel->repeat_gesture = 1;
    if (dismiss_focus) panel->repeat_gesture = 0;
    if (!repeat_held && panel->repeat_gesture) {
        panel->repeat_gesture = 0;
        panel->repeat_on ^= 1u;
        panel->pads_down = panel->repeat_latched = 0;
    }
    if (panel->repeat_edit) panel->focus_control = panel->arp_focus = 0;
#ifdef ELITE_DRUMS808
    unsigned home_roll = panel->page == PAGE_CONDUCTOR && !seq_held && !globe_held && !panel->conductor_seq_pending;
    if (!home_roll || (pressed & ~(1u << BTN_REST))) panel->drum_clear_pending = 0;
    else if (pressed & (1u << BTN_REST)) {
        panel->drum_clear_pending = 1;
        drum_repeat_frame = frames;
    }
    unsigned drum_repeat = repeat_held && !seq_held && (panel->page == PAGE_DRUMS || home_roll);
    unsigned voice_pick = panel->page == PAGE_DRUMS && !repeat_held && !seq_held && !globe_held &&
                          (buttons & (1u << SMK_BTN_KNOB_BANK));
    unsigned drum_grid_changed = (panel->drums_repeat && (!drum_repeat || panel->page != prior_page)) ||
                                 (drum_repeat && drum_part(panel) != prior_drum_focus) ||
                                 (panel->drums_voice_pick && !voice_pick && !drum_repeat);
    panel->drums_repeat = (uint8_t)drum_repeat;
    if (home_roll && panel->drums_repeat) panel->conductor_rec_pending = 0;
    panel->drums_voice_pick = (uint8_t)voice_pick;
    drum_roll *drum_roll_edit = drum_part(panel) ? &panel->drums_b.roll : &panel->drums_roll;
    unsigned focused_drum = drum_part(panel) ? DRUM_LATCH_B : DRUM_LATCH_A;
    unsigned momentary = panel->drums_repeat ? focused_drum : 0;
    clear_drum_rolls(panel, DRUM_LATCH_BOTH & ~(panel->drum_latch | momentary));
#endif

    for (unsigned k = 0; k < 8; k++) {
#ifdef ELITE_DRUMS808
        if (panel->page == PAGE_MIX) break; /* Read mixer knobs after pad selection. */
#endif
        int delta = fm1_enc_take(k);
        if (!delta) continue;
#ifdef ELITE_DRUMS808
        panel->strip_knob = (uint8_t)(k + 1u);
        panel->strip_frame = frames;
#endif
#ifdef ELITE_DILLO
        if (panel->page == PAGE_DILLO) {
            if (k == 0) set_control(sound, CTRL_LEVEL, bounded(sound->master + delta, 0, 127));
            else if (panel->dillo_record >= DILLO_REC_PITCH) {
                if (k == 5 && panel->dillo_record == DILLO_REC_TIME) {
                    if (panel->dillo_time_edit || (!globe_held && (buttons & (1u << BTN_TIE)))) {
                        dillo_step_deltas[1] = delta;
                    } else if (panel->dillo_take_gate) {
                        panel->dillo_take_gate = (uint8_t)bounded(panel->dillo_take_gate + delta * 5, 5, 100);
                    }
                }
                if (k == 7 && panel->dillo_record == DILLO_REC_TIME) dillo_step_deltas[3] = delta;
            } else if (k >= 4) {
                dillo_step_deltas[k - 4u] = delta;
            }
            continue;
        }
#endif
#ifdef ELITE_DRUMS808
        if (panel->page == PAGE_DILLO_SOUND) {
            dillo_sound before = panel->dillo_sound;
            bb_dillo_patch *patch = &panel->dillo_sound.tone;
            dillo_decay *decay = &panel->dillo_sound.decay;
            if (panel->dillo_sound_page) {
                if (k == 0) patch->attack_ms = (uint8_t)bounded(patch->attack_ms + delta, 1, 255);
                else if (k == 1) patch->tracking = (uint8_t)bounded(patch->tracking + delta, 0, 100);
                else if (k == 2) decay->accent = (uint8_t)bounded(decay->accent + delta, 0, DILLO_DECAY_MAX);
                else if (k == 3) decay->amp = (uint8_t)bounded(decay->amp + delta, 0, DILLO_DECAY_MAX);
                else if (k == 4) decay->sweep = (uint16_t)bounded(decay->sweep + delta, 0, DILLO_SWEEP_COUNT - 1);
            } else {
                switch (k) {
                case 0: patch->cutoff_hz = step_dillo_cutoff(patch->cutoff_hz, delta); break;
                case 1: patch->resonance_x100 = (uint16_t)bounded(patch->resonance_x100 + 20 * delta, 0, 2000); break;
                case 2: patch->envelope = (uint8_t)bounded(patch->envelope + delta, 0, 100); break;
                case 3: patch->decay = (uint8_t)bounded(patch->decay + delta, 0, 100); break;
                case 4: patch->accent = (uint8_t)bounded(patch->accent + delta, 0, 100); break;
                case 5: patch->slide_ms = (uint8_t)bounded(patch->slide_ms + delta, 1, 255); break;
                case 6: patch->wave = delta > 0; break;
                case 7: patch->drive = (uint8_t)bounded(patch->drive + delta, 0, 100); break;
                }
            }
            if (memcmp(&panel->dillo_sound, &before, sizeof before)) {
                panel->dillo_sound_status = KIT_NORMAL;
                panel->conductor_edits++;
                panel->conductor_dirty = 1;
            }
            continue;
        }
#ifdef ELITE_SYNTH_PRESETS
        if (panel->page == PAGE_SYNTH_PRESETS) {
            if (k == 0) synth_spin(panel, delta); /* K1 spins the jukebox; the arrows turn pages */
            continue;
        }
#endif
        if (panel->page == PAGE_DRUM_PRESETS) {
            /* K1 flicks through the banks; the arrows step them and wrap. */
            if (k == 0) rhythm_spin(panel, delta); /* K1 spins the jukebox; the arrows turn pages */
            continue;
        }
        if (panel->page == PAGE_SCENES || panel->page == PAGE_KITS || panel->page == PAGE_INSTRUMENTS) continue;
        if (panel->page == PAGE_DRUMS || panel->page == PAGE_CONDUCTOR) {
            if (panel->drums_repeat) panel->drum_clear_pending = 0;
            if (panel->drums_repeat && k == 1) {
                unsigned rate = drum_roll_edit->rate ? drum_roll_edit->rate : 5;
                unsigned next = bounded((int)rate + delta, 1, TG_ARP_RATE_COUNT);
                if (next != drum_roll_edit->rate) {
                    drum_roll_edit->rate = (uint8_t)next;
                    conductor_changed(panel);
                }
            }
            if (panel->drums_repeat && k == 2) {
                unsigned next = bounded(drum_roll_edit->swing + delta, 0, DRUM_ROLL_SWING_MAX);
                if (next != drum_roll_edit->swing) {
                    drum_roll_edit->swing = (uint8_t)next;
                    conductor_changed(panel);
                }
            }
            if (panel->drums_repeat && k == 3) {
                unsigned latch = delta > 0 ? panel->drum_latch | focused_drum :
                                              panel->drum_latch & ~focused_drum;
                if (latch != panel->drum_latch) {
                    panel->drum_latch = (uint8_t)latch;
                    if (delta < 0) clear_drum_rolls(panel, focused_drum);
                    conductor_changed(panel);
                }
            }
            if (panel->page == PAGE_DRUMS && !panel->drums_repeat && !panel->drums_voice_pick &&
                k >= 1 && k <= DRUM_KIT_PARAMS) {
                uint8_t *value = drum_part(panel) ? &panel->drums_b.kit.value[panel->drums_b_track][k - 1u] :
                    &panel->drums_kit.value[panel->drums_track][k - 1u];
                unsigned next = (unsigned)bounded(*value + delta, 0, 127);
                if (*value != next) {
                    *value = (uint8_t)next;
                    conductor_changed(panel);
                }
            }
            if (panel->page == PAGE_CONDUCTOR && !panel->drums_repeat && k == 7) {
                unsigned before = sound->master;
                set_control(sound, CTRL_LEVEL, sound->master + delta);
                if (sound->master != before) conductor_changed(panel);
            }
            if (panel->page == PAGE_CONDUCTOR && !panel->drums_repeat && k == 4) {
                unsigned before = sound->tempo;
                set_control(sound, CTRL_TEMPO, sound->tempo + delta);
                if (sound->tempo != before) conductor_changed(panel);
            }
            if (panel->drums_repeat && !k) {
                unsigned before = sound->master;
                set_control(sound, CTRL_LEVEL, sound->master + delta);
                if (sound->master != before) conductor_changed(panel);
            } else if (panel->page == PAGE_DRUMS && !k && !panel->drums_voice_pick) {
                step_drum_voice(panel, delta);
            } else if (panel->page == PAGE_CONDUCTOR && !panel->drums_repeat) {
                if (k < BAND_LANES) {
                    uint8_t *level = conductor_mix_value(panel, conductor_lane_order[k], MIX_LEVEL);
                    unsigned next = bounded(*level + delta, 0, 127);
                    if (*level != next) {
                        *level = (uint8_t)next;
                        conductor_changed(panel);
                    }
                } else if (k == 5) {
                    int before = sound->patch.value[TG_CUTOFF];
                    set_control(sound, TG_CUTOFF, before + 2 * delta);
                    if (sound->patch.value[TG_CUTOFF] != before) conductor_changed(panel);
                } else if (k == 6) {
                    uint16_t *hz = &panel->dillo_sound.tone.cutoff_hz;
                    uint16_t next = step_dillo_cutoff(*hz, delta);
                    if (*hz != next) {
                        *hz = next;
                        panel->dillo_sound_status = KIT_NORMAL;
                        conductor_changed(panel);
                    }
                }
            }
            continue;
        }
#endif
        if (panel->repeat_edit && k < 4) {
            panel->repeat_gesture = 0;
            if (k == 0) {
                int rate = sound->repeat_rate ? sound->repeat_rate : 5;
                sound->repeat_rate = (uint8_t)bounded(rate + delta, 1, TG_ARP_RATE_COUNT);
            } else if (k == 1) {
                int gate = sound->repeat_gate ? sound->repeat_gate : 75;
                sound->repeat_gate = (uint8_t)bounded(gate + 5 * delta, 5, 100);
            } else if (k == 2) {
                sound->repeat_swing = (uint8_t)bounded(sound->repeat_swing + 2 * delta, 0, 25);
            } else {
                panel->repeat_latch = delta > 0;
                if (!panel->repeat_latch) panel->repeat_latched = 0;
                else if (panel->pads_down & (1u << panel->pad_lead))
                    panel->repeat_latched = panel->pad_lead + 1;
            }
        } else if (panel->page == PAGE_ARP) {
            arp_encoder(panel, k, delta);
        } else if (panel->save_mode) {
            if (k == 1) panel->name_cursor = (uint8_t)bounded(panel->name_cursor + delta, 0, TG_NAME_BYTES - 2);
            else if (k == 0) {
                int c = panel->save_name[panel->name_cursor] + delta;
                panel->save_name[panel->name_cursor] = (char)bounded(c, 32, 126);
            }
        } else if (panel->page == PAGE_ASSIGN) {
            panel->assign_source = MAP_KNOB;
            panel->assign_slot = (uint8_t)k;
        } else if (k == 5 && panel->page == PAGE_STEPS) {
            unsigned gate = tg_phrase_gate(sound);
            unsigned edited = bounded((int)gate + 5 * delta, 5, 100);
            if (edited != gate) {
                if (panel->phrase_undo != UNDO_GATE) save_phrase_undo(panel, UNDO_GATE);
                sound->phrase_gate = (uint8_t)edited;
            }
        } else if (k == 6 && (panel->page == PAGE_STEPS || seq_held)) {
            panel->seq_exit_pending = 0;
            unsigned length = tg_pattern_length(sound);
            unsigned resized = (unsigned)bounded((int)length + delta, 1, TG_STEPS);
            if (resized != length) {
                if (panel->phrase_undo != UNDO_LENGTH) save_phrase_undo(panel, UNDO_LENGTH);
                sound->length = (uint8_t)resized;
            }
            panel->page = PAGE_STEPS;
        } else if (k == 7 && (buttons & (1u << BTN_PATCH))) {
            panel->patch_bank_pending = 0;
            panel->pad_light = (uint8_t)bounded(panel->pad_light + delta * 4, 0, 127);
        } else if (k == 7 && panel->page == PAGE_STEPS && panel->record) {
            unsigned cursor = bounded((int)panel->cursor + delta, 0, TG_STEPS - 1);
            if (cursor != panel->cursor) {
                panel->cursor = (uint8_t)cursor;
                panel->record_key = 0;
            }
        } else if (k == 7 && panel->page == PAGE_STEPS) {
            step_pitch_delta = delta;
        } else if (k == 7 && panel->page == PAGE_PATCH && panel->library_kind == LIB_FACTORY &&
                   tg_imported_count()) {
            open_sound_bank(panel);
        } else if (k >= 6 && panel->page == PAGE_PATCH && panel->library_kind == LIB_IMPORTED) {
            if (k == 7) library_category(panel, delta);
            else {
                int count = (int)tg_imported_match_count(panel->import_category);
                if (count) {
                    unsigned slot = (unsigned)((panel->library_slot + delta % count + count) % count);
                    panel->preset_bank = (uint8_t)(slot / TG_PADS);
                    if (panel->save_mode) {
                        panel->library_slot = (uint16_t)slot;
                        panel->compare_active = 0;
                        panel->library_status = LIB_NORMAL;
                    } else {
                        library_pad(panel, slot % TG_PADS); /* Scrolling plays each sound as it is reached. */
                    }
                }
            }
        } else if (panel->page == PAGE_FILTER_ENV) {
            if (k == 0)
                sound->patch.filter_env_mode = (uint8_t)bounded(sound->patch.filter_env_mode + delta,
                                                                TG_FILTER_ENV_SHARED, TG_FILTER_ENV_CLOSE);
            else if (k == 1) set_control(sound, TG_ENV, sound->patch.value[TG_ENV] + 2 * delta);
            else if (k < 4) {
                unsigned control = CTRL_ACCENT_TONE + k - 2;
                set_control(sound, control, (int)control_value(sound, control) + 2 * delta);
            }
        } else if (panel->page == PAGE_GLOBAL) {
            if (k == 0)
                panel->scale_root = (uint8_t)((panel->scale_root + delta % SCALE_ROOTS + SCALE_ROOTS) % SCALE_ROOTS);
            else if (k == 1)
                panel->scale_type = (uint8_t)bounded(panel->scale_type + delta, 0,
                                  sizeof scale_intervals / sizeof scale_intervals[0] - 1);
            else if (k == 2)
                panel->midi_channel = (uint8_t)bounded(panel->midi_channel + delta, 0, MIDI_CHANNELS - 1);
            else if (k == 3)
                panel->pad_midi_channel = (uint8_t)bounded(panel->pad_midi_channel + delta, 0, MIDI_CHANNELS - 1);
            else if (k == 4)
                panel->midi_tx = delta > 0;
            else if (k == 5)
                select_profile(panel, bounded(panel->profile + delta, 0, TG_LAYOUT_PROFILES - 1));
            else if (k == 6)
                panel->pedal_mode = (uint8_t)bounded(panel->pedal_mode + delta, PEDAL_OFF, PEDAL_HIGH);
        } else if (k == 7 && panel->page == PAGE_MANUAL && panel->fader_bank == 0) {
            sound->patch.sub_mode = (uint8_t)bounded(sound->patch.sub_mode + delta, 0, 2);
            panel->focus_control = 0;
        } else if (k >= 6 && panel->page == PAGE_MANUAL && panel->fader_bank == 2) {
            if (k == 6)
                sound->patch.trigger = (uint8_t)bounded(sound->patch.trigger + delta, TG_GATE_TRIG, TG_LFO_TRIG);
            else
                sound->patch.amp_gate = (uint8_t)bounded(sound->patch.amp_gate + delta, 0, 1);
            panel->focus_control = 0;
        } else if (k == 3 && panel->page == PAGE_MANUAL && panel->fader_bank == 3) {
            sound->tune_cents = (int8_t)bounded(sound->tune_cents + delta, -TG_TUNE_LIMIT, TG_TUNE_LIMIT);
            panel->focus_control = 0;
        } else if (k >= 5 && panel->page == PAGE_MANUAL && panel->fader_bank == 3) {
            if (k == 5) {
                set_control(sound, CTRL_PWM_SOURCE, sound->patch.pwm + delta);
                for (unsigned f = 0; f < 4; f++)
                    if (panel->fader_map[3][f] == CTRL_PWM_SOURCE) fader_caught &= ~(1u << f);
            } else if (k == 6) sound->patch.lfo_wave = (uint8_t)bounded(sound->patch.lfo_wave + delta, TG_LFO_TRI, TG_LFO_NOISE);
            else sound->patch.range = (uint8_t)bounded(sound->patch.range + delta, TG_RANGE_16, TG_RANGE_2);
            panel->focus_control = 0;
        } else {
            if (panel->page == PAGE_FX && k >= sizeof fx_map && k != 7) {
                if (k == 3) {
                    sound->patch.glide_mode = (uint8_t)bounded(sound->patch.glide_mode + delta, TG_GLIDE_OFF, TG_GLIDE_ON);
                    panel->focus_control = 0;
                }
                continue;
            }
            unsigned p = panel->knob_map[panel->knob_bank][k];
            if (panel->page == PAGE_FX) p = k == 7 ? TG_GLIDE : fx_map[k];
            if (k == 4 && panel->page == PAGE_MANUAL && panel->fader_bank == 3) p = TG_VIBRATO;
            unsigned stride = (p == CTRL_TEMPO || control_switch(p) || control_five_positions(p)) ? 1u : p == CTRL_LEVEL ? 4u : 2u;
            set_control(sound, p, (int)control_value(sound, p) + delta * (int)stride);
            if (focus_allowed && (p != CTRL_TEMPO || sound->clock.source != TG_CLOCK_USB)) {
                focus_control(panel, p, k, 0, 0, frames);
                knob_focused = 1;
            }
            for (unsigned f = 0; f < 4; f++)
                if (panel->fader_map[panel->fader_bank][f] == p)
                    fader_caught &= ~(1u << f);
        }
    }
#ifdef ELITE_DRUMS808
    if ((prior_page == PAGE_MIX) != (panel->page == PAGE_MIX)) fader_caught = 0;
#endif
    if (!faders_ready) {
        memcpy(last_fader, faders, sizeof faders);
        memcpy(focus_fader_position, faders, sizeof faders);
        faders_ready = frames > 16u;
    } else {
        for (unsigned i = 0; i < 4; i++) {
            unsigned p = panel->page == PAGE_FILTER_ENV ? CTRL_FILTER_ATTACK + i :
                                                          panel->fader_map[panel->fader_bank][i];
            unsigned from = last_fader[i], to = faders[i], value = control_value(sound, p);
            unsigned target = p == CTRL_TEMPO ? ((value - 30u) * 127u + 135u) / 270u : control_switch(p) ? value*127u : control_five_positions(p) ? (value*127u+2u)/4u : value;
            if (panel->save_mode) {
                fader_caught = 0;
#ifdef ELITE_DRUMS808
            } else if (panel->page == PAGE_MIX && i >= BAND_LANES) {
                fader_caught &= ~(1u << i);
            } else if (panel->page == PAGE_MIX) {
                uint8_t *level = conductor_mix_value(panel, conductor_lane_order[i], MIX_LEVEL);
                if ((from <= *level && to >= *level) || (from >= *level && to <= *level))
                    fader_caught |= 1u << i;
                if ((fader_caught & (1u << i)) && to != *level) {
                    *level = (uint8_t)to;
                    conductor_changed(panel);
                }
#endif
            } else if (panel->page == PAGE_FX || panel->page == PAGE_GLOBAL || panel->page == PAGE_ARP ||
                       panel->page == PAGE_CONDUCTOR || panel->page == PAGE_SCENES || panel->page == PAGE_DILLO ||
                       panel->page == PAGE_DILLO_SOUND || panel->page == PAGE_DRUMS || panel->page == PAGE_KITS ||
                       panel->page == PAGE_DRUM_PRESETS || panel->page == PAGE_INSTRUMENTS ||
                       panel->page == PAGE_SYNTH_PRESETS) {
                if (from != to) fader_caught &= ~(1u << i);
            } else if (panel->page == PAGE_ASSIGN) {
                if (from + 2u < to || to + 2u < from) {
                    panel->assign_source = MAP_FADER;
                    panel->assign_slot = (uint8_t)i;
                }
            } else {
                if ((from <= target && to >= target) || (from >= target && to <= target))
                    fader_caught |= 1u << i;
                if ((fader_caught & (1u << i)) && to != target)
                    set_control(sound, p, p == CTRL_TEMPO ? 30 + (int)(to * 270u / 127u) : control_switch(p) ? (int)(to>=64u) : control_five_positions(p) ? (int)((to*4u+63u)/127u) : (int)to);
            }
            unsigned anchor = focus_fader_position[i];
            if (to + 2u < anchor || anchor + 2u < to) {
                focus_fader_position[i] = (uint8_t)to;
                if (focus_allowed && !knob_focused && panel->page == PAGE_MANUAL &&
                    (p != CTRL_TEMPO || sound->clock.source != TG_CLOCK_USB))
                    focus_control(panel, p, 8u + i, !(fader_caught & (1u << i)), to, frames);
            } else if (panel->focus_control == p + 1u && panel->focus_slot == 8u + i &&
                       (fader_caught & (1u << i))) {
                panel->focus_pickup = 0;
            }
            last_fader[i] = faders[i];
        }
    }

    if (!wheel_ready && frames > 24u) {
        bend_center = wheels[0] >= 256 && wheels[0] <= 768 ? wheels[0] : 512;
        wheel_ready = 1;
    }
    if (wheel_ready) {
        sound->bend = (int16_t)wheel_bend(wheels[0], bend_center);
        sound->mod = (uint8_t)(wheels[1] >> 3);
    } else { sound->bend = 0; sound->mod = 0; }
    unsigned pad_performance = (panel->pad_keys || panel->repeat_on) && performance_page(panel->page);
    if (!pad_performance) panel->pads_down = panel->repeat_latched = 0;
#ifdef ELITE_DILLO
    unsigned time_pick = panel->page == PAGE_DILLO && panel->dillo_record == DILLO_REC_TIME &&
                         !globe_held && (buttons & (1u << BTN_TIE));
    unsigned time_pick_ended = panel->dillo_time_pick && !time_pick;
    unsigned classic_grid_changed = panel->page == PAGE_DILLO && prior_dillo_record != panel->dillo_record &&
        (prior_dillo_record >= DILLO_REC_PITCH || panel->dillo_record >= DILLO_REC_PITCH);
    panel->dillo_time_pick = (uint8_t)time_pick;
    if (panel->dillo_record != DILLO_REC_TIME) panel->dillo_time_edit = 0;
#endif
#ifdef ELITE_DRUMS808
    unsigned rec_save_release = panel->conductor_rec_pending && !(buttons & (1u << BTN_REC));
    unsigned selection_grid_changed =
        (!prior_seq_picker && panel->conductor_seq_pending) ||
        (prior_seq_picker && (!seq_held || !panel->conductor_seq_pending)) ||
        (prior_page != panel->page && (conductor_selection_grid(prior_page) ||
                                      conductor_selection_grid(panel->page))) ||
        (panel->page == PAGE_DILLO_SOUND && prior_dillo_bank != panel->dillo_sound_bank) ||
#ifdef ELITE_SYNTH_PRESETS
        (panel->page == PAGE_SYNTH_PRESETS &&
         (prior_synth_group != panel->synth_preset_group || prior_synth_page != panel->synth_preset_page)) ||
#endif
        (panel->page == PAGE_PATCH &&
         (prior_library_kind != panel->library_kind || prior_preset_bank != panel->preset_bank ||
          (panel->library_kind == LIB_IMPORTED && prior_import_category != panel->import_category)));
    unsigned remapped_launch_rows = 0;
    if (prior_page == PAGE_CONDUCTOR && panel->page == PAGE_CONDUCTOR &&
        !panel->draft_view && !panel->conductor_seq_pending && !panel->drums_repeat) {
        if (panel->band_bank != prior_bank) remapped_launch_rows = 3u; /* every group shows new slots */
        if (panel->band_bottom != prior_bottom) remapped_launch_rows |= 2u;
        /* A drum-focus chord (GLOBE+FX) in the same scan owns its strike. */
        if (drum_part(panel) != prior_drum_focus) remapped_launch_rows |= 1u;
    }
#endif
    int new_pad = -1;
    for (int32_t event; (event = smk_pad_take()) >= 0;) {
        unsigned pad = (unsigned)event >> 8;
        if (pad >= TG_PADS) continue;
#ifdef ELITE_DRUMS808
        if (event & 127) { panel->pad_hit = (uint8_t)(pad + 1u); panel->pad_hit_frame = panel->view_frames; }
#endif
        if (!(event & 127)) {
#ifdef ELITE_DRUMS808
            if (pad < D8_NUM) {
                if (!(panel->drum_latch & DRUM_LATCH_A)) panel->drums_roll.velocity[pad] = 0;
                if (!(panel->drum_latch & DRUM_LATCH_B)) panel->drums_b.roll.velocity[pad] = 0;
            }
#endif
            panel->pads_down &= ~(1u << pad);
            if (panel->audition_step && (panel->audition_step - 1u) % TG_PADS == pad)
                panel->audition_step = 0;
#ifdef ELITE_DRUMS808
            if (panel->dillo_audition == pad + 1u) panel->dillo_audition = 0;
#endif
            continue;
        }
#ifdef ELITE_DRUMS808
        /* A buffered capture strike cannot become a recall or a REC tap save. */
        if (rec_save_release) {
            panel->conductor_rec_pending = 0;
            continue;
        }
        /* Buffered strikes belong to the previous drum overlay or part. */
        if (drum_grid_changed) {
            panel->drum_clear_pending = 0;
            continue;
        }
        /* A strike buffered across a page or bank change belongs to the previous grid. */
        if (selection_grid_changed || cleanup_edge || draft_edge || drum_undo_edge) continue;
        /* A strike queued before a launch row changes cannot address its new slots. */
        if (remapped_launch_rows & (1u << (pad / CONDUCTOR_ROW))) {
            panel->conductor_rec_pending = 0;
            continue;
        }
#endif
#ifdef ELITE_DILLO
        if (time_pick_ended || classic_grid_changed || dillo_take_restored) continue;
#endif
        if (globe_held) {
            panel->globe_tap_pending = panel->arp_tap_pending = 0;
            if (!panel->save_mode) {
                panel->pad_midi_channel = (uint8_t)pad;
                panel->focus_control = panel->arp_focus = 0;
            }
        } else if (panel->page == PAGE_ASSIGN) {
            panel->assign_source = MAP_PAD;
            panel->assign_slot = (uint8_t)pad;
        } else if (pad_performance) {
            panel->repeat_gesture = 0;
            panel->pads_down |= 1u << pad;
            midi_pad_velocity[pad] = (uint8_t)(event & 127);
            panel->pad_lead = (uint8_t)pad;
            if (panel->repeat_on && panel->repeat_latch) panel->repeat_latched = (uint8_t)(pad + 1);
            new_pad = (int)pad;
        } else if (panel->page == PAGE_PATCH) {
            library_pad_pressed = 1;
            library_pad(panel, pad);
#ifdef ELITE_DILLO
        } else if (panel->page == PAGE_DILLO) {
            if (panel->dillo_record >= DILLO_REC_PITCH) {
                if (panel->dillo_record == DILLO_REC_TIME) {
                    if (!time_pick) dillo_time_pad(panel, pad);
                    else if (pad < panel->dillo_time_count) {
                        panel->dillo_time_edit = panel->dillo_time_edit == pad + 1u ? 0 : (uint8_t)(pad + 1u);
                        panel->dillo_take_error = 0;
#ifdef ELITE_DRUMS808
                        panel->dillo_audition = (uint8_t)(pad + 1u);
                        dillo_audition_strike = 1;
#endif
                    }
                }
                else if (pad < panel->dillo_pitch_count)
                    panel->dillo_pitch_replace = panel->dillo_pitch_replace == pad + 1u ? 0 : (uint8_t)(pad + 1u);
                continue;
            }
            panel->dillo_cursor = (uint8_t)pad;
            dillo_step step = panel->dillo_pattern.steps[pad];
            unsigned length = panel->dillo_pattern.length;
            if (buttons & (1u << BTN_REST)) {
                dillo_pad_command = 1;
                step = (dillo_step){0};
            } else if (buttons & (1u << BTN_TIE)) {
                dillo_pad_command = 1;
                step = step.event == DILLO_TIE ? (dillo_step){0} :
                    (dillo_step){.event = DILLO_TIE, .gate_percent = 65};
                if (step.event && length <= pad) length = pad + 1u;
            }
            dillo_edit_step(panel, step, length, UNDO_STEP);
#ifdef ELITE_DRUMS808
            panel->dillo_audition = 0;
            /* Resolve the preview after any same-scan step edits. */
            if (!panel->dillo_record && !conductor_running(panel) &&
                !(buttons & ((1u << BTN_SEQ) | (1u << BTN_REST) | (1u << BTN_TIE) | (1u << BTN_REC)))) {
                panel->dillo_audition = (uint8_t)(pad + 1u);
                dillo_audition_strike = 1;
            }
#endif
#endif
#ifdef ELITE_DRUMS808
        } else if (panel->page == PAGE_DILLO_SOUND) {
            panel->conductor_rec_pending = 0;
            if (pad < DILLO_SOUND_PAGE_SLOTS)
                conductor_dillo_sound_pad(panel, pad + panel->dillo_sound_bank * DILLO_SOUND_PAGE_SLOTS,
                                         buttons & (1u << BTN_REC));
        } else if (panel->page == PAGE_KITS) {
            panel->conductor_rec_pending = 0;
            conductor_kit_pad(panel, pad, buttons & (1u << BTN_REC));
#ifdef ELITE_SYNTH_PRESETS
        } else if (panel->page == PAGE_SYNTH_PRESETS) {
            panel->conductor_rec_pending = 0;
#ifdef ELITE_ACID_DUETS
            if (synth_preset_duets(panel)) {
                conductor_duet_pad(panel, pad, buttons & (1u << BTN_TIE));
                if (duet_at(panel, pad)) { panel->synth_cursor = (uint8_t)(pad % DUET_PADS); panel->synth_info = 1; }
            } else
#endif
            if (synth_preset_at(panel, pad)) {
                conductor_synth_preset_pad(panel, pad);
                panel->synth_cursor = (uint8_t)pad;
                panel->synth_info = 1;
            }
#endif
        } else if (panel->page == PAGE_DRUM_PRESETS) {
            panel->conductor_rec_pending = 0;
            rhythm_pad(panel, pad);
        } else if (panel->page == PAGE_INSTRUMENTS) {
            panel->conductor_rec_pending = 0;
            pick_instrument(panel, pad, frames);
        } else if (panel->page == PAGE_SCENES) {
            panel->conductor_rec_pending = 0;
            if (panel->scene_cleanup) {
                if (pad < CONDUCTOR_SCENES) {
                    panel->scene_cursor = (uint8_t)pad;
                    panel->scene_cleanup = SCENE_CLEANUP_PICK;
                    if (!(panel->scene_slots & (1u << pad))) panel->scene_status = SCENE_EMPTY;
                    else if (conductor_scene_in_use(panel, pad)) panel->scene_status = SCENE_REMOVE_IN_USE;
                    else {
                        panel->scene_cleanup_assets = conductor_reclaim_assets(panel, pad);
                        panel->scene_cleanup = (uint8_t)(SCENE_CLEANUP_CONFIRM + pad);
                    }
                }
                continue;
            }
            unsigned store = buttons & (1u << BTN_REC);
            if (pad < CONDUCTOR_SCENES) {
                unsigned action = SCENE_LOAD;
                if (store) action = SCENE_STORE;
                else if (buttons & (1u << BTN_TIE)) action = SCENE_MIX;
                conductor_scene_pad(panel, pad, action);
            } else if (store) conductor_scene_pad(panel, pad - CONDUCTOR_SCENES, SCENE_FORK);
        } else if (panel->page == PAGE_MIX) {
            if (pad < BAND_LANES) panel->mixer_lane = conductor_lane_order[pad];
        } else if (panel->drums_repeat) {
            panel->drum_clear_pending = 0;
            if (pad < D8_NUM) {
                uint8_t *velocity = &drum_roll_edit->velocity[pad];
                unsigned strike = buttons & (1u << BTN_TIE) ? 127 : (unsigned)(event & 127);
                *velocity = (panel->drum_latch & focused_drum) && *velocity ? 0 : (uint8_t)strike;
            }
        } else if (panel->page == PAGE_CONDUCTOR) {
            panel->conductor_rec_pending = 0;
            if (panel->draft_view) {
                if (pad < BAND_LANES)
                    conductor_restore_draft(panel, conductor_lane_order[pad]);
            } else if (panel->conductor_seq_pending) {
                /* SEQ + any pad of an instrument's group opens that instrument's editor. */
                panel->conductor_seq_pending = (uint8_t)band_lane_editor(band_group_lane(pad));
            } else band_pad(panel, pad, buttons & (1u << BTN_REC));
        } else if (panel->page == PAGE_DRUMS) {
            if (panel->drums_voice_pick) {
                if (pad < DRUM_SOUNDS) drum_pick_sound(panel, pad);
                continue;
            }
            unsigned part = drum_part(panel);
            drum_pattern *pattern = part ? &panel->drums_b.pattern : &panel->drums_pattern;
            unsigned track = part ? panel->drums_b_track : panel->drums_track;
            unsigned bit = 1u << part;
            if (!(panel->drums_restore & bit)) {
                conductor_drum_backup[part] = *pattern;
                panel->drums_undo_track[part] = (uint8_t)track;
                panel->drums_undo |= (uint8_t)bit;
            }
            uint16_t mask = (uint16_t)(1u << pad);
            if (buttons & (1u << BTN_TIE)) {
                pattern->hits[track] |= mask;
                pattern->accents[track] ^= mask;
            } else {
                pattern->hits[track] ^= mask;
                pattern->accents[track] &= pattern->hits[track];
            }
            if ((pattern->hits[track] & mask) && !conductor_running(panel)) {
                drum_audition *strike = &panel->drum_auditions[part];
                strike->track = (uint8_t)track;
                strike->accent = !!(pattern->accents[track] & mask);
                strike->generation++;
            }
            if (drum_part(panel)) conductor_changed(panel);
#endif
        } else if (panel->page == PAGE_TONE) {
            struct patch_snapshot previous = snapshot_patch(panel);
            select_preset(panel, (panel->preset / TG_PADS) * TG_PADS + pad);
            remember_patch(panel, &previous);
        } else if (panel->page != PAGE_FX && panel->page != PAGE_GLOBAL &&
                   panel->page != PAGE_FILTER_ENV && panel->page != PAGE_ARP && panel->page != PAGE_MIX && panel->page != PAGE_SCENES && panel->page != PAGE_KITS && panel->page != PAGE_DILLO_SOUND &&
        panel->page != PAGE_DRUM_PRESETS && panel->page != PAGE_INSTRUMENTS && panel->page != PAGE_SYNTH_PRESETS) {
            unsigned step = step_page_base(panel, head) + pad;
            if (step >= TG_STEPS) continue;
            panel->page = PAGE_STEPS;
            panel->seq_exit_pending = 0;
            panel->record_key = 0;
            panel->cursor = (uint8_t)step;
            if (buttons & (1u << BTN_REST)) {
                if (sound->steps[step] != TG_REST) save_phrase_undo(panel, UNDO_STEP);
                sound->steps[step] = TG_REST;
                cleared_step = 1;
            }
            if ((buttons & (1u << BTN_TIE)) && (sound->steps[step] & 127)) {
                save_phrase_undo(panel, UNDO_STEP);
                sound->steps[step] ^= TG_TIE;
                step_pitch_delta = 0;
            }
            panel->audition_step = 0;
            if (!panel->record && !sound->running && !tg_clock_active(&sound->clock) &&
                !(buttons & ((1u << BTN_TIE) | (1u << BTN_REST) | (1u << BTN_SEQ))) &&
                step < tg_pattern_length(sound) && (sound->steps[step] & 127u)) {
                panel->audition_step = (uint8_t)(step + 1u);
                panel->audition_velocity = (uint8_t)(event & 127);
                panel->audition_strike = 1;
            }
        }
    }

    /* A pad selection can arrive in the same scan as the pitch encoder. */
    if (step_pitch_delta && !cleared_step && panel->page == PAGE_STEPS &&
        panel->cursor < tg_pattern_length(sound)) {
        uint8_t *entry = &sound->steps[panel->cursor];
        int pitch = *entry & 127u;
        int edited = bounded(pitch + step_pitch_delta, 1, 127);
        if (pitch && edited != pitch) {
            if (panel->phrase_undo != UNDO_PITCH || panel->undo_cursor != panel->cursor)
                save_phrase_undo(panel, UNDO_PITCH);
            *entry = (uint8_t)(edited | (*entry & TG_TIE));
        }
    }

#ifdef ELITE_DILLO
    /* Resolve pad selection and event changes before editing the chosen step. Time Pass
     * append still uses the gate set before the event pad is dispatched. */
    if (panel->page == PAGE_DILLO && panel->dillo_time_edit) {
        unsigned index = panel->dillo_time_edit - 1u;
        dillo_step *step = &panel->dillo_time[index];
        if (dillo_step_deltas[1] && step->gate_percent)
            step->gate_percent = (uint8_t)bounded(step->gate_percent + dillo_step_deltas[1] * 5, 5, 100);
        if (dillo_step_deltas[3] && step->event == DILLO_NOTE) {
            uint8_t *pitch = &panel->dillo_pitches[dillo_time_rank(panel, index)];
            *pitch = (uint8_t)bounded(*pitch + dillo_step_deltas[3], 0, 127);
        }
    } else if (panel->page == PAGE_DILLO && panel->dillo_record < DILLO_REC_PITCH &&
               !classic_grid_changed && !dillo_pad_command && !dillo_take_restored) {
        dillo_edit_turns(panel, dillo_step_deltas);
    }
#endif
#ifdef ELITE_DRUMS808
    if (panel->drum_clear_pending && !repeat_held) {
        panel->drum_clear_pending = 0;
        if ((uint32_t)(frames - drum_repeat_frame) < DRUM_REPEAT_HOLD_FRAMES)
            clear_drum_rolls(panel, DRUM_LATCH_BOTH);
    }
    if (panel->page == PAGE_MIX) {
        if (!globe_held && (pressed & (1u << BTN_PARA)))
            conductor_toggle_pattern(panel, panel->mixer_lane);
        conductor_mixer_knobs(panel);
    }
    if (panel->conductor_seq_pending && !seq_held) {
        conductor_editor(panel, panel->conductor_seq_pending);
        panel->conductor_seq_pending = 0;
    }
#endif
#ifdef ELITE_DRUMS808
    if (momentary && !(panel->drum_latch & momentary)) {
        /* The scanner clears pad ownership even if its event queue is full. */
        uint32_t config = fm1_icfg();
        fm1_irq_off();
        unsigned held = smk_pad_own;
        fm1_icfg_set(config);
        for (unsigned pad = 0; pad < D8_NUM; pad++)
            if (!(held & (1u << pad))) drum_roll_edit->velocity[pad] = 0;
    }
#endif
    if (panel->audition_step) {
        /* A full event queue can lose a release. Follow the scanner's
         * ownership bitmap so a preview cannot outlive the physical pad. */
        uint32_t config = fm1_icfg();
        fm1_irq_off();
        unsigned held = smk_pad_own & (1u << ((panel->audition_step - 1u) % TG_PADS));
        fm1_icfg_set(config);
        if (!held) panel->audition_step = 0;
    }

    if (prior_page != panel->page) {
        panel->focus_control = panel->arp_focus = 0;
        panel->key_transpose_armed = 0;
    }

    uint64_t new_keys = down & ~transpose_down;
    if (seq_held && new_keys) panel->seq_exit_pending = 0;
    transpose_down = down;
    transpose_keys &= down;
    const uint64_t reset_key = UINT64_C(1) << (TG_KEYS - 1);
    const uint64_t scale_keys = ((UINT64_C(1) << SCALE_ROOTS) - 1u) << SCALE_FIRST_KEY;
    const uint64_t channel_keys = (UINT64_C(1) << MIDI_CHANNELS) - 1u;
    const uint64_t profile_keys = ((UINT64_C(1) << TG_LAYOUT_PROFILES) - 1u) << PROFILE_FIRST_KEY;
    uint64_t globe_keys = globe_held ? new_keys & (channel_keys | profile_keys | scale_keys | reset_key) : 0;
    if (globe_keys) {
        panel->globe_tap_pending = 0;
        panel->arp_tap_pending = 0;
        transpose_keys |= globe_keys;
        new_keys &= ~globe_keys;
        if (!panel->save_mode) {
            int channel = tg_keyboard_scan(&(tg_keyboard){0}, globe_keys & channel_keys);
            if (channel >= 0) {
                panel->midi_channel = (uint8_t)channel;
                panel->focus_control = panel->arp_focus = 0;
            }
            for (unsigned profile = 0; profile < TG_LAYOUT_PROFILES; profile++) {
                if (!(globe_keys & (UINT64_C(1) << (PROFILE_FIRST_KEY + profile)))) continue;
                select_profile(panel, profile);
                break;
            }
            for (unsigned root = 0; root < SCALE_ROOTS; root++) {
                if (!(globe_keys & (UINT64_C(1) << (SCALE_FIRST_KEY + root)))) continue;
                panel->scale_root = (uint8_t)root;
                panel->focus_control = panel->arp_focus = 0;
                break;
            }
            if ((globe_keys & reset_key) && panel_sound_edited(panel)) {
                sound->patch = *panel_saved_patch(panel);
                sound->sound_epoch += sound->patch.filter_idle_hold;
                panel->focus_control = panel->arp_focus = 0;
                fader_caught = 0;
            }
        }
    }
    if (!globe_held && panel->globe_tap_pending) {
        panel->globe_tap_pending = 0;
        if (!panel->save_mode && !(pressed & navigation)) {
            panel->page = panel->page == PAGE_GLOBAL ? PAGE_TONE : PAGE_GLOBAL;
            panel->record = panel->key_transpose_armed = panel->focus_control = panel->arp_focus = 0;
        }
    }
    if (arp_held && !globe_held) {
        static const uint8_t modes[] = {TG_ARP_UP, TG_ARP_DOWN, TG_ARP_INCLUSIVE,
            TG_ARP_BOUNCE, TG_ARP_RANDOM, TG_ARP_ORDER, TG_ARP_REPEAT};
        const uint64_t octave_key = UINT64_C(1) << OCTAVE_KEY;
        const uint64_t gate_up = UINT64_C(1) << GATE_UP_KEY;
        const uint64_t gate_down = UINT64_C(1) << GATE_DOWN_KEY;
        uint64_t selected = new_keys & arp_command_keys;
        if (selected) {
            for (unsigned mode = 0; mode < sizeof modes; mode++) {
                if (!(selected & (UINT64_C(1) << mode))) continue;
                sound->arp = modes[mode];
                break;
            }
            for (unsigned rate = 0; rate < TG_ARP_RATE_COUNT; rate++) {
                if (!(selected & (UINT64_C(1) << (RATE_FIRST_KEY + rate)))) continue;
                sound->arp_rate = (uint8_t)(rate + 1u);
                break;
            }
            if (selected & octave_key) sound->arp_octaves = (sound->arp_octaves + 1u) % 4u;
            int gate_delta = !!(selected & gate_up) - !!(selected & gate_down);
            if (gate_delta) {
                int gate = sound->arp_gate ? sound->arp_gate : 75;
                sound->arp_gate = (uint8_t)bounded(gate + 5 * gate_delta, 5, 100);
            }
            if (selected & (UINT64_C(1) << LATCH_KEY)) panel->arp_latch ^= 1u;
            int swing_delta = !!(selected & (UINT64_C(1) << SWING_UP_KEY)) -
                              !!(selected & (UINT64_C(1) << SWING_DOWN_KEY));
            sound->arp_swing = (uint8_t)bounded(sound->arp_swing + 2 * swing_delta, 0, 25);
            int tempo_delta = !!(selected & (UINT64_C(1) << TEMPO_UP_KEY)) -
                              !!(selected & (UINT64_C(1) << TEMPO_DOWN_KEY));
            if (tempo_delta)
                sound->arp_tempo = (uint16_t)bounded((int)tg_arp_tempo(sound) + tempo_delta, 30, 300);
            if (selected & (UINT64_C(1) << TAP_KEY)) {
                /* Matrix frames are 1.6 ms. A stale tap starts a fresh pair. */
                uint32_t interval = frames - arp_tap_frame;
                if (arp_tap_seen && interval >= 125 && interval <= 1250)
                    sound->arp_tempo = (uint16_t)((37500u + interval / 2u) / interval);
                arp_tap_frame = frames;
                arp_tap_seen = 1;
            }
            if (selected & (UINT64_C(1) << SYNC_KEY))
                arp_sync(panel, sound->clock.source ^ 1u);
            panel->arp_tap_pending = 0;
            panel->focus_control = 0;
            panel->arp_focus = performance_page(panel->page) && panel->page != PAGE_GLOBAL && panel->page != PAGE_ARP;
            if (panel->arp_focus) control_focus_frame = frames;
            transpose_keys |= selected;
            new_keys &= ~selected;
        }
    }
    if (!arp_held && panel->arp_tap_pending) {
        panel->arp_tap_pending = 0;
        sound->arp = sound->arp < TG_ARP_BOUNCE ? sound->arp + 1u : TG_ARP_OFF;
        panel->arp_focus = 0;
    }
#ifdef ELITE_DRUMS808
    if (panel->drums_repeat && !globe_held && !arp_held) {
        for (unsigned rate = 0; rate < TG_ARP_RATE_COUNT; rate++) {
            uint64_t key = UINT64_C(1) << (RATE_FIRST_KEY + rate);
            if (!(new_keys & key)) continue;
            panel->drum_clear_pending = 0;
            if (drum_roll_edit->rate != rate + 1u) {
                drum_roll_edit->rate = (uint8_t)(rate + 1u);
                conductor_changed(panel);
            }
            transpose_keys |= key;
            new_keys &= ~key;
        }
    }
#endif
    if (panel->repeat_edit && !globe_held && !arp_held) {
        for (unsigned rate = 0; rate < TG_ARP_RATE_COUNT; rate++) {
            uint64_t key = UINT64_C(1) << (RATE_FIRST_KEY + rate);
            if (!(new_keys & key)) continue;
            sound->repeat_rate = (uint8_t)(rate + 1u);
            panel->repeat_gesture = 0;
            transpose_keys |= key;
            new_keys &= ~key;
        }
    }
    if (panel->key_transpose_armed && new_keys) {
        for (unsigned key = 0; key < TG_KEYS; key++) {
            if (!(new_keys & (UINT64_C(1) << key))) continue;
            panel->key_transpose = (int8_t)((int)key - 12);
            break;
        }
        panel->key_transpose_armed = 0;
        panel->focus_control = 0;
        transpose_keys |= new_keys;
    } else if (!panel->record && !panel->save_mode && panel->page != PAGE_ASSIGN &&
#ifdef ELITE_DRUMS808
               panel->conductor_seq_pending < EDITOR_A &&
#endif
               (buttons & (1u << BTN_SEQ))) {
        if (new_keys) {
            for (unsigned key = 0; key < TG_KEYS; key++) {
                if (!(new_keys & (UINT64_C(1) << key))) continue;
                if (!reset_transpose) sound->transpose = (int8_t)(untransposed_key_note(panel, key) - 48);
#ifdef ELITE_DRUMS808
                panel->conductor_seq_pending = 0;
                /* The phrase command targets Telegram. Dillo keeps its lead. */
                if (!panel->conductor_dillo_keys)
#endif
                    panel->hold = 0;
                break; /* Lowest newly pressed key resolves simultaneous presses. */
            }
            transpose_keys |= new_keys;
        }
    }
#ifdef ELITE_DILLO
    if (panel->page != PAGE_DILLO) {
#ifdef ELITE_DRUMS808
        /* Sound and Mixer opened from Steps return to the same working phrase. */
        unsigned return_page = panel->page == PAGE_MIX ? panel->fx_return : panel->page;
        unsigned detour = return_page == PAGE_DILLO ||
                          (return_page == PAGE_DILLO_SOUND && !panel->dillo_sound_home);
        if (!detour || panel->dillo_record < DILLO_REC_PITCH)
#endif
        {
            panel->dillo_record = DILLO_REC_OFF;
            panel->dillo_time_edit = 0;
        }
    }
    unsigned dillo_entering = panel->dillo_record == DILLO_REC_DIRECT || panel->dillo_record == DILLO_REC_PITCH;
#ifdef ELITE_DRUMS808
    if (!dillo_entering) panel->dillo_pitch_paused = 0;
    dillo_entering = dillo_entering && !panel->dillo_pitch_paused;
#endif
    dillo_entering = dillo_entering && panel->page == PAGE_DILLO;
    if (dillo_entering) {
        uint64_t entry = new_keys & ~transpose_keys;
        for (unsigned key = 0; key < TG_KEYS; key++) {
            if (!(entry & (UINT64_C(1) << key))) continue;
            unsigned pitch = key_note(panel, key);
            if (panel->dillo_record == DILLO_REC_DIRECT) dillo_pitch(panel, pitch, UNDO_STEP);
            else if (panel->dillo_pitch_replace) {
                panel->dillo_pitches[panel->dillo_pitch_replace - 1u] = (uint8_t)pitch;
                panel->dillo_pitch_replace = 0;
            } else if (panel->dillo_pitch_count < DILLO_STEPS)
                panel->dillo_pitches[panel->dillo_pitch_count++] = (uint8_t)pitch;
            else break;
#ifdef ELITE_DRUMS808
            /* Each accepted pitch gets a fresh attack, even with overlapping keys. */
            panel->dillo_entry_key = (uint8_t)(key + 1u);
            panel->dillo_entry_pitch = (uint8_t)pitch;
            panel->dillo_entry_velocity = velocities[key] ? velocities[key] : 64;
            if (panel->dillo_record == DILLO_REC_DIRECT)
                panel->dillo_entry_velocity = panel->dillo_pattern.steps[panel->dillo_cursor].flags & DILLO_ACCENT ? 127 : 64;
            panel->dillo_live.gap++;
#endif
            break;
        }
        transpose_keys |= entry;
    }
#ifdef ELITE_DRUMS808
    if (panel->dillo_entry_key && (!dillo_entering || !(down & (UINT64_C(1) << (panel->dillo_entry_key - 1u)))))
        panel->dillo_entry_key = 0;
#endif
#endif
    down &= ~transpose_keys;
#ifdef ELITE_DRUMS808
    const uint64_t continuing_keys = down & keyboard.down;
#endif
    int last = tg_keyboard_scan(&keyboard, down);
#ifdef ELITE_DRUMS808
    unsigned dillo_keys_active = panel->conductor_dillo_keys && !panel->record && panel->page != PAGE_ASSIGN;
    unsigned key_focus_changed = prior_key_focus != panel->conductor_dillo_keys;
    if (key_focus_changed) {
        /* Drop released latches and keep the order of keys held through the switch.
         * The scan below adds any newly pressed keys at the end. */
        held_chord = (tg_latch){0};
        arp_chord = (tg_latch){0};
        held_keyboard = keyboard;
        update_key_order(continuing_keys);
    }
#else
    const unsigned dillo_keys_active = 0;
    const unsigned key_focus_changed = 0;
#endif
    unsigned arp_latch = !dillo_keys_active && panel->arp_latch && sound->arp &&
                         arp_latch_allowed(sound) && !panel->record;
    unsigned pedal_travel = panel->pedal_mode == PEDAL_LOW ? 1023u - pedal : pedal;
    panel->pedal_down = panel->pedal_mode != PEDAL_OFF &&
        pedal_travel >= (panel->pedal_down ? 880u : 928u);
    unsigned hold_keys = panel->hold || (panel->pedal_down && !panel->record);
    const tg_latch *active_hold = arp_latch ? &arp_chord : hold_keys ? &held_chord : NULL;
    if (active_hold && down && !active_hold->down && !key_focus_changed) {
        /* A replacement chord owns its own press order and strike, even if
         * it uses the same pitches as the previous held gesture. */
        held_keyboard.down = 0;
        key_order_mask = 0;
        key_order_count = 0;
    }
    uint64_t held = tg_latch_scan(&held_chord, down, hold_keys);
    uint64_t latched = tg_latch_scan(&arp_chord, down, arp_latch);
    uint64_t performance_keys = arp_latch ? latched : held;
    tg_keyboard_scan(&held_keyboard, performance_keys);
    if (panel->pedal_down && !panel->record && last >= 0)
        held_keyboard.selected = (uint8_t)last;
    update_key_order(performance_keys);
    int live = tg_keyboard_note(hold_keys || arp_latch ? &held_keyboard : &keyboard, sound->patch.trigger);
#ifdef ELITE_DRUMS808
    /* Return to the most recently played held key after a legato release. */
    if (dillo_keys_active) live = key_order_count ? key_order[key_order_count - 1u] : -1;
#endif
    sound->live_note = live < 0 ? 0 : (uint8_t)key_note(panel, (unsigned)live);
    sound->live_velocity = live < 0 ? 0 : velocities[live] ? velocities[live] : 64;
#ifdef ELITE_DRUMS808
    unsigned dillo_note = dillo_keys_active ? sound->live_note : 0;
    unsigned dillo_velocity = dillo_note ? sound->live_velocity : 0;
    if (panel->dillo_entry_key) {
        dillo_note = panel->dillo_entry_pitch;
        dillo_velocity = panel->dillo_entry_velocity;
    }
    if (panel->dillo_audition) {
        unsigned step = panel->dillo_audition - 1u;
        dillo_step note = {0};
        if (time_pick && step < panel->dillo_time_count && !conductor_clock_lost(panel)) {
            note = panel->dillo_time[step];
            if (note.event == DILLO_NOTE) note.pitch = panel->dillo_pitches[dillo_time_rank(panel, step)];
        } else if (!panel->dillo_record && panel->dillo_cursor == step && step < panel->dillo_pattern.length) {
            note = panel->dillo_pattern.steps[step];
        }
        uint32_t config = fm1_icfg();
        fm1_irq_off();
        unsigned held = smk_pad_own & (1u << step);
        fm1_icfg_set(config);
        if (!held || panel->page != PAGE_DILLO || conductor_running(panel) ||
            note.event != DILLO_NOTE || dillo_note) {
            panel->dillo_audition = 0;
        } else {
            dillo_note = note.pitch ? note.pitch : TG_NOTE_ZERO;
            dillo_velocity = note.flags & DILLO_ACCENT ? 127 : 64;
            if (dillo_audition_strike) panel->dillo_live.strike++;
        }
    }
    /* Return to a held lead with its own attack after previewing a pitch. */
    if ((!dillo_note && panel->dillo_live.note) || (prior_dillo_entry && !panel->dillo_entry_key) ||
        (prior_dillo_audition && (!panel->dillo_audition || dillo_audition_strike)))
        panel->dillo_live.gap++;
    panel->dillo_live.note = (uint8_t)dillo_note;
    panel->dillo_live.velocity = (uint8_t)dillo_velocity;
    unsigned dillo_expression = dillo_keys_active && !panel->dillo_entry_key && !panel->dillo_audition;
    panel->dillo_live.bend = dillo_expression ? sound->bend : 0;
    panel->dillo_live.mod = dillo_expression ? sound->mod : 0;
    if (dillo_keys_active) {
        if (dillo_expression && live >= 0 && (new_keys & (UINT64_C(1) << live))) panel->dillo_live.strike++;
        sound->live_note = sound->live_velocity = 0;
        sound->bend = sound->mod = 0;
        live = -1;
    }
#endif
    if (last >= 0 && last == live)
        sound->live_trigger++;
    if (last >= 0 && panel->page == PAGE_ASSIGN && panel->assign_source == MAP_PAD)
        panel->pad_note[panel->assign_slot] = (uint8_t)bounded((int)key_note(panel, (unsigned)last), 12, 108);
    sound->repeat_pad = 0;
    if (!performance_page(panel->page)) panel->pads_down = panel->repeat_latched = 0;
    if (live < 0 && pad_performance && panel->repeat_on && (panel->pads_down || panel->repeat_latched)) {
        unsigned selected = panel->repeat_latched ? panel->repeat_latched - 1u : panel->pad_lead;
        if (!panel->repeat_latched && !(panel->pads_down & (1u << selected))) {
            for (unsigned pad = 0; pad < TG_PADS; pad++) {
                if (!(panel->pads_down & (1u << pad))) continue;
                selected = pad;
                break;
            }
        }
        sound->repeat_pad = 1;
        sound->live_note = panel->pad_note[selected];
        sound->live_velocity = midi_pad_velocity[selected];
        if (new_pad == (int)selected) sound->live_trigger++;
    } else if (live < 0 && panel->pads_down) {
        unsigned selected = panel->pad_lead;
        if (sound->patch.trigger != TG_GATE_TRIG || !(panel->pads_down & (1u << selected))) {
            unsigned low = 127;
            for (unsigned pad = 0; pad < TG_PADS; pad++) {
                if (!(panel->pads_down & (1u << pad)) || panel->pad_note[pad] >= low) continue;
                selected = pad;
                low = panel->pad_note[pad];
            }
        }
        sound->live_note = panel->pad_note[selected];
        sound->live_velocity = midi_pad_velocity[selected];
        if (new_pad >= 0 && sound->live_note == panel->pad_note[(unsigned)new_pad])
            sound->live_trigger++;
    }
    unsigned arp_count = 0;
    if (sound->arp && !dillo_keys_active) {
        uint32_t seen[4] = {0};
        unsigned ordered = sound->arp == TG_ARP_ORDER;
        unsigned slots = ordered ? key_order_count : TG_KEYS;
        for (unsigned i = 0; i < slots; i++) {
            unsigned key = ordered ? key_order[i] : i;
            if (!(performance_keys & (UINT64_C(1) << key))) continue;
            unsigned note = key_note(panel, key);
            uint32_t bit = UINT32_C(1) << (note % 32u);
            if (panel->scale_type && (seen[note / 32u] & bit)) continue;
            seen[note / 32u] |= bit;
            sound->arp_notes[arp_count] = (uint8_t)note;
            sound->arp_velocity[arp_count++] = velocities[key] ? velocities[key] : 64;
        }
    }
    sound->arp_count = (uint8_t)arp_count;
#ifdef ELITE_DRUMS808
    if ((was_running && !sound->running) || old_clock_source != sound->clock.source)
        clear_drum_rolls(panel, panel->drum_latch);
    panel->drums_b.repeat = (momentary == DRUM_LATCH_B) ||
        ((panel->drum_latch & DRUM_LATCH_B) && drum_roll_active(&panel->drums_b.roll));
    panel->dillo_articulation = panel->page == PAGE_DILLO_SOUND && panel->dillo_enabled &&
        !(buttons & ((1u << BTN_SEQ) | (1u << BTN_REC))) ?
        (repeat_held ? DILLO_ACCENT : 0) | ((buttons & (1u << BTN_TIE)) ? DILLO_SLIDE : 0) : 0;
    if (panel->conductor_rec_pending) {
        if (!conductor_save_page(panel->page) || (pressed & navigation))
            panel->conductor_rec_pending = 0;
        else if (!(buttons & (1u << BTN_REC))) {
            panel->conductor_rec_pending = 0;
            conductor_save(panel);
        }
    }
#endif
    if (layout_requested && panel->page == PAGE_ASSIGN) save_layout(panel);
    if (confirm_requested && !library_pad_pressed && panel->page == PAGE_PATCH && panel->save_mode == SAVE_CONFIRM)
        library_confirm(panel);
    unsigned local_record_key = panel->record_key && panel->record_key <= TG_KEYS;
    if (!panel->record || (local_record_key && !(down & (UINT64_C(1) << (panel->record_key - 1u))))) {
        panel->record_key = 0;
        local_record_key = 0;
    }
    unsigned chord_held = buttons & (1u << BTN_TIE);
    unsigned explicit_tie = chord_held && (panel->record != REC_NEW || panel->cursor);
    if (panel->record && !cleared_step) {
        if ((pressed & (1u << BTN_REST)) && !chord_held)
            enter_step(panel, TG_REST, 0);
        else if (last >= 0) {
            enter_step(panel, key_note(panel, (unsigned)last), local_record_key || explicit_tie ? TG_TIE : 0);
            if (panel->record) panel->record_key = (uint8_t)(last + 1);
        }
    }
    panel->midi_step_entry = panel->page == PAGE_STEPS && panel->record && !cleared_step ? MIDI_STEP_ENTRY |
        (explicit_tie ? MIDI_STEP_TIE : 0u) : 0u;
}
