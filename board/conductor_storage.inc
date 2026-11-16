/* SPDX-License-Identifier: GPL-3.0-only */
/* Explicit wire fields, shared journal commit and unchanged flash range. */
enum {
    CONDUCTOR_A_ENABLED = 1u, CONDUCTOR_DILLO_ENABLED = 1u << 1,
    CONDUCTOR_TELEGRAM_MUTED = 1u << 5,
    CONDUCTOR_FLAGS = 3, CONDUCTOR_A_SLOT = 4, CONDUCTOR_DILLO_SLOT = 5, CONDUCTOR_TELEGRAM_SLOT = 6,
    CONDUCTOR_SETUP_BYTES = 7, CONDUCTOR_MAP_BYTES = 6,
    CONDUCTOR_SCENES = 8, CONDUCTOR_MIX_SIZE = MIX_LANES * MIX_CONTROLS,
    CONDUCTOR_DILLO_OLD = 1 + 4 * DILLO_STEPS,
    CONDUCTOR_DILLO_BYTES = 1 + 2 * DILLO_STEPS,
    CONDUCTOR_DRUM_OLD = 4 * D8_NUM,
    CONDUCTOR_DRUM_BYTES = (D8_NUM * DRUM_STEPS + 4) / 5,
    CONDUCTOR_TELEGRAM_BYTES = 2 + TG_STEPS,
    CONDUCTOR_LEGACY_BYTES = CONDUCTOR_SETUP_BYTES + CONDUCTOR_MAP_BYTES +
        CONDUCTOR_SLOTS * (CONDUCTOR_DILLO_OLD + CONDUCTOR_DRUM_OLD + CONDUCTOR_TELEGRAM_BYTES),
    CONDUCTOR_MIX_BYTES = CONDUCTOR_LEGACY_BYTES + CONDUCTOR_MIX_SIZE,
    CONDUCTOR_SCENE_BYTES = CONDUCTOR_SETUP_BYTES + CONDUCTOR_MIX_SIZE + TG_PATCH_WIRE_BYTES,
    CONDUCTOR_SCENES_BYTES = CONDUCTOR_MIX_BYTES - CONDUCTOR_SLOTS * (CONDUCTOR_DILLO_OLD - CONDUCTOR_DILLO_BYTES) +
        1 + CONDUCTOR_SCENES * CONDUCTOR_SCENE_BYTES,
    CONDUCTOR_KIT_BYTES = D8_NUM * DRUM_KIT_PARAMS,
    CONDUCTOR_KITS_BYTES = CONDUCTOR_SCENES_BYTES -
        CONDUCTOR_SLOTS * (CONDUCTOR_DRUM_OLD - CONDUCTOR_DRUM_BYTES) +
        2 + (DRUM_USER_KITS + 1) * CONDUCTOR_KIT_BYTES + CONDUCTOR_SCENES,
    CONDUCTOR_SCENE_COMPACT = CONDUCTOR_SETUP_BYTES + CONDUCTOR_MIX_SIZE + TG_PATCH_SCENE_BYTES,
    CONDUCTOR_SOUND_BYTES = 12,
    CONDUCTOR_SOUNDS_BYTES = CONDUCTOR_KITS_BYTES -
        CONDUCTOR_SCENES * (CONDUCTOR_SCENE_BYTES - CONDUCTOR_SCENE_COMPACT) +
        2 + (DILLO_USER_SOUNDS + 1) * CONDUCTOR_SOUND_BYTES + CONDUCTOR_SCENES,
    CONDUCTOR_START_DILLO_SOUND = CONDUCTOR_SOUNDS_BYTES - CONDUCTOR_SCENES -
        (DILLO_USER_SOUNDS + 1) * CONDUCTOR_SOUND_BYTES - 1,
    CONDUCTOR_START_KIT = CONDUCTOR_START_DILLO_SOUND - CONDUCTOR_SCENES -
        (DRUM_USER_KITS + 1) * CONDUCTOR_KIT_BYTES - 2,
    CONDUCTOR_B_SLOT = 1, CONDUCTOR_B_KIT_SLOT = 2, CONDUCTOR_B_MIX = 4,
    CONDUCTOR_B_SETUP_BYTES = CONDUCTOR_B_MIX + MIX_CONTROLS,
    CONDUCTOR_DUAL_DRUM_BYTES = CONDUCTOR_SOUNDS_BYTES + 2 + CONDUCTOR_B_SETUP_BYTES +
        CONDUCTOR_KIT_BYTES + (CONDUCTOR_SLOTS + 1) * CONDUCTOR_DRUM_BYTES +
        CONDUCTOR_SCENES * CONDUCTOR_B_SETUP_BYTES,
    CONDUCTOR_STARTUP_SOUND_BYTES = CONDUCTOR_DUAL_DRUM_BYTES + TG_NAME_BYTES + TG_PATCH_WIRE_BYTES,
    CONDUCTOR_PERFORMANCE_BYTES = 5,
    CONDUCTOR_PERFORMANCE_TAIL_BYTES = CONDUCTOR_STARTUP_SOUND_BYTES + (1 + CONDUCTOR_SCENES) * CONDUCTOR_PERFORMANCE_BYTES,
    CONDUCTOR_DECAY_BYTES = 2,
    CONDUCTOR_WIRE_BYTES = CONDUCTOR_PERFORMANCE_TAIL_BYTES + (1 + DILLO_USER_SOUNDS) * CONDUCTOR_DECAY_BYTES,
    /* Kit sound switches, five bits each for working A then each user kit, in a 20-bit
     * little-endian field. Written only when a conga, claves or maracas is chosen, so
     * other saves keep the Build 771 format. The journal has room for exactly these. */
    CONDUCTOR_SWITCH_KITS = 1 + DRUM_USER_KITS,
    CONDUCTOR_SWITCH_BYTES = (CONDUCTOR_SWITCH_KITS * DRUM_ALTERNATES + 7) / 8,
    CONDUCTOR_SWITCH_WIRE_BYTES = CONDUCTOR_WIRE_BYTES + CONDUCTOR_SWITCH_BYTES,
    CONDUCTOR_MAX_BYTES = CONDUCTOR_SWITCH_WIRE_BYTES
};
_Static_assert(CONDUCTOR_MAX_BYTES <= (unsigned)TG_STORE_TAIL_MAX, "Conductor save exceeds available journal space");
_Static_assert(TG_PATCH_WIRE_BYTES == TG_PARAMETERS + 17 + TG_ADSR_STAGES, "patch wire size");
enum { SCENE_NORMAL, SCENE_CAPTURED, SCENE_RECALLED, SCENE_EMPTY, SCENE_CUED,
       SCENE_SAVE_KIT, SCENE_SAVE_DILLO, SCENE_SAVE_B_KIT, SCENE_VARIATION,
       SCENE_OCCUPIED, SCENE_PATTERN_FULL, SCENE_KITS_FULL, SCENE_SOUNDS_FULL,
       SCENE_REMOVED, SCENE_REMOVE_STOP, SCENE_REMOVE_BUSY, SCENE_REMOVE_IN_USE, SCENE_REMOVE_CHANGED,
       SCENE_MIX_RECALLED };
