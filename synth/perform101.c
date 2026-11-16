/* SPDX-License-Identifier: GPL-3.0-only */
#include "perform101.h"
#include "../vendor/bb/x0x/fastmath.h" /* the board is freestanding: no libm */
#include <string.h>

enum { RATE = 44100, ENTRY_RATE = RATE / 2, TAPE_MAX = 30000 /* lags the ring by 7500 entries at most */ };
static const float LPF_OPEN = 18000.0f, LPF_SHUT = 300.0f, HPF_OPEN = 20.0f, HPF_SHUT = 1500.0f;

void tg_perform_init(tg_perform *p, int16_t *ring)
{
    memset(p, 0, sizeof *p);
    p->ring = ring;
    memset(ring, 0, TG_PERF_RING * sizeof *ring);
    p->mode = TG_PF_NONE;
    p->cutoff[0] = LPF_OPEN;
    p->cutoff[1] = HPF_OPEN;
    for (unsigned i = 0; i < TG_PF_LANES; i++) p->lane_gain[i] = TG_PERF_FADE;
}

static int32_t ring_at(const tg_perform *p, uint32_t entry)
{
    return p->ring[entry & (TG_PERF_RING - 1u)];
}

/* Q16 position within the slice, wrapping inside it so a loop's seam reads its own start. */
static int32_t loop_read(const tg_perform *p, uint32_t pos)
{
    uint32_t i = pos >> 16, frac = pos & 0xffffu;
    uint32_t next = i + 1u < p->length ? i + 1u : 0u;
    int32_t a = ring_at(p, p->anchor + i), b = ring_at(p, p->anchor + next);
    return a + (int32_t)(((int64_t)(b - a) * frac) >> 16);
}

static void start_mode(tg_perform *p, unsigned mode, unsigned tempo)
{
    p->mode = (uint8_t)mode;
    if (mode == TG_PF_TAPE) {
        p->anchor = p->head;
        p->pos = 0;
        p->speed = 1u << 15;
        uint32_t beat = (uint32_t)RATE * 60u / (tempo ? tempo : 120u);
        p->tape_total = p->tape_left = beat < TAPE_MAX ? beat : TAPE_MAX;
    } else {
        p->length = tg_perform_slice(mode, tempo);
        p->anchor = (uint32_t)p->head - p->length;
        p->pos = mode == TG_PF_REV ? (p->length << 16) - (1u << 15) : 0u;
    }
}

static void filter_block(tg_perform *p, unsigned k, unsigned held, unsigned frames, unsigned tempo)
{
    float open = k ? HPF_OPEN : LPF_OPEN, shut = k ? HPF_SHUT : LPF_SHUT;
    if (held && !p->filter_on[k]) p->filter_age[k] = 0;
    p->filter_on[k] = (uint8_t)held;
    if (held) {
        float bar = 4.0f * 60.0f * RATE / (float)(tempo ? tempo : 120u);
        float t = (float)p->filter_age[k] / bar;
        if (t > 1.0f) t = 1.0f;
        p->cutoff[k] = open * fm_powf(shut / open, t);
        p->filter_age[k] += frames;
    } else if (p->cutoff[k] != open) {
        /* Let go: back open in about 90 ms, then out of the way. */
        float step = fm_powf(open / shut, (float)frames / (0.09f * RATE));
        p->cutoff[k] *= step; /* up toward the LPF's open end, down toward the HPF's */
        if (k ? p->cutoff[k] <= open * 1.05f : p->cutoff[k] >= open * 0.95f) {
            p->cutoff[k] = open;
            memset(p->ic1[k], 0, sizeof p->ic1[k]);
            memset(p->ic2[k], 0, sizeof p->ic2[k]);
        }
    }
}

