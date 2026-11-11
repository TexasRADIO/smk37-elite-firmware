/* SPDX-License-Identifier: GPL-3.0-only */
#include "fx101.h"

static int32_t limit(int32_t value, int32_t ceiling)
{
    return value < -ceiling - 1 ? -ceiling - 1 : value > ceiling ? ceiling : value;
}

void tg_fx_clear(tg_fx *fx)
{
    fx->delay_age = 0;
    for (unsigned i = 0; i < 4; i++) { fx->room_age[i] = 0; fx->damp[i] = 0; }
}

/* Linked instantaneous soft knee. Quiet samples pass through exactly.
 * The table samples knee + excess * headroom / (headroom + excess)
 * in 1/8-full-scale input intervals. Four Q23 lanes plus the largest
 * possible wet return sum are bounded by 42 << 20. */
static inline void conductor_headroom(int32_t *stereo)
{
    enum { KNEE = 6 << 20, SHIFT = 20, FRACTION_BITS = 10, LAST_BIN = 36 };
    static const uint32_t shape[] = {
        6291456, 6990507, 7340032, 7549747, 7689557, 7789422,
        7864320, 7922574, 7969178, 8007308, 8039083, 8065969,
        8089015, 8108988, 8126464, 8141884, 8155591, 8167855,
        8178893, 8188879, 8197958, 8206247, 8213845, 8220836,
        8227289, 8233263, 8238811, 8243977, 8248798, 8253308,
        8257536, 8261508, 8265246, 8268771, 8272100, 8275248,
        8278232, 8281062
    };
    int32_t left = stereo[0], right = stereo[1];
    unsigned a = (unsigned)(left < 0 ? -left : left);
    unsigned b = (unsigned)(right < 0 ? -right : right);
    unsigned peak = a > b ? a : b;
    if (peak <= KNEE) return;
    unsigned offset = peak - KNEE;
    unsigned index = offset >> SHIFT;
    if (index > LAST_BIN) index = LAST_BIN;
    unsigned fraction = (offset >> (SHIFT - FRACTION_BITS)) & ((1u << FRACTION_BITS) - 1u);
    int32_t target = (int32_t)(shape[index] + ((shape[index + 1] - shape[index]) * fraction >> FRACTION_BITS));
    if (a == b) {
        stereo[0] = left < 0 ? -target : target;
        stereo[1] = right < 0 ? -target : target;
        return;
    }
    /* Mono needs no divide. For unequal stereo, preserve the quieter
     * channel's ratio with a shared Q14 gain and 32-bit products. */
    unsigned loud = b > a;
    int32_t quiet = stereo[loud ^ 1u];
    stereo[loud] = stereo[loud] < 0 ? -target : target;
    if (quiet) {
        unsigned divisor = peak >> 9;
        int32_t scale = (int32_t)((((unsigned)target >> 9) * 16384u + divisor / 2u) / divisor);
        int32_t coarse = tg_fx_trunc_shift(quiet, 9), fine = quiet - coarse * 512;
        stereo[loud ^ 1u] = tg_fx_trunc_shift(coarse * scale + tg_fx_trunc_shift(fine * scale, 9), 5);
    }
}

/* Keep the shared sample body in the render loops. PI32 otherwise saves
 * and restores eleven registers on every call. */
static const uint16_t room_length[] = {1223, 1429, 1619, 1879};