enum {
    CONDUCTOR_SAVE_NONE = 0, CONDUCTOR_SAVE_PENDING = 1,
    CONDUCTOR_SAVE_FAILED = 2, CONDUCTOR_SAVE_STARTUP = 4
};
enum { CONDUCTOR_STARTUP_CHECK = 2 };

typedef struct {
    uint8_t setup[CONDUCTOR_SETUP_BYTES];
    conductor_mixer mixer;
    tg_patch patch;
    uint8_t kit_slot, dillo_sound_slot, drums_b[CONDUCTOR_B_SETUP_BYTES];
    uint8_t performance[CONDUCTOR_PERFORMANCE_BYTES];
} conductor_scene;
static conductor_scene conductor_scenes[CONDUCTOR_SCENES] CONDUCTOR_POOL;
static tg_saved_patch conductor_startup_sound CONDUCTOR_POOL;
static const char *conductor_startup_label;
static drum_kit conductor_kits[DRUM_USER_KITS] CONDUCTOR_POOL;
static dillo_sound conductor_dillo_sounds[DILLO_USER_SOUNDS] CONDUCTOR_POOL;
/* Largest accepted legacy tail also fits during in-place migration. */
static uint8_t conductor_snapshot[CONDUCTOR_MAX_BYTES] CONDUCTOR_POOL;
static unsigned conductor_snapshot_size = CONDUCTOR_WIRE_BYTES;
_Static_assert(CONDUCTOR_SOUNDS_BYTES <= sizeof conductor_snapshot &&
               CONDUCTOR_KITS_BYTES <= sizeof conductor_snapshot &&
               CONDUCTOR_SCENES_BYTES <= sizeof conductor_snapshot &&
               CONDUCTOR_MIX_BYTES <= sizeof conductor_snapshot &&
               CONDUCTOR_LEGACY_BYTES <= sizeof conductor_snapshot, "Conductor migration buffer too small");
static uint8_t conductor_snapshot_present;

static void conductor_put16(uint8_t **out, unsigned value)
{
    *(*out)++ = (uint8_t)value;
    *(*out)++ = (uint8_t)(value >> 8);
}

static unsigned conductor_get16(const uint8_t **in)
{
    unsigned value = *(*in)++;
    return value | (unsigned)*(*in)++ << 8;
}

/* Keep the 16-byte name field and its final NUL. For long catalogue names,
 * put a stable name key after the fallback prefix's NUL. Older readers see
 * the prefix. Neither catalogue order nor edited patch data enters the key. */
enum { CONDUCTOR_NAME_PREFIX = TG_NAME_BYTES - 6 };

static uint32_t conductor_name_key(const char *name)
{
    uint32_t key = 2166136261u;
    for (; *name; name++) key = (key ^ (uint8_t)*name) * 16777619u;
    return key ? key : 1;
}

static int conductor_setup_label(const char *name, unsigned length)
{
    return length == 7 && !memcmp(name, "SETUP ", 6) && name[6] >= '1' && name[6] <= '8';
}

static void conductor_encode_name(uint8_t *out, const char *name)
{
    unsigned length = 0;
    while (length < TG_NAME_BYTES && name[length]) length++;
    memset(out, 0, TG_NAME_BYTES);
    if (length < TG_NAME_BYTES) {
        memcpy(out, name, length);
        /* Distinguish a literal sound name from an old scene placeholder. */
        if (conductor_setup_label(name, length)) out[length + 1u] = 1;
        return;
    }
    memcpy(out, name, CONDUCTOR_NAME_PREFIX);
    uint32_t key = conductor_name_key(name);
    uint8_t *tag = out + CONDUCTOR_NAME_PREFIX + 1u;
    conductor_put16(&tag, key & 65535u);
    conductor_put16(&tag, key >> 16);
}

static const char *conductor_identify_sound(const char *prefix, unsigned length, uint32_t key,
                                           const tg_patch *patch, unsigned include_user)
{
    const char *match = NULL;
    unsigned catalogue = TG_PRESETS + tg_imported_count();
    unsigned count = catalogue + (include_user ? TG_USER_SLOTS : 0);
    for (unsigned i = 0; i < count; i++) {
        const char *name;
        const tg_patch *sound;
        if (i < catalogue) {
            const tg_preset *preset = i < TG_PRESETS ? &tg_presets[i] : &tg_imported_at(i - TG_PRESETS)->sound;
            name = preset->name;
            sound = &preset->patch;
        } else {
            const tg_saved_patch *saved = &user_library.patches[i - catalogue];
            name = saved->name;
            sound = &saved->patch;
        }
        if (!*name || (patch && memcmp(patch, sound, sizeof *patch))) continue;
        unsigned n = 0;
        while (n < length && name[n] && name[n] == prefix[n]) n++;
        if (n != length || (key && conductor_name_key(name) != key)) continue;
        if (match) {
            const char *a = match, *b = name;
            while (*a && *a == *b) { a++; b++; }
            if (*a != *b) return NULL;
        }
        match = name;
    }
    return match;
}

static const char *conductor_resolve_startup_name(void)
{
    const char *name = conductor_startup_sound.name;
    unsigned length = 0;
    while (name[length]) length++;
    const char *match = NULL;
    if (length == CONDUCTOR_NAME_PREFIX) {
        const uint8_t *tag = (const uint8_t *)name + length + 1u;
        uint32_t key = conductor_get16(&tag);
        key |= (uint32_t)conductor_get16(&tag) << 16;
        if (key) match = conductor_identify_sound(name, length, key, NULL, 0);
    } else if (length == TG_NAME_BYTES - 1u) {
        match = conductor_identify_sound(name, length, 0, &conductor_startup_sound.patch, 0);
    } else if (conductor_setup_label(name, length) && !name[length + 1u]) {
        match = conductor_identify_sound(NULL, 0, 0, &conductor_startup_sound.patch, 0);
        if (!match) match = "STARTUP PATCH";
    }
    return match ? match : name;
}

/* A roll settings are followed by a little-endian arp word. The previously
 * unused top bit stores A's roll latch preference. */
