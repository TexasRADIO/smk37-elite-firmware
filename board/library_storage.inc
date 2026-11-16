/* SPDX-License-Identifier: GPL-3.0-only */
/* Foreground-only board adapter. Hardware pause/resume is supplied by main.c
 * and modelled independently in the host integration test. */
_Static_assert(ELITE_LIBRARY_HI - ELITE_LIBRARY_LO == TG_STORE_BYTES, "library region size");
_Static_assert(FL_WIN_SAFE(ELITE_LIBRARY_LO, ELITE_LIBRARY_HI) &&
               ELITE_LIBRARY_LO >= FL_DATA_LO && ELITE_LIBRARY_HI <= FL_DATA_HI,
               "library must remain inside data VM and outside protected/OTA ranges");
static uint8_t library_scratch[TG_STORE_BANK];
static tg_library library_verified;
static uint8_t library_writable, library_write_open;
static int library_last_result;
#ifdef ELITE_DRUMS808
#ifdef ELITE_BUILD
__attribute__((section(".pool"), aligned(4)))
#endif
static uint8_t conductor_verified[CONDUCTOR_MAX_BYTES];
static unsigned conductor_verified_size = CONDUCTOR_WIRE_BYTES;
static unsigned conductor_storage_ok;
static uint32_t conductor_generation, conductor_library_crc, conductor_tail_hash;
static uint8_t conductor_committed_verified, conductor_committed_present;

static uint32_t recall_hash_more(uint32_t value, const void *bytes, unsigned count)
{
    const uint8_t *in = bytes;
    while (count--) value = (value ^ *in++) * UINT32_C(16777619);
    return value;
}

static uint32_t recall_hash(const void *bytes, unsigned count)
{
    return recall_hash_more(UINT32_C(2166136261), bytes, count);
}

/* Cache only a validated current-format bank read from NOR, never a draft. */
static void library_remember_conductor(const uint8_t *tail, unsigned size)
{
    tg_store_loaded_identity(library_scratch, &conductor_generation, &conductor_library_crc);
    conductor_tail_hash = recall_hash(tail, size);
    conductor_committed_verified = 1;
}

static void library_discard_failed_conductor_save(struct panel *panel)
{
    if (!(panel->conductor_save_state & CONDUCTOR_SAVE_PENDING)) return;
    /* A later sound save must use the last durable arrangement. The panel
     * and working banks still contain the edits for an explicit retry. */
    memcpy(conductor_snapshot, conductor_verified, sizeof conductor_snapshot);
    conductor_snapshot_size = conductor_verified_size;
    conductor_snapshot_present = conductor_committed_present;
    panel->conductor_save_state = CONDUCTOR_SAVE_FAILED;
    panel->conductor_dirty = 1;
}
#endif

static int library_bounds(uint32_t off, uint32_t n)
{
    return off <= TG_STORE_BYTES && n <= TG_STORE_BYTES - off;
}
static int library_read(void *context, uint32_t off, void *out, uint32_t n)
{
    (void)context;
    if (!library_bounds(off, n)) return -1;
    memcpy(out, (const void *)FL_XIP(ELITE_LIBRARY_LO + off), n);
    fm1_wdt_feed();
    return 0;
}
static int library_erase(void *context, uint32_t off)
{
    (void)context;
    if (!library_write_open || !library_bounds(off, TG_STORE_SECTOR) || off % TG_STORE_SECTOR) return -1;
    uint32_t elapsed;
    fm1_wdt_feed();
    int rc = fl_erase4k(ELITE_LIBRARY_LO + off, &elapsed);
    fm1_wdt_feed();
    return rc;
}
static int library_program(void *context, uint32_t off, const void *data, uint32_t n)
{
    (void)context;
    if (!library_write_open || !n || !library_bounds(off, n) || n > 256 || (off & 255u) + n > 256 ||
        off % TG_STORE_SECTOR >= TG_STORE_SECTOR - 256) return -1;
    fm1_wdt_feed();
    return fl_write(ELITE_LIBRARY_LO + off, data, n);
}
static const tg_store_io library_io = {0, library_read, library_erase, library_program};