void tg_perform_render(tg_perform *p, int32_t *st, unsigned frames, unsigned tempo, unsigned buffer_fx,
                       unsigned filters)
{
    unsigned target = buffer_fx < TG_PF_LPF && (buffer_fx == TG_PF_TAPE || tg_perform_slice(buffer_fx, tempo)) ?
                      buffer_fx : TG_PF_NONE;
    if (target != p->mode && !p->wet) {
        if (target == TG_PF_NONE) p->mode = TG_PF_NONE;
        else start_mode(p, target, tempo);
    }
    filter_block(p, 0, filters & 1u, frames, tempo);
    filter_block(p, 1, (filters >> 1) & 1u, frames, tempo);
    unsigned filtering[2] = {p->filter_on[0] || p->cutoff[0] != LPF_OPEN, p->filter_on[1] || p->cutoff[1] != HPF_OPEN};
    if (p->mode == TG_PF_NONE && !p->wet && !filtering[0] && !filtering[1]) {
        /* Idle: only record. */
        for (unsigned i = 0; i < frames; i++) {
            int32_t m = (st[2u * i] + st[2u * i + 1u]) / 2 >> 8;
            if (!p->half) { p->odd = m; p->half = 1; continue; }
            p->half = 0;
            int32_t v = (p->odd + m) / 2;
            p->ring[p->head] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            p->head = (uint16_t)((p->head + 1u) & (TG_PERF_RING - 1u));
        }
        return;
    }
    float a1[2] = {0}, a2[2] = {0}, a3[2] = {0};
    const float k_damp = 0.625f; /* Q 1.6: a little bite as it closes */
    for (unsigned k = 0; k < 2; k++) if (filtering[k]) {
        float w = FM_PI * p->cutoff[k] / RATE; /* below 1.3: sin and cos both in fm_sin_pi's range */
        float g = fm_sin_pi(w) / fm_sin_pi(w + 0.5f * FM_PI);
        a1[k] = 1.0f / (1.0f + g * (g + k_damp));
        a2[k] = g * a1[k];
        a3[k] = g * a2[k];
    }
    unsigned recording = p->mode == TG_PF_NONE || p->mode == TG_PF_TAPE;
    for (unsigned i = 0; i < frames; i++) {
        int32_t l = st[2u * i], r = st[2u * i + 1u];
        if (recording) {
            int32_t m = (l + r) / 2 >> 8;
            if (!p->half) { p->odd = m; p->half = 1; }
            else {
                p->half = 0;
                int32_t v = (p->odd + m) / 2;
                p->ring[p->head] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
                p->head = (uint16_t)((p->head + 1u) & (TG_PERF_RING - 1u));
            }
        }
        if (p->mode != TG_PF_NONE) {
            int32_t s;
            if (p->mode == TG_PF_TAPE) {
                uint32_t idx = p->pos >> 16, frac = p->pos & 0xffffu;
                int32_t a = ring_at(p, p->anchor + idx), b = ring_at(p, p->anchor + idx + 1u);
                s = a + (int32_t)(((int64_t)(b - a) * frac) >> 16);
                s = (int32_t)(((int64_t)s * p->speed) >> 15); /* the level falls with the speed, as tape does */
                p->pos += p->speed;
                if (p->tape_left) p->tape_left--;
                p->speed = (uint32_t)(((uint64_t)(1u << 15) * p->tape_left) / p->tape_total);
            } else {
                s = loop_read(p, p->pos);
                uint32_t end = p->length << 16;
                if (p->mode == TG_PF_REV) p->pos = p->pos >= (1u << 15) ? p->pos - (1u << 15) : p->pos + end - (1u << 15);
                else { p->pos += 1u << 15; if (p->pos >= end) p->pos -= end; }
            }
            int32_t wet_s = s * 256;
            unsigned w = p->wet;
            l = (int32_t)(((int64_t)l * (TG_PERF_FADE - w) + (int64_t)wet_s * w) / TG_PERF_FADE);
            r = (int32_t)(((int64_t)r * (TG_PERF_FADE - w) + (int64_t)wet_s * w) / TG_PERF_FADE);
            if (p->mode == target) { if (p->wet < TG_PERF_FADE) p->wet++; }
            else if (p->wet) p->wet--;
        }
        for (unsigned k = 0; k < 2; k++) if (filtering[k]) {
            float x[2] = {(float)l, (float)r};
            for (unsigned c = 0; c < 2; c++) {
                float v3 = x[c] - p->ic2[k][c];
                float v1 = a1[k] * p->ic1[k][c] + a2[k] * v3;
                float v2 = p->ic2[k][c] + a2[k] * p->ic1[k][c] + a3[k] * v3;
                p->ic1[k][c] = 2.0f * v1 - p->ic1[k][c];
                p->ic2[k][c] = 2.0f * v2 - p->ic2[k][c];
                x[c] = k ? x[c] - k_damp * v1 - v2 : v2;
                if (!(x[c] == x[c]) || p->ic1[k][c] != p->ic1[k][c] || p->ic2[k][c] != p->ic2[k][c]) {
                    /* Never send a non-finite value to the DAC: drop this sample and restart the filter. */
                    x[c] = 0.0f;
                    p->ic1[k][c] = p->ic2[k][c] = 0.0f;
                }
            }
            l = (int32_t)(x[0] > 8388607.0f ? 8388607.0f : x[0] < -8388608.0f ? -8388608.0f : x[0]);
            r = (int32_t)(x[1] > 8388607.0f ? 8388607.0f : x[1] < -8388608.0f ? -8388608.0f : x[1]);
        }
        st[2u * i] = l;
        st[2u * i + 1u] = r;
    }
    if (!p->wet && p->mode != target && target == TG_PF_NONE) p->mode = TG_PF_NONE;
}

void tg_perform_lane(tg_perform *p, unsigned lane, int32_t *buffer, unsigned frames, unsigned stride, unsigned muted)
{
    if (lane >= TG_PF_LANES || !buffer) return;
    uint16_t gain = p->lane_gain[lane], goal = muted ? 0 : TG_PERF_FADE;
    if (gain == goal && gain == TG_PERF_FADE) return;
    for (unsigned i = 0; i < frames; i++) {
        if (gain < goal) gain++;
        else if (gain > goal) gain--;
        for (unsigned c = 0; c < stride; c++)
            buffer[i * stride + c] = (int32_t)(((int64_t)buffer[i * stride + c] * gain) / TG_PERF_FADE);
    }
    p->lane_gain[lane] = gain;
}

unsigned tg_perform_busy(const tg_perform *p)
{
    return p->mode != TG_PF_NONE || p->wet || p->filter_on[0] || p->filter_on[1] ||
           p->cutoff[0] != LPF_OPEN || p->cutoff[1] != HPF_OPEN;
}