enum {
    CONDUCTOR_PERFORMANCE_LEGACY = 255,
    CONDUCTOR_ROLL_RATES = TG_ARP_RATE_COUNT + 1,
    CONDUCTOR_ROLL_MAX = (1 + DRUM_ROLL_SWING_MAX) * CONDUCTOR_ROLL_RATES - 1
};
_Static_assert(CONDUCTOR_ROLL_MAX < CONDUCTOR_PERFORMANCE_LEGACY, "roll settings overlap the legacy marker");
static const uint8_t default_performance[CONDUCTOR_PERFORMANCE_BYTES] = {CONDUCTOR_PERFORMANCE_LEGACY};

/* The original 0..8 rate bytes decode as straight rolls. */
static uint8_t conductor_pack_roll(const drum_roll *roll)
{
    return (uint8_t)(roll->rate + CONDUCTOR_ROLL_RATES * roll->swing);
}

static drum_roll conductor_unpack_roll(unsigned packed)
{
    return (drum_roll){.rate = packed % CONDUCTOR_ROLL_RATES, .swing = packed / CONDUCTOR_ROLL_RATES};
}

static void conductor_capture_performance(const struct panel *panel, uint8_t *out)
{
    const tg_controls *sound = &panel->sound;
    uint32_t arp = sound->arp | (uint32_t)sound->arp_rate << 3 | (uint32_t)sound->arp_gate << 7 |
        (uint32_t)sound->arp_octaves << 14 | (uint32_t)sound->arp_swing << 16 |
        (uint32_t)panel->arp_latch << 21 | (uint32_t)sound->arp_tempo << 22 |
        (uint32_t)!!(panel->drum_latch & DRUM_LATCH_A) << 31;
    *out++ = conductor_pack_roll(&panel->drums_roll);
    conductor_put16(&out, arp & 0xffffu);
    conductor_put16(&out, arp >> 16);
}

static int conductor_read_performance(struct panel *panel, const uint8_t *in)
{
    unsigned roll = *in++;
    uint32_t arp = conductor_get16(&in);
    arp |= (uint32_t)conductor_get16(&in) << 16;
    if (roll == CONDUCTOR_PERFORMANCE_LEGACY) return !arp;
    unsigned mode = arp & 7u, rate = arp >> 3 & 15u, gate = arp >> 7 & 127u;
    unsigned swing = arp >> 16 & 31u, tempo = (arp >> 22) & 511u;
    if (roll > CONDUCTOR_ROLL_MAX || mode > TG_ARP_REPEAT || rate > TG_ARP_RATE_COUNT ||
        (gate && (gate < 5 || gate > 100)) || swing > 25 || (tempo && (tempo < 30 || tempo > 300))) return 0;
    if (panel) {
        panel->drums_roll = conductor_unpack_roll(roll);
        panel->drum_latch = (panel->drum_latch & ~DRUM_LATCH_A) | (arp >> 31);
        panel->sound.arp = (uint8_t)mode;
        panel->sound.arp_rate = (uint8_t)rate;
        panel->sound.arp_gate = (uint8_t)gate;
        panel->sound.arp_octaves = (uint8_t)(arp >> 14 & 3u);
        panel->sound.arp_swing = (uint8_t)swing;
        panel->arp_latch = (uint8_t)(arp >> 21 & 1u);
        panel->sound.arp_tempo = (uint16_t)tempo;
    }
    return 1;
}

static void conductor_capture_setup(const struct panel *panel, uint8_t *out)
{
    conductor_put16(&out, panel->sound.tempo);
    *out++ = panel->sound.master;
    *out++ = (panel->drums_enabled ? CONDUCTOR_A_ENABLED : 0) |
             (panel->dillo_enabled ? CONDUCTOR_DILLO_ENABLED : 0) | panel->conductor_telegram << 2 |
             panel->conductor_dillo_keys << 3 | panel->conductor_bank << 4 |
             (panel->sound.phrase_muted ? CONDUCTOR_TELEGRAM_MUTED : 0);
    *out++ = panel->drums_slot;
    *out++ = panel->dillo_slot;
    *out = panel->telegram_slot;
}

static int conductor_valid_setup(const uint8_t *setup, unsigned drums, unsigned dillo, unsigned telegram)
{
    unsigned tempo = conductor_get16(&setup);
    unsigned master = *setup++, flags = *setup++;
    unsigned drum_slot = *setup++, dillo_slot = *setup++, telegram_slot = *setup;
    return tempo >= 30 && tempo <= 300 && master <= 127 && flags < 2u * CONDUCTOR_TELEGRAM_MUTED &&
           drum_slot < CONDUCTOR_SLOTS && dillo_slot < CONDUCTOR_SLOTS && telegram_slot < CONDUCTOR_SLOTS &&
           ((drums | 1u) & (1u << drum_slot)) && ((dillo | 1u) & (1u << dillo_slot)) &&
           ((telegram | 1u) & (1u << telegram_slot));
}

static int conductor_decode_mixer(conductor_mixer *mixer, const uint8_t *in)
{
    for (unsigned i = 0; i < CONDUCTOR_MIX_SIZE; i++) if (in[i] > 127) return 0;
    memcpy(mixer->value, in, CONDUCTOR_MIX_SIZE);
    return 1;
}

static void conductor_cue_setup_patterns(struct panel *panel, const uint8_t *setup)
{
    conductor_cue_pattern(panel, MIX_808, setup[CONDUCTOR_A_SLOT]);
    conductor_cue_pattern(panel, MIX_DILLO, setup[CONDUCTOR_DILLO_SLOT]);
    conductor_cue_pattern(panel, MIX_TELEGRAM, setup[CONDUCTOR_TELEGRAM_SLOT]);
}

static void conductor_apply_mix(struct panel *panel, const conductor_mixer *mixer)
{
    panel->mixer = *mixer;
    panel->sound.patch.value[TG_DELAY] = mixer->value[MIX_TELEGRAM][MIX_DELAY];
    panel->sound.patch.value[TG_REVERB] = mixer->value[MIX_TELEGRAM][MIX_ROOM];
    panel->sound.patch.drive = mixer->value[MIX_TELEGRAM][MIX_DRIVE];
}

static void conductor_apply_setup(struct panel *panel, const uint8_t *in, const conductor_mixer *mixer)
{
    clear_drum_rolls(panel, DRUM_LATCH_BOTH);
    conductor_cue_setup_patterns(panel, in);
    panel->sound.transpose = 0;
    unsigned tempo = conductor_get16(&in);
    unsigned master = *in++, flags = *in;
    panel->sound.tempo = (uint16_t)tempo;
    panel->sound.step_swing = 0;
    panel->sound.master = (uint8_t)master;
    panel->drums_enabled = !!(flags & CONDUCTOR_A_ENABLED);
    panel->dillo_enabled = !!(flags & CONDUCTOR_DILLO_ENABLED);
    panel->conductor_telegram = (flags >> 2) & 1u;
    panel->conductor_dillo_keys = (flags >> 3) & 1u;
    panel->conductor_bank = (flags >> 4) & 1u;
    panel->band_bank = (uint8_t)(panel->conductor_bank * 2u); /* saved bit: slots 1-4 or 9-12 */
    panel->sound.phrase_muted = !!(flags & CONDUCTOR_TELEGRAM_MUTED);
    panel->conductor_lane = panel->conductor_telegram ? PAGE_STEPS : PAGE_DILLO;
    conductor_apply_mix(panel, mixer);
}

