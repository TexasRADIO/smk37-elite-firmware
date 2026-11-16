/* SPDX-License-Identifier: GPL-3.0-only */
/* Big Beat Editor protocol v0 for BB-37 Elite.
 *
 * This adapter is foreground-only.  It edits the panel's pending snapshot;
 * main_loop publishes that snapshot through the existing audio-owner handoff.
 * It never parses USB or mutates renderer state in the audio interrupt. */

enum {
    ED_PROTOCOL = 0,
    ED_PRODUCT_BB37 = 3,
    ED_HELLO = 0x01, ED_HELLO_REPLY = 0x02,
    ED_CAPABILITIES = 0x03, ED_CAPABILITIES_REPLY = 0x04,
    ED_SNAPSHOT_BEGIN = 0x10, ED_SNAPSHOT_CHUNK = 0x11, ED_SNAPSHOT_END = 0x12,
    ED_TX_BEGIN = 0x20, ED_SET = 0x21, ED_TX_COMMIT = 0x23, ED_TX_ABORT = 0x24,
    ED_ACK = 0x30, ED_REJECT = 0x31, ED_CONFLICT = 0x33, ED_APPLIED = 0x34,
    ED_PLAY = 0x40, ED_STOP = 0x41,
    ED_PING = 0x70, ED_PONG = 0x71, ED_SETTLE = 0x73, ED_SETTLE_REPLY = 0x74,
    ED_OK = 0, ED_QUEUED = 1, ED_REFUSED = 2, ED_STALE = 3,
    ED_UNKNOWN_FIELD = 4, ED_BUSY = 5, ED_NOT_SUPPORTED = 6,
    ED_GLOBAL = 7, ED_TELEGRAM = 9, ED_DILLO = 4,
    ED_CONDUCTOR = 1, ED_MIX = 6, ED_BAND_727 = 11,
    ED_MAX_CHUNK = 112,
};

typedef struct {
    uint32_t revision;
    uint16_t tx_id, staged_field, staged_value;
    uint8_t connected, tx_open, tx_domain, staged_target, staged_valid;
} elite_editor_state;

static elite_editor_state editor_state = {.revision = 1};
static uint8_t editor_raw[128], editor_packed[148], editor_wire[160];

/* Keep the protocol adapter out of the timing-sensitive optimizer lane.  The
 * functions marked ED_SMALL execute only in the foreground and favor the
 * Elite's bounded XIP budget; tiny byte helpers remain inline candidates. */
#define ED_SMALL __attribute__((noinline, minsize))

static uint16_t editor_read14(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)(p[0] & 0x7fu) << 7) | (p[1] & 0x7fu));
}

static uint32_t editor_read28(const uint8_t *p)
{
    return ((uint32_t)(p[0] & 0x7fu) << 21) |
           ((uint32_t)(p[1] & 0x7fu) << 14) |
           ((uint32_t)(p[2] & 0x7fu) << 7) | (p[3] & 0x7fu);
}

