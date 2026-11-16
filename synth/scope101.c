#include "scope101.h"

void tg_scope_write(tg_scope *s, const int32_t *stereo, unsigned frames)
{
    unsigned head = s->head;
    for (unsigned i = 0; i < frames; i++) {
        int32_t v = stereo[2u * i] >> 8;
        if (!s->half) { s->odd = v; s->half = 1; continue; }
        s->half = 0;
        v = (s->odd + v) / 2;
        s->ring[head] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        head = (head + 1u) & (TG_SCOPE_RING - 1u);
    }
    s->head = (uint16_t)head;
}

uint32_t tg_scope_period16(unsigned note)
{
    /* 16 * 44100 / f for C-1..B-1 (8.18-15.43 Hz); each octave up halves it. */
    static const uint32_t octave0[12] = {86304, 81460, 76888, 72572, 68499, 64654,
                                         61025, 57600, 54367, 51316, 48436, 45717};
    if (!note || note > 127u) return 0;
    return octave0[note % 12u] >> (note / 12u);
}

static int16_t at(const tg_scope *s, unsigned head, unsigned back)
{
    return s->ring[(head - back) & (TG_SCOPE_RING - 1u)];
}

unsigned tg_scope_trace(const tg_scope *s, uint32_t period16, int8_t *y, unsigned columns, unsigned half)
{
    unsigned head = s->head;
    /* In ring entries (two samples each): three periods, at least 32 entries. */
    uint32_t period = period16 / (16u * TG_SCOPE_DECIMATE);
    uint32_t window = period16 ? period16 * TG_SCOPE_PERIODS / (16u * TG_SCOPE_DECIMATE) : 256u;
    enum { NEWEST = 2u, LIMIT = TG_SCOPE_RING - TG_SCOPE_BLOCK_GUARD - NEWEST - 2u };
    if (window < 32u) window = 32u;
    if (window > LIMIT * 3u / 4u) window = LIMIT * 3u / 4u;
    /* The latest rising crossing that leaves a full window after it, looked for over one period
     * (or 128 entries) further back; with none, the window ends at the newest entry. */
    unsigned search = period16 ? period + 1u : 128u;
    if (search > LIMIT - window) search = LIMIT - window;
    unsigned newest = NEWEST;
    unsigned start = newest + window; /* entries back from head to the window's first */
    for (unsigned back = newest + window; back < newest + window + search; back++)
        if (at(s, head, back + 1u) < 0 && at(s, head, back) >= 0) { start = back; break; }
    int peak = 0;
    for (unsigned i = 0; i < window; i++) {
        int v = at(s, head, start - i);
        if (v < 0) v = -v;
        if (v > peak) peak = v;
    }
    if (peak < 96) { /* about -50 dBFS: call it silence */
        for (unsigned c = 0; c < columns; c++) y[c] = 0;
        return 0;
    }
    /* Full scale fills the plot; quieter sounds get up to 4x, never past the edge. */
    int scale = peak > 32768 / TG_SCOPE_MAX_GAIN ? peak : 32768 / TG_SCOPE_MAX_GAIN;
    for (unsigned c = 0; c < columns; c++) {
        unsigned i = columns > 1u ? c * (window - 1u) / (columns - 1u) : 0u;
        int v = at(s, head, start - i);
        int32_t px = (int32_t)v * (int32_t)half / scale;
        if (px > (int32_t)half) px = (int32_t)half;
        if (px < -(int32_t)half) px = -(int32_t)half;
        y[c] = (int8_t)px;
    }
    return 1;
}