/* 0 is rest, 1..100 are tie gates, and notes encode all pitch, gate and
 * accent/slide combinations above that. No precision is lost. */
static unsigned conductor_pack_step(const dillo_step *step)
{
    if (step->event != DILLO_NOTE) return step->gate_percent;
    return 101u + 100u * (128u * step->flags + step->pitch) + step->gate_percent - 1u;
}

static dillo_step conductor_unpack_step(unsigned value)
{
    if (value <= 100)
        return (dillo_step){.event = value ? DILLO_TIE : DILLO_REST, .gate_percent = (uint8_t)value};
    value -= 101;
    return (dillo_step){.event = DILLO_NOTE, .pitch = (uint8_t)(value / 100u % 128u),
        .gate_percent = (uint8_t)(1u + value % 100u), .flags = (uint8_t)(value / 12800u)};
}

/* A 1% tie outside a short phrase is a legal, inaudible step for older
 * readers. Use it as a loop marker without changing the journal size. */
enum { CONDUCTOR_DILLO_LOOP_MARKER = 1 };

static void conductor_pack_dillo(uint8_t *out, const dillo_phrase *phrase)
{
    *out++ = phrase->length;
    unsigned short_loop = dillo_phrase_grid(phrase) < DILLO_STEPS;
    for (unsigned step = 0; step < DILLO_STEPS; step++)
        conductor_put16(&out, short_loop && step == DILLO_STEPS - 1u ?
                         CONDUCTOR_DILLO_LOOP_MARKER : conductor_pack_step(&phrase->steps[step]));
}

/* Each step is a ternary digit: rest, hit, or accented hit.
 * Five consecutive digits fit in one byte. */
static void conductor_pack_drums(uint8_t *out, const drum_pattern *pattern)
{
    unsigned value = 0, place = 1;
    for (unsigned i = 0; i < D8_NUM * DRUM_STEPS; i++) {
        unsigned track = i / DRUM_STEPS, bit = 1u << (i % DRUM_STEPS);
        if (pattern->hits[track] & bit) value += place * ((pattern->accents[track] & bit) ? 2u : 1u);
        place *= 3;
        if (place == 243) {
            *out++ = (uint8_t)value;
            place = 1;
            value = 0;
        }
    }
    if (place != 1) *out = (uint8_t)value;
}

static int conductor_unpack_drums(drum_pattern *pattern, const uint8_t *in)
{
    unsigned value = 0;
    memset(pattern, 0, sizeof *pattern);
    for (unsigned i = 0; i < D8_NUM * DRUM_STEPS; i++) {
        if (i % 5 == 0) {
            if (value) return 0;
            value = *in++;
        }
        unsigned track = i / DRUM_STEPS;
        uint16_t bit = (uint16_t)(1u << (i % DRUM_STEPS));
        unsigned digit = value % 3;
        if (digit) pattern->hits[track] |= bit;
        if (digit == 2) pattern->accents[track] |= bit;
        value /= 3;
    }
    return value == 0;
}

static int conductor_kit_exists(unsigned slots, unsigned slot)
{
    return slot <= DRUM_USER_KITS && ((slots << 1 | 1u) & (1u << slot));
}

static int conductor_decode_kit(drum_kit *kit, const uint8_t *in)
{
    for (unsigned i = 0; i < CONDUCTOR_KIT_BYTES; i++) if (in[i] > 127) return 0;
    memcpy(kit->value, in, CONDUCTOR_KIT_BYTES);
    kit->alternate = 0; /* the switch tail, when present, follows the decay bytes */
    return 1;
}

static int conductor_dillo_sound_exists(unsigned slots, unsigned slot)
{
    return slot < dillo_sound_total() && (slot < BB_DILLO_PATCHES || slot >= DILLO_SOUND_PAGE_SLOTS ||
        (slots & (1u << (slot - BB_DILLO_PATCHES))));
}

static void conductor_pack_dillo_sound(uint8_t *out, const bb_dillo_patch *patch)
{
    conductor_put16(&out, patch->cutoff_hz);
    conductor_put16(&out, patch->resonance_x100);
    *out++ = patch->wave;
    *out++ = patch->envelope;
    *out++ = patch->decay;
    *out++ = patch->accent;
    *out++ = patch->drive;
    *out++ = patch->slide_ms;
    *out++ = patch->attack_ms;
    *out = patch->tracking;
}

static int conductor_unpack_dillo_sound(bb_dillo_patch *patch, const uint8_t *in)
{
    patch->cutoff_hz = (uint16_t)conductor_get16(&in);
    patch->resonance_x100 = (uint16_t)conductor_get16(&in);
    patch->wave = *in++;
    patch->envelope = *in++;
    patch->decay = *in++;
    patch->accent = *in++;
    patch->drive = *in++;
    patch->slide_ms = *in++;
    patch->attack_ms = *in++;
    patch->tracking = *in;
    return dillo_patch_valid(patch);
}

static void conductor_pack_dillo_decay(uint8_t *out, dillo_decay decay)
{
    /* Both decays use seven bits. Stock keeps the exact previous encoding. */
    out[0] = decay.accent | ((decay.sweep & 1u) << 7);
    out[1] = decay.amp | ((decay.sweep & 2u) << 6);
}

enum { B_ENABLED = 1, B_MUTED = 2, B_INITIALIZED = 4, B_FOCUS = 8, B_LATCH = 16 };
static const uint8_t default_b_setup[CONDUCTOR_B_SETUP_BYTES] = {0, 0, 0, 0, 90, 64};
#ifdef ELITE_PERCUSSION_727
enum { B_KIT_SHIFT = 5, B_KIT_MASK = 3u << B_KIT_SHIFT, B_STOCK_KIT = (P727_STOCK + 1u) << B_KIT_SHIFT };

static unsigned conductor_b_layer_kit(unsigned flags)
{
    return (flags & B_KIT_MASK) >> B_KIT_SHIFT;
}