static void library_setup(struct panel *panel)
{
    library_write_open = 0;
    tg_library_init(&user_library);
    int rc = tg_store_load(&library_io, &user_library, library_scratch);
    profile_draft_slots = 0;
    panel->profile = user_library.profile;
    apply_layout(panel, &user_library.layouts[panel->profile]);
    fader_caught = 0;
    library_last_result = rc;
    library_writable = rc == TG_STORE_OK || rc == TG_STORE_EMPTY;
#ifdef ELITE_DRUMS808
    conductor_snapshot_present = 0;
    conductor_committed_verified = 0;
    conductor_generation = conductor_library_crc = conductor_tail_hash = 0;
    panel->mixer = (conductor_mixer)CONDUCTOR_MIX_DEFAULT;
    drum_kit_default(&panel->drums_kit);
    drum_kit_default(&panel->drums_b.kit);
    memcpy(panel->drums_b.mix, default_b_setup + 4, MIX_CONTROLS);
    panel->dillo_sound = (dillo_sound){.tone = bb_dillo_bank[DILLO_DEFAULT_SOUND].sound};
    panel->dillo_sound_slot = DILLO_DEFAULT_SOUND;
    static const unsigned tail_sizes[] = {CONDUCTOR_SWITCH_WIRE_BYTES, CONDUCTOR_WIRE_BYTES,
        CONDUCTOR_PERFORMANCE_TAIL_BYTES, CONDUCTOR_STARTUP_SOUND_BYTES,
        CONDUCTOR_DUAL_DRUM_BYTES, CONDUCTOR_SOUNDS_BYTES, CONDUCTOR_KITS_BYTES,
        CONDUCTOR_SCENES_BYTES, CONDUCTOR_MIX_BYTES, CONDUCTOR_LEGACY_BYTES};
    unsigned tail_size = CONDUCTOR_WIRE_BYTES;
    int tail = rc;
    if (rc == TG_STORE_OK) {
        memset(conductor_snapshot, 0, sizeof conductor_snapshot); /* a short tail leaves the switch bytes zero */
        tail = tg_store_loaded_tail(library_scratch, conductor_snapshot, sizeof conductor_snapshot, &tail_size);
        if (tail == TG_STORE_OK) {
            unsigned known = 0;
            for (unsigned i = 0; i < sizeof tail_sizes / sizeof tail_sizes[0]; i++)
                known |= tail_sizes[i] == tail_size;
            if (!known || !conductor_restore(panel, conductor_snapshot, tail_size)) tail = TG_STORE_INVALID;
        }
    }
    if (tail == TG_STORE_OK && tail_size >= CONDUCTOR_WIRE_BYTES) {
        conductor_snapshot_size = tail_size;
        library_remember_conductor(conductor_snapshot, tail_size);
    } else if (tail == TG_STORE_OK || tail == TG_STORE_EMPTY) {
        if (tail == TG_STORE_EMPTY) {
            /* Start stopped with the factory groove ready on slot one. */
#ifdef ELITE_SINGLE_808
            panel->dillo_sound.tone.drive = CONDUCTOR_DILLO_START_DRIVE;
            panel->sound.patch.drive = CONDUCTOR_TELEGRAM_START_DRIVE;
#endif
            panel->drums_pattern = drum_demo;
            panel->drums_b.pattern = drum_demo_b;
            panel->dillo_pattern = dillo_demo;
            panel->drums_initialized = panel->drums_b_initialized = 1;
            panel->drums_enabled = panel->drums_b.enabled = panel->dillo_enabled = 1;
            panel->conductor_lane = PAGE_DILLO;
        }
        /* Seed the startup fields before a library-only save is possible. */
        memset(conductor_snapshot, 0, sizeof conductor_snapshot);
        conductor_snapshot_size = conductor_encode(panel, conductor_snapshot, 1);
    }
    conductor_snapshot_present = tail == TG_STORE_OK;
    conductor_storage_ok = tail == TG_STORE_OK || tail == TG_STORE_EMPTY;
    conductor_committed_present = conductor_snapshot_present;
    if (conductor_storage_ok) {
        memcpy(conductor_verified, conductor_snapshot, sizeof conductor_verified);
        conductor_verified_size = conductor_snapshot_size;
    }
#ifdef ELITE_PERCUSSION_727
    if (panel->layer_available) {
        conductor_migrate_layer(panel);
        panel->conductor_startup_dirty = CONDUCTOR_STARTUP_CHECK;
    }
#endif
    library_writable &= conductor_storage_ok;
#endif
    library_writable = library_writable && !tg_store_can_save(&library_io, library_scratch) && flash_chip_ok();
    if (!library_writable) panel->library_status = LIB_UNAVAILABLE;
}