static ED_SMALL uint32_t editor_crc32(const uint8_t *p, unsigned n)
{
    uint32_t crc = 0xffffffffu;
    while (n--) {
        crc ^= *p++;
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return crc ^ 0xffffffffu;
}

static ED_SMALL unsigned editor_pack7(const uint8_t *in, unsigned n, uint8_t *out)
{
    unsigned at = 0;
    for (unsigned i = 0; i < n; i += 7) {
        unsigned count = n - i > 7 ? 7 : n - i;
        unsigned mask = at++;
        out[mask] = 0;
        for (unsigned j = 0; j < count; j++) {
            uint8_t byte = in[i + j];
            out[mask] |= (uint8_t)((byte >> 7) << j);
            out[at++] = byte & 0x7fu;
        }
    }
    return at;
}

static ED_SMALL unsigned editor_reply_begin(uint8_t type, uint16_t seq)
{
    static const uint8_t prefix[] = {0xf0, 0x00, 0x22, 0x24, 0x35, 0x05};
    memcpy(editor_wire, prefix, sizeof prefix);
    editor_wire[6] = type;
    editor_wire[7] = (uint8_t)((seq >> 7) & 0x7fu);
    editor_wire[8] = (uint8_t)(seq & 0x7fu);
    return 9;
}

static ED_SMALL unsigned editor_put14(unsigned at, uint16_t value)
{
    editor_wire[at++] = (uint8_t)((value >> 7) & 0x7fu);
    editor_wire[at++] = (uint8_t)(value & 0x7fu);
    return at;
}

static ED_SMALL unsigned editor_put32(unsigned at, uint32_t value)
{
    editor_wire[at++] = (uint8_t)((value >> 28) & 0x0fu);
    editor_wire[at++] = (uint8_t)((value >> 21) & 0x7fu);
    editor_wire[at++] = (uint8_t)((value >> 14) & 0x7fu);
    editor_wire[at++] = (uint8_t)((value >> 7) & 0x7fu);
    editor_wire[at++] = (uint8_t)(value & 0x7fu);
    return at;
}

static ED_SMALL void editor_send(unsigned n)
{
    editor_wire[n++] = 0xf7;
    (void)ota_wire_send(editor_wire, n);
}

static ED_SMALL void editor_result(uint8_t type, uint16_t seq, uint8_t result)
{
    unsigned n = editor_reply_begin(type, seq);
    editor_wire[n++] = result;
    editor_send(n);
}

static ED_SMALL void editor_raw16(unsigned at, uint16_t value)
{
    editor_raw[at] = (uint8_t)(value >> 8);
    editor_raw[at + 1u] = (uint8_t)value;
}

static void editor_raw32(unsigned at, uint32_t value)
{
    editor_raw[at] = (uint8_t)(value >> 24);
    editor_raw[at + 1u] = (uint8_t)(value >> 16);
    editor_raw[at + 2u] = (uint8_t)(value >> 8);
    editor_raw[at + 3u] = (uint8_t)value;
}

/* BB-37's v0 snapshot schema is fixed at 107 bytes. */
static ED_SMALL unsigned editor_snapshot(const struct panel *panel)
{
    static const uint8_t sections[][3] = {
        {4, ED_GLOBAL, 6}, {13, ED_TELEGRAM, 24}, {40, ED_DILLO, 17},
        {60, ED_CONDUCTOR, 10}, {73, ED_MIX, 20}, {96, ED_BAND_727, 8},
    };
    const bb_dillo_patch *tone = &panel->dillo_sound.tone;
    memset(editor_raw, 0, 107);
#pragma clang loop unroll(disable)
    for (unsigned i = 0; i < sizeof sections / sizeof sections[0]; i++) {
        editor_raw[sections[i][0]] = sections[i][1];
        editor_raw[sections[i][0] + 2u] = sections[i][2];
    }
    editor_raw32(0, editor_state.revision);

    editor_raw16(7, (uint16_t)(panel->sound.tempo * 10u));
    editor_raw[9] = panel->sound.step_swing;
    editor_raw[10] = panel->sound.master;
    editor_raw[11] = panel->sound.running;
    editor_raw[12] = panel->page;

    editor_raw[16] = panel->preset;
    memcpy(editor_raw + 17, panel->sound.patch.value, 18);
    editor_raw[35] = panel->sound.patch.drive;
    editor_raw[36] = panel->sound.patch.value[TG_DELAY];
    editor_raw[37] = panel->sound.patch.value[TG_REVERB];
    editor_raw[38] = panel->sound.arp;
    editor_raw[39] = panel->sound.arp_rate;

    editor_raw[43] = panel->dillo_sound_slot;
    editor_raw16(44, tone->cutoff_hz);
    editor_raw16(46, tone->resonance_x100);
    editor_raw[48] = tone->envelope;
    editor_raw[49] = tone->decay;
    editor_raw[50] = tone->accent;
    editor_raw[51] = tone->drive;
    editor_raw[52] = tone->wave;
    editor_raw[53] = tone->slide_ms;
    editor_raw[54] = tone->attack_ms;
    editor_raw[55] = tone->tracking;
    editor_raw[56] = panel->dillo_sound.decay.accent;
    editor_raw[57] = panel->dillo_sound.decay.amp;
    editor_raw16(58, panel->dillo_sound.decay.sweep);

    editor_raw[63] = panel->scene_queued;
    editor_raw[64] = panel->conductor_lane;
    editor_raw[65] = panel->telegram_slot;
    editor_raw[66] = panel->dillo_slot;
    editor_raw[67] = panel->drums_slot;
    editor_raw[68] = panel->drums_b.slot;
    editor_raw[69] = 1;
    editor_raw[70] = panel->dillo_enabled;
    editor_raw[71] = panel->drums_enabled;
    editor_raw[72] = panel->drums_b.enabled && !panel->drums_b.muted;

    /* These are contiguous byte arrays. Reuse the freestanding copy routine
     * rather than expanding fixed-count copies into foreground XIP code. */
    memcpy(editor_raw + 76, panel->mixer.value, 3u * MIX_CONTROLS);
    memcpy(editor_raw + 91, panel->drums_b.mix, MIX_CONTROLS);
    memcpy(editor_raw + 99, panel->layer_mix, 8);
    return 107;
}

static ED_SMALL void editor_send_snapshot(const struct panel *panel, uint16_t seq)
{
    unsigned raw = editor_snapshot(panel);
    unsigned packed = editor_pack7(editor_raw, raw, editor_packed);
    unsigned total = (packed + ED_MAX_CHUNK - 1u) / ED_MAX_CHUNK;
    uint32_t crc = editor_crc32(editor_raw, raw);
    for (unsigned chunk = 0; chunk < total; chunk++) {
        unsigned offset = chunk * ED_MAX_CHUNK;
        unsigned count = packed - offset > ED_MAX_CHUNK ? ED_MAX_CHUNK : packed - offset;
        unsigned n = editor_reply_begin(ED_SNAPSHOT_CHUNK, seq);
        n = editor_put14(n, (uint16_t)chunk);
        n = editor_put14(n, (uint16_t)total);
        memcpy(editor_wire + n, editor_packed + offset, count); n += count;
        if (chunk + 1u == total) n = editor_put32(n, crc);
        editor_send(n);
    }
    editor_send(editor_reply_begin(ED_SNAPSHOT_END, seq));
}

static ED_SMALL int editor_set_valid(uint8_t scope, uint8_t target, uint16_t field, uint16_t value)
{
    if (scope == ED_GLOBAL) {
        if (field == 0x3001u) return value >= 300u && value <= 3000u;
        if (field == 0x3002u) return value <= 60u;
        if (field == 0x3003u) return value <= 127u;
        return 0;
    }
    if (scope == ED_TELEGRAM) {
        if (field >= 0x3101u && field <= 0x3115u) return value <= 127u;
        if (field == 0x3116u) return value <= 7u;
        if (field == 0x3117u) return value <= 8u;
        return 0;
    }
    if (scope == ED_DILLO) {
        static const uint16_t maximum[] = {
            16000, 2000, 100, 100, 100, 100, 1, 255, 255, 100, 100, 100,
            DILLO_SWEEP_COUNT - 1u,
        };
        unsigned index = (unsigned)field - 0x3201u;
        if (index >= sizeof maximum / sizeof maximum[0]) return 0;
        unsigned minimum = index == 0 ? 20u : (index == 7 || index == 8);
        return value >= minimum && value <= maximum[index];
    }
    if (scope == ED_MIX && target < 4u) {
        unsigned base = 0x3400u + target * 8u;
        return field >= base && field < base + MIX_CONTROLS && value <= 127u;
    }
    if (scope == ED_BAND_727 && target < 8u)
        return field == 0x3500u + target && value <= 100u;
    return 0;
}

static ED_SMALL void editor_apply(struct panel *panel)
{
    uint16_t field = editor_state.staged_field, value = editor_state.staged_value;
    uint8_t scope = editor_state.tx_domain, target = editor_state.staged_target;
    if (scope == ED_GLOBAL) {
        if (field == 0x3001u) panel->sound.tempo = (uint16_t)(value / 10u);
        else if (field == 0x3002u) panel->sound.step_swing = (uint8_t)value;
        else panel->sound.master = (uint8_t)value;
    } else if (scope == ED_TELEGRAM) {
        if (field >= 0x3101u && field <= 0x3112u)
            panel->sound.patch.value[field - 0x3101u] = (uint8_t)value;
        else if (field == 0x3113u) panel->sound.patch.drive = (uint8_t)value;
        else if (field == 0x3114u) panel->sound.patch.value[TG_DELAY] = (uint8_t)value;
        else if (field == 0x3115u) panel->sound.patch.value[TG_REVERB] = (uint8_t)value;
        else if (field == 0x3116u) panel->sound.arp = (uint8_t)value;
        else panel->sound.arp_rate = (uint8_t)value;
    } else if (scope == ED_DILLO) {
        bb_dillo_patch *tone = &panel->dillo_sound.tone;
        if (field == 0x3201u) tone->cutoff_hz = value;
        else if (field == 0x3202u) tone->resonance_x100 = value;
        else if (field <= 0x320au) {
            static const uint8_t offsets[] = {
                __builtin_offsetof(bb_dillo_patch, envelope), __builtin_offsetof(bb_dillo_patch, decay),
                __builtin_offsetof(bb_dillo_patch, accent), __builtin_offsetof(bb_dillo_patch, drive),
                __builtin_offsetof(bb_dillo_patch, wave), __builtin_offsetof(bb_dillo_patch, slide_ms),
                __builtin_offsetof(bb_dillo_patch, attack_ms), __builtin_offsetof(bb_dillo_patch, tracking),
            };
            ((uint8_t *)tone)[offsets[field - 0x3203u]] = (uint8_t)value;
        }
        else if (field == 0x320bu) panel->dillo_sound.decay.accent = (uint8_t)value;
        else if (field == 0x320cu) panel->dillo_sound.decay.amp = (uint8_t)value;
        else panel->dillo_sound.decay.sweep = value;
        panel->dillo_sound_status = KIT_NORMAL;
    } else if (scope == ED_MIX) {
        unsigned control = field - (0x3400u + target * 8u);
        if (target < 3u) panel->mixer.value[target][control] = (uint8_t)value;
        else panel->drums_b.mix[control] = (uint8_t)value;
    } else if (scope == ED_BAND_727) {
        panel->layer_mix[target] = (uint8_t)value;
    }
    conductor_changed(panel);
    editor_state.revision++;
}

static ED_SMALL void editor_hello(uint16_t seq, const uint8_t *payload, unsigned length)
{
    if (!length || payload[0] != ED_PROTOCOL) {
        editor_result(ED_REJECT, seq, ED_NOT_SUPPORTED);
        return;
    }
    static const uint8_t reply[] = {
        ED_PROTOCOL, ED_PRODUCT_BB37, (ELITE_BUILD >> 7) & 0x7fu, ELITE_BUILD & 0x7fu,
        /* "EL82": Elite v0 schema, in the protocol's five seven-bit groups. */
        (0x454c3832u >> 28) & 0x0fu, (0x454c3832u >> 21) & 0x7fu,
        (0x454c3832u >> 14) & 0x7fu, (0x454c3832u >> 7) & 0x7fu, 0x454c3832u & 0x7fu,
        ED_MAX_CHUNK, 0x45u, 0, 64, 0, 0, 0, 1,
    };
    unsigned n = editor_reply_begin(ED_HELLO_REPLY, seq);
    memcpy(editor_wire + n, reply, sizeof reply);
    editor_send(n + sizeof reply);
    editor_state.connected = 1;
}

static ED_SMALL void editor_close_tx(void)
{
    editor_state.tx_open = editor_state.staged_valid = 0;
    memset(&editor_state.tx_id, 0, 3u * sizeof(uint16_t));
}

static ED_SMALL int editor_message(struct panel *panel)
{
    static const uint8_t prefix[] = {0x00, 0x22, 0x24, 0x35, 0x05};
    const uint8_t *message;
    uint32_t length;
    if (!ota_frame_get(&message, &length) || length < 8u ||
        memcmp(message, prefix, sizeof prefix)) return 0;
    uint8_t type = message[5];
    uint16_t seq = editor_read14(message + 6);
    const uint8_t *payload = message + 8;
    unsigned payload_length = length - 8u;
    ota_frame_done();

    if (type == ED_HELLO) editor_hello(seq, payload, payload_length);
    else if (type == ED_CAPABILITIES) {
        unsigned n = editor_reply_begin(ED_CAPABILITIES_REPLY, seq);
        editor_wire[n++] = 1; editor_wire[n++] = 0x45u; editor_send(n);
    } else if (type == ED_PING) editor_send(editor_reply_begin(ED_PONG, seq));
    else if (type == ED_SNAPSHOT_BEGIN) editor_send_snapshot(panel, seq);
    else if (type == ED_TX_BEGIN) {
        if (!editor_state.connected || payload_length < 7u || editor_state.tx_open) {
            editor_result(ED_REJECT, seq, ED_REFUSED);
        } else {
            uint32_t base = editor_read28(payload);
            if (base && base != editor_state.revision) {
                unsigned n = editor_reply_begin(ED_CONFLICT, seq);
                n = editor_put32(n, editor_state.revision); editor_send(n);
            } else {
                editor_state.tx_domain = payload[4] & 0x7fu;
                editor_state.tx_id = editor_read14(payload + 5);
                editor_state.tx_open = 1; editor_state.staged_valid = 0;
                editor_result(ED_ACK, seq, ED_OK);
            }
        }
    } else if (type == ED_SET) {
        if (!editor_state.tx_open || editor_state.staged_valid || payload_length < 6u ||
            payload[0] != editor_state.tx_domain) {
            editor_result(ED_REJECT, seq, ED_REFUSED);
        } else {
            uint16_t field = editor_read14(payload + 2), value = editor_read14(payload + 4);
            if (!editor_set_valid(payload[0], payload[1], field, value))
                editor_result(ED_REJECT, seq, ED_UNKNOWN_FIELD);
            else {
                editor_state.staged_target = payload[1]; editor_state.staged_field = field;
                editor_state.staged_value = value; editor_state.staged_valid = 1;
                editor_result(ED_APPLIED, seq, ED_OK);
            }
        }
    } else if (type == ED_TX_COMMIT) {
        if (!editor_state.tx_open || !editor_state.staged_valid || payload_length < 2u ||
            editor_read14(payload) != editor_state.tx_id) editor_result(ED_REJECT, seq, ED_REFUSED);
        else {
            editor_apply(panel); editor_close_tx(); editor_result(ED_ACK, seq, ED_QUEUED);
        }
    } else if (type == ED_TX_ABORT) {
        editor_close_tx(); editor_result(ED_ACK, seq, ED_OK);
    } else if (type == ED_PLAY || type == ED_STOP) {
        panel->sound.running = type == ED_PLAY;
        editor_state.revision++;
        editor_result(ED_ACK, seq, ED_QUEUED);
    } else if (type == ED_SETTLE) {
        unsigned n = editor_reply_begin(ED_SETTLE_REPLY, seq);
        n = editor_put32(n, editor_state.revision);
        editor_wire[n++] = editor_state.tx_open; editor_send(n);
    } else editor_result(ED_REJECT, seq, ED_NOT_SUPPORTED);
    return 1;
}