static void conductor_migrate_layer(struct panel *panel)
{
    /* Never play old 808 B hits through the percussion mapping. Boot only
     * updates RAM; the original journal survives until an explicit save. */
    if (!panel->drums_b.percussion_kit) {
        panel->drums_b_slots = 0;
        memset(&panel->drums_b.pattern, 0, sizeof panel->drums_b.pattern);
        panel->drums_b.enabled = panel->drums_b.muted = panel->drums_b.slot = 0;
        panel->drums_b_initialized = 0;
        panel->drums_focus = 0;
        panel->drum_latch &= ~DRUM_LATCH_B;
        panel->drums_b.percussion_kit = P727_STOCK + 1u;
    }
    for (unsigned slot = 0; slot < CONDUCTOR_SCENES; slot++) {
        uint8_t *setup = conductor_scenes[slot].drums_b;
        if ((panel->scene_slots & (1u << slot)) && !conductor_b_layer_kit(setup[0])) {
            setup[0] = B_STOCK_KIT;
            setup[CONDUCTOR_B_SLOT] = 0;
        }
    }
}
#endif

static void conductor_capture_b_setup(const struct panel *panel, uint8_t *out)
{
    const conductor_drum_part *part = &panel->drums_b;
    unsigned flags = part->enabled | part->muted << 1 | panel->drums_b_initialized << 2 |
                     panel->drums_focus << 3 | ((panel->drum_latch & DRUM_LATCH_B) ? B_LATCH : 0);
#ifdef ELITE_PERCUSSION_727
    flags |= (unsigned)part->percussion_kit << B_KIT_SHIFT;
#endif
    *out++ = (uint8_t)flags;
    *out++ = part->slot;
    *out++ = panel->drums_b_kit_slot;
    *out++ = conductor_pack_roll(&part->roll);
    memcpy(out, part->mix, MIX_CONTROLS);
}

static int conductor_valid_b_setup(const uint8_t *in, unsigned slots, unsigned kits)
{
    unsigned flags = *in++, slot = *in++, kit = *in++, roll = *in++;
#ifdef ELITE_PERCUSSION_727
    if (conductor_b_layer_kit(flags) > P727_KITS) return 0;
    flags &= ~B_KIT_MASK;
#endif
    if (flags > (B_ENABLED | B_MUTED | B_INITIALIZED | B_FOCUS | B_LATCH) ||
        slot >= CONDUCTOR_SLOTS || !((slots | 1u) & (1u << slot)) ||
        (!(flags & B_INITIALIZED) && ((flags & (B_ENABLED | B_MUTED)) || slot)) ||
        !conductor_kit_exists(kits, kit) || roll > CONDUCTOR_ROLL_MAX) return 0;
    for (unsigned i = 0; i < MIX_CONTROLS; i++) if (in[i] > 127) return 0;
    return 1;
}

static void conductor_apply_b_setup(struct panel *panel, const uint8_t *in)
{
    conductor_drum_part *part = &panel->drums_b;
    unsigned flags = *in++, slot = *in++;
#ifdef ELITE_PERCUSSION_727
    part->percussion_kit = (uint8_t)conductor_b_layer_kit(flags);
#endif
    part->enabled = !!(flags & B_ENABLED);
    part->muted = !!(flags & B_MUTED);
    panel->drums_b_initialized = !!(flags & B_INITIALIZED);
    conductor_forget_drum_undo(panel, 1);
    panel->drums_focus = !!(flags & B_FOCUS);
    panel->drum_latch = (panel->drum_latch & ~DRUM_LATCH_B) | ((flags & B_LATCH) ? DRUM_LATCH_B : 0);
    panel->drums_b_kit_slot = *in++;
    part->roll = conductor_unpack_roll(*in++);
    part->repeat = 0;
    memcpy(part->mix, in, MIX_CONTROLS);
    part->slot = (uint8_t)slot;
    if (!panel->drums_b_initialized) memset(&part->pattern, 0, sizeof part->pattern);
    else part->pattern = *conductor_b_pattern(panel, slot);
    part->cue++;
}

/* Library saves update the shared slots in the current tail. Only a
 * Conductor save (or initial seeding) replaces the startup fields. */
/* Returns the tail size. Without startup the working fields already in out are kept,
 * including the working kits' switch bytes, which are zero after a short tail. */