static inline __attribute__((always_inline)) void mix_sample(tg_fx *fx, int32_t *stereo,
                                                            int32_t delay_input, int32_t room_input,
                                                            unsigned master, unsigned full)
{
    unsigned pos = fx->delay_pos;
    int32_t echo_l = full || fx->delay_age >= 11025u ? fx->delay[(pos - 11025u) & (TG_DELAY_SIZE - 1u)] : 0;
    int32_t echo_r = full || fx->delay_age >= 14700u ? fx->delay[(pos - 14700u) & (TG_DELAY_SIZE - 1u)] : 0;
    fx->delay[pos] = (int16_t)limit(delay_input + tg_fx_trunc_shift(echo_r * 3, 3), 32767);
    fx->delay_pos = (uint16_t)((pos + 1u) & (TG_DELAY_SIZE - 1u));
    if (!full && fx->delay_age < TG_DELAY_SIZE) fx->delay_age++;
    int32_t room[2] = {0, 0};
    for (unsigned j = 0; j < 4; j++) {
        unsigned tap = fx->room_pos[j];
        int32_t delayed = full || fx->room_age[j] >= room_length[j] ? fx->room[j][tap] : 0;
        fx->damp[j] += tg_fx_trunc_shift(delayed - fx->damp[j], 2);
        fx->room[j][tap] = (int16_t)limit(room_input + tg_fx_trunc_shift(fx->damp[j] * 11, 4), 32767);
        fx->room_pos[j] = (uint16_t)(tap + 1u == room_length[j] ? 0 : tap + 1u);
        if (!full && fx->room_age[j] < room_length[j]) fx->room_age[j]++;
        room[j & 1u] += delayed;
    }
    stereo[0] = master ? stereo[0] + (echo_l * 3 + room[0]) * 64 : 0;
    stereo[1] = master ? stereo[1] + (echo_r * 3 + room[1]) * 64 : 0;
}

void tg_fx_render(tg_fx *fx, int32_t *stereo, unsigned frames,
                  unsigned delay_send, unsigned room_send, unsigned master)
{
    for (unsigned i = 0; i < frames; i++) {
        int32_t input = tg_fx_trunc_shift(stereo[i * 2u], 8);
        int32_t delay_delta = (int32_t)(delay_send * 16u) - fx->delay_send;
        int32_t room_delta = (int32_t)(room_send * 16u) - fx->room_send;
        fx->delay_send = (uint16_t)(fx->delay_send + (delay_delta > 0 ? 1 : delay_delta < 0 ? -1 : 0));
        fx->room_send = (uint16_t)(fx->room_send + (room_delta > 0 ? 1 : room_delta < 0 ? -1 : 0));
        /* The standalone synth supplies the same dry signal to both sides. */
        stereo[i * 2u + 1u] = stereo[i * 2u];
        mix_sample(fx, stereo + i * 2u, tg_fx_trunc_shift(input * fx->delay_send, 11),
                   tg_fx_trunc_shift(input * fx->room_send, 12), master, 0);
        stereo[i * 2u] = limit(stereo[i * 2u], 8388607);
        stereo[i * 2u + 1u] = limit(stereo[i * 2u + 1u], 8388607);
    }
}

static int bus_has_input(const int32_t *delay, const int32_t *room, unsigned frames)
{
    for (unsigned i = 0; i < frames; i++)
        if (delay[i] || room[i]) return 1;
    return 0;
}

static inline __attribute__((always_inline)) void render_wet_bus(
    tg_fx *fx, int32_t *stereo, const int32_t *delay, const int32_t *room,
    unsigned frames, unsigned master, unsigned full)
{
    for (unsigned i = 0; i < frames; i++) {
        mix_sample(fx, stereo + i * 2u, delay[i], room[i], master, full);
        conductor_headroom(stereo + i * 2u);
    }
}

void tg_fx_render_bus(tg_fx *fx, int32_t *stereo, const int32_t *delay, const int32_t *room,
                      unsigned frames, unsigned master)
{
    /* Ages are zero together at startup or after clear. With neither a
     * history nor an input, no tap can contribute. Leave the empty rings
     * alone so the first send still starts with zero history. */
    if (!fx->delay_age && !fx->room_age[0] && !bus_has_input(delay, room, frames)) {
        for (unsigned i = 0; i < frames; i++) {
            if (!master) stereo[2u * i] = stereo[2u * i + 1u] = 0;
            conductor_headroom(stereo + 2u * i);
        }
        return;
    }
    /* Age counters saturate until clear. In that steady state every tap
     * is readable and the audio loop only needs to advance the positions. */
    if (fx->delay_age == TG_DELAY_SIZE && fx->room_age[0] == room_length[0] &&
        fx->room_age[1] == room_length[1] && fx->room_age[2] == room_length[2] &&
        fx->room_age[3] == room_length[3])
        render_wet_bus(fx, stereo, delay, room, frames, master, 1);
    else
        render_wet_bus(fx, stereo, delay, room, frames, master, 0);
}