#ifdef ELITE_DRUMS808
static unsigned library_startup_differs(const struct panel *panel)
{
    unsigned size = conductor_encode(panel, library_scratch, 1);
    return size != conductor_verified_size || memcmp(library_scratch, conductor_verified, size) != 0 ||
           conductor_unstored_pattern(panel, 0) != 0;
}
#endif

static void library_service(struct panel *panel)
{
#ifdef ELITE_DRUMS808
    /* Full setup recall changes the working arrangement without staging a write. */
    if (panel->conductor_startup_dirty == CONDUCTOR_STARTUP_CHECK)
        panel->conductor_startup_dirty = !conductor_storage_ok || library_startup_differs(panel);
#endif
    if (!panel->store_request) return;
    if (!library_writable || library_hardware_fault()) {
#ifdef ELITE_DRUMS808
        library_discard_failed_conductor_save(panel);
#endif
        panel->store_request = 0;
        panel->library_status = LIB_UNAVAILABLE;
        return;
    }
    if (library_transport_busy(&panel->sound) || panel->sound.live_note || panel->pads_down || panel->sound.arp_count
#ifdef ELITE_DRUMS808
        || panel->dillo_live.note || drum_roll_active(&panel->drums_roll) || drum_roll_active(&panel->drums_b.roll)
#endif
    ) {
        panel->library_status = LIB_WAIT_STOP;
        return;
    }
    /* Re-check raw keys, transport, updater and quiet output while disabling
     * audio. A key arriving after the panel snapshot must postpone the save. */
    if (!library_pause_audio()) {
        panel->library_status = LIB_WAIT_QUIET;
        return;
    }
    int rc = tg_store_can_save(&library_io, library_scratch);
    if (!rc && !flash_chip_ok()) rc = TG_STORE_INVALID;
    if (!rc) {
        library_write_open = 1;
#ifdef ELITE_DRUMS808
        rc = tg_store_save_tail(&library_io, &user_library, conductor_snapshot,
            conductor_snapshot_present ? conductor_snapshot_size : 0, library_scratch);
#else
        rc = tg_store_save(&library_io, &user_library, library_scratch);
#endif
        library_write_open = 0;
    }
    /* A failed commit acknowledgement is ambiguous. Only an independent
     * readback equal to the requested library can clear the dirty flag. */
    int readback = tg_store_load(&library_io, &library_verified, library_scratch);
    unsigned saved = !readback && !memcmp(&library_verified, &user_library, sizeof user_library);
#ifdef ELITE_DRUMS808
    if (saved) {
        unsigned verified_size = 0;
        memset(conductor_verified, 0, sizeof conductor_verified);
        int tail = tg_store_loaded_tail(library_scratch, conductor_verified, sizeof conductor_verified, &verified_size);
        if (conductor_snapshot_present) {
            saved = !tail && verified_size == conductor_snapshot_size &&
                    !memcmp(conductor_snapshot, conductor_verified, verified_size);
            if (saved) {
                conductor_verified_size = verified_size;
                library_remember_conductor(conductor_verified, verified_size);
            }
        } else saved = tail == TG_STORE_EMPTY;
    }
    unsigned conductor_saved = CONDUCTOR_SAVE_NONE;
    if (saved) {
        conductor_committed_present = conductor_snapshot_present;
        if (panel->conductor_save_state & CONDUCTOR_SAVE_PENDING) {
            conductor_saved = panel->conductor_save_state;
            panel->conductor_dirty = panel->conductor_edits != panel->conductor_save_edits;
            panel->conductor_save_state = CONDUCTOR_SAVE_NONE;
        }
    } else library_discard_failed_conductor_save(panel);
#endif
    library_last_result = saved ? TG_STORE_OK : rc ? rc : readback ? readback : TG_STORE_INVALID;
    if (saved) panel->library_dirty = 0;
    panel->library_status = saved ? LIB_SAVED : LIB_FAILED;
    panel->store_request = 0;
    library_writable = !tg_store_can_save(&library_io, library_scratch);
#ifdef ELITE_DRUMS808
    library_writable &= conductor_storage_ok;
#endif
    audio_restart();
#ifdef ELITE_DRUMS808
    /* Compare the working arrangement after resuming audio. A library
     * save can verify the slots while startup still holds older settings. */
    if (conductor_saved && !panel->conductor_dirty) {
        panel->conductor_startup_dirty = library_startup_differs(panel);
        if (conductor_saved & CONDUCTOR_SAVE_STARTUP)
            panel->conductor_dirty = panel->conductor_startup_dirty;
    }
#endif
}