static unsigned conductor_encode(const struct panel *panel, uint8_t *out, unsigned startup)
{
    if (startup) conductor_capture_setup(panel, out);
    out += CONDUCTOR_SETUP_BYTES;
    conductor_put16(&out, panel->drums_slots);
    conductor_put16(&out, panel->dillo_slots);
    conductor_put16(&out, panel->telegram_slots);
    for (unsigned slot = 0; slot < CONDUCTOR_SLOTS; slot++) {
        unsigned bit = 1u << slot;
        if (panel->drums_slots & bit) conductor_pack_drums(out, &conductor_drum_slots[slot]);
        else memset(out, 0, CONDUCTOR_DRUM_BYTES);
        out += CONDUCTOR_DRUM_BYTES;
        if (panel->dillo_slots & bit) conductor_pack_dillo(out, &conductor_dillo_slots[slot]);
        else memset(out, 0, CONDUCTOR_DILLO_BYTES);
        out += CONDUCTOR_DILLO_BYTES;
        if (panel->telegram_slots & bit) {
            const tg_saved_phrase *phrase = &conductor_telegram_slots[slot];
            *out++ = phrase->length ? phrase->length : TG_PADS;
            *out++ = phrase->gate;
            memcpy(out, phrase->steps, TG_STEPS);
            out += TG_STEPS;
        } else {
            memset(out, 0, CONDUCTOR_TELEGRAM_BYTES);
            out += CONDUCTOR_TELEGRAM_BYTES;
        }
    }
    if (startup) {
        conductor_mixer mixer = conductor_mixer_controls(panel);
        memcpy(out, mixer.value, CONDUCTOR_MIX_SIZE);
    }
    out += CONDUCTOR_MIX_SIZE;
    *out++ = panel->scene_slots;
    for (unsigned slot = 0; slot < CONDUCTOR_SCENES; slot++, out += CONDUCTOR_SCENE_COMPACT) {
        const conductor_scene *scene = &conductor_scenes[slot];
        if (panel->scene_slots & (1u << slot)) {
            memcpy(out, scene->setup, CONDUCTOR_SETUP_BYTES);
            memcpy(out + CONDUCTOR_SETUP_BYTES, scene->mixer.value, CONDUCTOR_MIX_SIZE);
            tg_patch_pack_scene(out + CONDUCTOR_SETUP_BYTES + CONDUCTOR_MIX_SIZE, &scene->patch);
        } else memset(out, 0, CONDUCTOR_SCENE_COMPACT);
    }
    *out++ = panel->kit_slots;
    if (startup) {
        out[0] = panel->kit_slot;
        memcpy(out + 1, panel->drums_kit.value, CONDUCTOR_KIT_BYTES);
    }
    out += 1 + CONDUCTOR_KIT_BYTES;
    for (unsigned slot = 0; slot < DRUM_USER_KITS; slot++, out += CONDUCTOR_KIT_BYTES) {
        if (panel->kit_slots & (1u << slot))
            memcpy(out, conductor_kits[slot].value, CONDUCTOR_KIT_BYTES);
        else memset(out, 0, CONDUCTOR_KIT_BYTES);
    }
    for (unsigned slot = 0; slot < CONDUCTOR_SCENES; slot++)
        *out++ = panel->scene_slots & (1u << slot) ? conductor_scenes[slot].kit_slot : 0;
    *out++ = panel->dillo_sound_slots;
    if (startup) {
        out[0] = panel->dillo_sound_slot;
        conductor_pack_dillo_sound(out + 1, &panel->dillo_sound.tone);
    }
    out += 1 + CONDUCTOR_SOUND_BYTES;
    for (unsigned slot = 0; slot < DILLO_USER_SOUNDS; slot++, out += CONDUCTOR_SOUND_BYTES) {
        if (panel->dillo_sound_slots & (1u << slot))
            conductor_pack_dillo_sound(out, &conductor_dillo_sounds[slot].tone);
        else memset(out, 0, CONDUCTOR_SOUND_BYTES);
    }
    for (unsigned slot = 0; slot < CONDUCTOR_SCENES; slot++)
        *out++ = panel->scene_slots & (1u << slot) ? conductor_scenes[slot].dillo_sound_slot : DILLO_DEFAULT_SOUND;
    conductor_put16(&out, panel->drums_b_slots);
    if (startup) {
        conductor_capture_b_setup(panel, out);
        memcpy(out + CONDUCTOR_B_SETUP_BYTES, panel->drums_b.kit.value, CONDUCTOR_KIT_BYTES);
        conductor_pack_drums(out + CONDUCTOR_B_SETUP_BYTES + CONDUCTOR_KIT_BYTES, &panel->drums_b.pattern);
    }
#ifdef ELITE_PERCUSSION_727
    else if (panel->layer_available && !conductor_b_layer_kit(out[0])) {
        /* A library-only save keeps the other startup lanes. Invalidate the
         * old B slot reference before serializing the new layer slot map. */
        out[0] = B_STOCK_KIT;
        out[CONDUCTOR_B_SLOT] = 0;
        memset(out + CONDUCTOR_B_SETUP_BYTES + CONDUCTOR_KIT_BYTES, 0, CONDUCTOR_DRUM_BYTES);
    }
#endif
    out += CONDUCTOR_B_SETUP_BYTES + CONDUCTOR_KIT_BYTES + CONDUCTOR_DRUM_BYTES;
    for (unsigned slot = 0; slot < CONDUCTOR_SLOTS; slot++, out += CONDUCTOR_DRUM_BYTES) {
        if (panel->drums_b_slots & (1u << slot)) conductor_pack_drums(out, &conductor_drum_b_slots[slot]);
        else memset(out, 0, CONDUCTOR_DRUM_BYTES);
    }
    for (unsigned slot = 0; slot < CONDUCTOR_SCENES; slot++, out += CONDUCTOR_B_SETUP_BYTES)
        memcpy(out, panel->scene_slots & (1u << slot) ? conductor_scenes[slot].drums_b :
               default_b_setup, CONDUCTOR_B_SETUP_BYTES);
    if (startup) {
        const char *name = panel_sound_name(panel);
        if (panel->scene_sound) {
            name = conductor_identify_sound(NULL, 0, 0, &conductor_scenes[panel->scene_sound - 1u].patch, 1);
            if (!name) name = "STARTUP PATCH";
        }
        if (panel->startup_sound && name == conductor_startup_sound.name)
            memcpy(out, conductor_startup_sound.name, TG_NAME_BYTES);
        else conductor_encode_name(out, name);
        tg_patch_encode(out + TG_NAME_BYTES, &panel->sound.patch);
        conductor_capture_performance(panel, out + TG_NAME_BYTES + TG_PATCH_WIRE_BYTES);
    }
    out += TG_NAME_BYTES + TG_PATCH_WIRE_BYTES + CONDUCTOR_PERFORMANCE_BYTES;
    for (unsigned slot = 0; slot < CONDUCTOR_SCENES; slot++, out += CONDUCTOR_PERFORMANCE_BYTES)
        memcpy(out, panel->scene_slots & (1u << slot) ? conductor_scenes[slot].performance :
               default_performance, CONDUCTOR_PERFORMANCE_BYTES);
    if (startup) conductor_pack_dillo_decay(out, panel->dillo_sound.decay);
    out += CONDUCTOR_DECAY_BYTES;
    for (unsigned slot = 0; slot < DILLO_USER_SOUNDS; slot++, out += CONDUCTOR_DECAY_BYTES)
        conductor_pack_dillo_decay(out, panel->dillo_sound_slots & (1u << slot) ?
                                   conductor_dillo_sounds[slot].decay : (dillo_decay){0});
    uint32_t field = startup ? panel->drums_kit.alternate :
                     (out[0] | (uint32_t)out[1] << 8 | (uint32_t)out[2] << 16) & DRUM_ALTERNATE_MASK;
    for (unsigned slot = 0; slot < DRUM_USER_KITS; slot++)
        if (panel->kit_slots & (1u << slot))
            field |= (uint32_t)conductor_kits[slot].alternate << (DRUM_ALTERNATES * (slot + 1u));
    for (unsigned i = 0; i < CONDUCTOR_SWITCH_BYTES; i++) out[i] = (uint8_t)(field >> (8 * i));
    return field ? CONDUCTOR_SWITCH_WIRE_BYTES : CONDUCTOR_WIRE_BYTES;
}

static int conductor_restore(struct panel *panel, const uint8_t *in, unsigned size)
{
    const uint8_t *setup = in;
    in += CONDUCTOR_SETUP_BYTES;
    unsigned drums = conductor_get16(&in), dillo = conductor_get16(&in), telegram = conductor_get16(&in);
    if (!conductor_valid_setup(setup, drums, dillo, telegram)) return 0;
    unsigned has_switches = size == CONDUCTOR_SWITCH_WIRE_BYTES;
    unsigned has_decay = has_switches || size == CONDUCTOR_WIRE_BYTES;
    unsigned has_performance = has_decay || size == CONDUCTOR_PERFORMANCE_TAIL_BYTES;
    unsigned has_startup_sound = has_performance || size == CONDUCTOR_STARTUP_SOUND_BYTES;
    unsigned has_b = has_startup_sound || size == CONDUCTOR_DUAL_DRUM_BYTES;
    unsigned has_dillo_sounds = has_b || size == CONDUCTOR_SOUNDS_BYTES;
    unsigned has_kits = has_dillo_sounds || size == CONDUCTOR_KITS_BYTES;
    unsigned has_scenes = has_kits || size == CONDUCTOR_SCENES_BYTES;

    /* Publish occupancy only after the entire snapshot validates. */
    for (unsigned slot = 0; slot < CONDUCTOR_SLOTS; slot++) {
        unsigned bit = 1u << slot;
        drum_pattern pattern;
        if (has_kits) {
            if (!conductor_unpack_drums(&pattern, in)) return 0;
            in += CONDUCTOR_DRUM_BYTES;
        } else {
            for (unsigned track = 0; track < D8_NUM; track++) {
                pattern.hits[track] = (uint16_t)conductor_get16(&in);
                pattern.accents[track] = (uint16_t)conductor_get16(&in);
                if ((drums & bit) && (pattern.accents[track] & ~pattern.hits[track])) return 0;
            }
        }
        conductor_drum_slots[slot] = pattern;
        dillo_phrase phrase = {.length = *in++};
        if ((dillo & bit) && (!phrase.length || phrase.length > DILLO_STEPS)) return 0;
        for (unsigned step = 0; step < DILLO_STEPS; step++) {
            dillo_step entry;
            if (has_scenes) {
                unsigned value = conductor_get16(&in);
                if ((dillo & bit) && value > 51300u) return 0;
                if ((dillo & bit) && phrase.length < DILLO_STEPS && step == DILLO_STEPS - 1u &&
                    value == CONDUCTOR_DILLO_LOOP_MARKER) {
                    phrase.repeat_short = 1;
                    value = 0;
                }
                entry = conductor_unpack_step(value);
            } else {
                entry = (dillo_step){in[0], in[1], in[2], in[3]};
                in += 4;
            }
            if ((dillo & bit) && !dillo_phrase_set_step(&phrase, step, entry)) return 0;
        }
        conductor_dillo_slots[slot] = phrase;
        tg_saved_phrase *tg = &conductor_telegram_slots[slot];
        tg->length = *in++;
        tg->gate = *in++;
        if (telegram & bit) {
            if (!tg->length || tg->length > TG_STEPS || (tg->gate && (tg->gate < 5 || tg->gate > 100)))
                return 0;
            for (unsigned step = 0; step < TG_STEPS; step++) if (in[step] == TG_TIE) return 0;
        }
        memcpy(tg->steps, in, TG_STEPS);
        in += TG_STEPS;
    }
    conductor_mixer mixer = conductor_mixer_controls(panel);
    if (size != CONDUCTOR_LEGACY_BYTES && !conductor_decode_mixer(&mixer, in)) return 0;
    unsigned scenes = 0;
    if (has_scenes) {
        in += CONDUCTOR_MIX_SIZE;
        scenes = *in++;
        unsigned scene_bytes = has_dillo_sounds ? CONDUCTOR_SCENE_COMPACT : CONDUCTOR_SCENE_BYTES;
        for (unsigned slot = 0; slot < CONDUCTOR_SCENES; slot++, in += scene_bytes) {
            if (!(scenes & (1u << slot))) continue;
            conductor_scene *scene = &conductor_scenes[slot];
            if (!conductor_valid_setup(in, drums, dillo, telegram) ||
                !conductor_decode_mixer(&scene->mixer, in + CONDUCTOR_SETUP_BYTES)) return 0;
            const uint8_t *patch = in + CONDUCTOR_SETUP_BYTES + CONDUCTOR_MIX_SIZE;
            if (!(has_dillo_sounds ? tg_patch_unpack_scene(&scene->patch, patch) :
                  tg_patch_decode(&scene->patch, patch))) return 0;
            memcpy(scene->setup, in, CONDUCTOR_SETUP_BYTES);
        }
    }
    unsigned kit_slots = 0, kit_slot = 0;
    drum_kit working;
    if (has_kits) {
        kit_slots = *in++;
        kit_slot = *in++;
        if (kit_slots >= (1u << DRUM_USER_KITS) || !conductor_kit_exists(kit_slots, kit_slot) ||
            !conductor_decode_kit(&working, in)) return 0;
        in += CONDUCTOR_KIT_BYTES;
        for (unsigned slot = 0; slot < DRUM_USER_KITS; slot++, in += CONDUCTOR_KIT_BYTES)
            if (!conductor_decode_kit(&conductor_kits[slot], in)) return 0;
        for (unsigned slot = 0; slot < CONDUCTOR_SCENES; slot++) {
            if (!conductor_kit_exists(kit_slots, in[slot])) return 0;
            conductor_scenes[slot].kit_slot = in[slot];
        }
    } else {
        drum_kit_default(&working);
        for (unsigned slot = 0; slot < CONDUCTOR_SCENES; slot++) conductor_scenes[slot].kit_slot = 0;
    }
    dillo_sound working_sound = {.tone = bb_dillo_bank[DILLO_DEFAULT_SOUND].sound};
    unsigned dillo_sound_slots = 0, dillo_sound_slot = DILLO_DEFAULT_SOUND;
    if (has_dillo_sounds) {
        in += CONDUCTOR_SCENES;
        dillo_sound_slots = *in++;
        dillo_sound_slot = *in++;
        if (dillo_sound_slots >= (1u << DILLO_USER_SOUNDS) ||
            !conductor_dillo_sound_exists(dillo_sound_slots, dillo_sound_slot) ||
            !conductor_unpack_dillo_sound(&working_sound.tone, in)) return 0;
        in += CONDUCTOR_SOUND_BYTES;
        for (unsigned slot = 0; slot < DILLO_USER_SOUNDS; slot++, in += CONDUCTOR_SOUND_BYTES)
            if ((dillo_sound_slots & (1u << slot)) &&
                !conductor_unpack_dillo_sound(&conductor_dillo_sounds[slot].tone, in)) return 0;
        for (unsigned slot = 0; slot < CONDUCTOR_SCENES; slot++) {
            if (!conductor_dillo_sound_exists(dillo_sound_slots, in[slot])) return 0;
            conductor_scenes[slot].dillo_sound_slot = in[slot];
        }
    } else {
        for (unsigned slot = 0; slot < CONDUCTOR_SCENES; slot++)
            conductor_scenes[slot].dillo_sound_slot = DILLO_DEFAULT_SOUND;
    }
    const uint8_t *b_setup = default_b_setup;
    unsigned b_slots = 0;
    drum_kit b_kit;
    drum_pattern b_working;
    memset(&b_working, 0, sizeof b_working);
    if (has_b) {
        in += CONDUCTOR_SCENES;
        b_slots = conductor_get16(&in);
        b_setup = in;
        in += CONDUCTOR_B_SETUP_BYTES;
        if (!conductor_valid_b_setup(b_setup, b_slots, kit_slots) ||
            !conductor_decode_kit(&b_kit, in)) return 0;
        in += CONDUCTOR_KIT_BYTES;
        if (!conductor_unpack_drums(&b_working, in)) return 0;
        in += CONDUCTOR_DRUM_BYTES;
        for (unsigned slot = 0; slot < CONDUCTOR_SLOTS; slot++, in += CONDUCTOR_DRUM_BYTES)
            if (!conductor_unpack_drums(&conductor_drum_b_slots[slot], in)) return 0;
    } else drum_kit_default(&b_kit);
    for (unsigned slot = 0; slot < CONDUCTOR_SCENES; slot++) {
        const uint8_t *scene_b = has_b ? in + slot * CONDUCTOR_B_SETUP_BYTES : default_b_setup;
        if (!conductor_valid_b_setup(scene_b, b_slots, kit_slots)) return 0;
        memcpy(conductor_scenes[slot].drums_b, scene_b, CONDUCTOR_B_SETUP_BYTES);
    }
    tg_saved_patch startup;
    if (has_startup_sound) {
        in += CONDUCTOR_SCENES * CONDUCTOR_B_SETUP_BYTES;
        if (!in[0] || in[TG_NAME_BYTES - 1u] ||
            !tg_patch_decode(&startup.patch, in + TG_NAME_BYTES)) return 0;
        for (unsigned i = 0; i < TG_NAME_BYTES && in[i]; i++)
            if (in[i] < 32 || in[i] > 126) return 0;
        memcpy(startup.name, in, TG_NAME_BYTES);
        in += TG_NAME_BYTES + TG_PATCH_WIRE_BYTES;
    }
    const uint8_t *performance = has_performance ? in : default_performance;
    if (!conductor_read_performance(NULL, performance) ||
        (has_performance && performance[0] == CONDUCTOR_PERFORMANCE_LEGACY)) return 0;
    for (unsigned slot = 0; slot < CONDUCTOR_SCENES; slot++) {
        const uint8_t *scene = has_performance ? in + (slot + 1u) * CONDUCTOR_PERFORMANCE_BYTES : default_performance;
        if (!conductor_read_performance(NULL, scene)) return 0;
        memcpy(conductor_scenes[slot].performance, scene, CONDUCTOR_PERFORMANCE_BYTES);
    }
    dillo_decay decay[1 + DILLO_USER_SOUNDS] = {{0}};
    if (has_decay) {
        in += (1 + CONDUCTOR_SCENES) * CONDUCTOR_PERFORMANCE_BYTES;
        for (unsigned i = 0; i <= DILLO_USER_SOUNDS; i++, in += CONDUCTOR_DECAY_BYTES) {
            dillo_decay saved = {in[0] & 127u, in[1] & 127u, (in[0] >> 7) | ((in[1] >> 7) << 1)};
            if (saved.accent > DILLO_DECAY_MAX || saved.amp > DILLO_DECAY_MAX ||
                saved.sweep >= DILLO_SWEEP_COUNT) return 0;
            decay[i] = saved;
        }
    }
    uint32_t field = 0;
    if (has_switches) {
        for (unsigned i = 0; i < CONDUCTOR_SWITCH_BYTES; i++) field |= (uint32_t)in[i] << (8 * i);
        /* No spare bits, no switches for an empty user kit and no empty field. */
        if (!field || field >> (DRUM_ALTERNATES * CONDUCTOR_SWITCH_KITS)) return 0;
        for (unsigned slot = 0; slot < DRUM_USER_KITS; slot++)
            if (!(kit_slots & (1u << slot)) && ((field >> (DRUM_ALTERNATES * (slot + 1u))) & DRUM_ALTERNATE_MASK))
                return 0;
    }
    working.alternate = (uint8_t)(field & DRUM_ALTERNATE_MASK);
    for (unsigned slot = 0; slot < DRUM_USER_KITS; slot++)
        conductor_kits[slot].alternate = (uint8_t)((field >> (DRUM_ALTERNATES * (slot + 1u))) & DRUM_ALTERNATE_MASK);
    working_sound.decay = decay[0];
    for (unsigned slot = 0; slot < DILLO_USER_SOUNDS; slot++)
        conductor_dillo_sounds[slot].decay = decay[slot + 1u];
    panel->drums_b_slots = (uint16_t)b_slots;
    conductor_apply_b_setup(panel, b_setup);
    panel->drums_b.kit = b_kit;
    panel->drums_b.pattern = b_working;
    panel->dillo_sound = working_sound;
    panel->dillo_sound_slot = (uint8_t)dillo_sound_slot;
    panel->dillo_sound_slots = (uint8_t)dillo_sound_slots;
    panel->kit_slots = (uint8_t)kit_slots;
    panel->kit_slot = (uint8_t)kit_slot;
    panel->drums_kit = working;
    panel->drums_slots = (uint16_t)drums;
    panel->dillo_slots = (uint16_t)dillo;
    panel->telegram_slots = (uint16_t)telegram;
    panel->scene_slots = (uint8_t)scenes;
    conductor_apply_setup(panel, setup, &mixer);
    conductor_read_performance(panel, performance);
    if (has_startup_sound) {
        conductor_startup_sound = startup;
        conductor_startup_label = conductor_resolve_startup_name();
        panel->sound.patch = startup.patch;
        panel->startup_sound = 1;
    }
    return 1;
}

static void conductor_save(struct panel *panel)
{
    unsigned startup = panel->page == PAGE_CONDUCTOR;
    if (panel->conductor_save_state == CONDUCTOR_SAVE_FAILED)
        panel->conductor_save_state = CONDUCTOR_SAVE_NONE;
    if (startup) {
        panel->conductor_copy_needed = (uint8_t)conductor_unstored_pattern(panel, 0);
        if (panel->conductor_copy_needed) return;
    }
    conductor_snapshot_size = conductor_encode(panel, conductor_snapshot, startup);
    conductor_snapshot_present = 1;
    panel->conductor_save_edits = panel->conductor_edits;
    /* A library-only save can update the slots of an already staged startup. */
    unsigned save_startup = startup || (panel->conductor_save_state & CONDUCTOR_SAVE_STARTUP);
    panel->conductor_save_state = CONDUCTOR_SAVE_PENDING | (save_startup ? CONDUCTOR_SAVE_STARTUP : 0);
    panel->store_request = panel->library_dirty = 1;
    panel->library_status = LIB_WAIT_STOP;
}