#ifdef ELITE_DRUMS808
static void library_recall_status(elite_usb_recall *status, const struct panel *panel)
{
    uint32_t config = fm1_icfg();
    fm1_irq_off();
    unsigned held = fm1_in.keys || smk_pad_own;
    fm1_icfg_set(config);
    unsigned busy = held || library_transport_busy(&panel->sound) ||
                    panel->sound.live_note || panel->dillo_live.note ||
                    panel->sound.arp_count || panel->store_request ||
                    drum_roll_active(&panel->drums_roll) || drum_roll_active(&panel->drums_b.roll);
    status->journal_generation = conductor_generation;
    status->library_crc = conductor_library_crc;
    status->tail_hash = conductor_tail_hash;
    status->scene = panel->scene_sound;
    status->state = (conductor_committed_verified ? ELITE_RECALL_VERIFIED : 0) |
        (busy ? ELITE_RECALL_BUSY : 0) |
        (panel->library_dirty || panel->conductor_dirty ? ELITE_RECALL_DIRTY : 0) |
        (panel->conductor_startup_dirty ? ELITE_RECALL_STARTUP_DIRTY : 0) |
        (!library_writable || library_last_result || library_hardware_fault() ? ELITE_RECALL_UNAVAILABLE : 0) |
        (panel->scene_status == SCENE_RECALLED && panel->scene_sound ? ELITE_RECALL_SCENE : 0) |
        (panel->drums_enabled &&
         (ELITE_DRUM_PARTS == 1 || (panel->drums_b.enabled && !panel->drums_b.muted)) &&
         panel->dillo_enabled && !panel->sound.phrase_muted ?
         ELITE_RECALL_ARRANGEMENT : 0);
    if (status->state & (ELITE_RECALL_BUSY | ELITE_RECALL_UNAVAILABLE)) return;

    uint8_t setup[CONDUCTOR_SETUP_BYTES + 2], b[CONDUCTOR_B_SETUP_BYTES];
    conductor_capture_setup(panel, setup);
    setup[CONDUCTOR_SETUP_BYTES] = panel->kit_slot;
    setup[CONDUCTOR_SETUP_BYTES + 1u] = panel->dillo_sound_slot;
    conductor_capture_b_setup(panel, b);
    uint8_t performance[CONDUCTOR_PERFORMANCE_BYTES];
    conductor_capture_performance(panel, performance);
    uint32_t routing = recall_hash_more(recall_hash(setup, sizeof setup), b, 4);
    status->routing_hash = recall_hash_more(routing, performance, sizeof performance);
    conductor_mixer mix = conductor_mixer_controls(panel);
    status->mixer_hash = recall_hash_more(recall_hash(mix.value, CONDUCTOR_MIX_SIZE), panel->drums_b.mix, MIX_CONTROLS);

    uint8_t pattern[CONDUCTOR_DRUM_BYTES];
    conductor_pack_drums(pattern, &panel->drums_pattern);
    status->drums_a_hash = recall_hash(pattern, sizeof pattern);
    conductor_pack_drums(pattern, &panel->drums_b.pattern);
    status->drums_b_hash = recall_hash(pattern, sizeof pattern);
    uint8_t dillo[CONDUCTOR_DILLO_BYTES];
    conductor_pack_dillo(dillo, &panel->dillo_pattern);
    status->dillo_hash = recall_hash(dillo, sizeof dillo);
    uint8_t phrase[2] = {(uint8_t)tg_pattern_length(&panel->sound), (uint8_t)tg_phrase_gate(&panel->sound)};
    status->telegram_phrase_hash = recall_hash_more(recall_hash(phrase, sizeof phrase), panel->sound.steps, TG_STEPS);

    uint8_t sound[CONDUCTOR_SOUND_BYTES + CONDUCTOR_DECAY_BYTES], telegram[TG_PATCH_WIRE_BYTES];
    conductor_pack_dillo_sound(sound, &panel->dillo_sound.tone);
    conductor_pack_dillo_decay(sound + CONDUCTOR_SOUND_BYTES, panel->dillo_sound.decay);
    uint32_t voices = recall_hash_more(recall_hash(sound, sizeof sound), panel->drums_kit.value, CONDUCTOR_KIT_BYTES);
    status->voices_hash = recall_hash_more(voices, panel->drums_b.kit.value, CONDUCTOR_KIT_BYTES);
    tg_patch_encode(telegram, &panel->sound.patch);
    status->telegram_sound_hash = recall_hash(telegram, sizeof telegram);
}
#endif
