/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/* 727 sound pool over USB MIDI: the host stages one 4 KiB sector in 256-byte chunks, then commits it
 * with its CRC-32. Only whole sectors inside the three planner windows (percussion_flash_range) are
 * written; a sector in the VM area is refused if its old or new bytes could form the update loader's
 * OTA record tag (tools/plan_727_flash.py guard_loader_records). An unchanged sector is not touched.
 * Reads are limited to the same windows, so the host can keep an exact undo copy first. */
#include <stdint.h>
#include <string.h>
#include "percussion_flash.h"

enum {
    P727_LOAD_SECTOR = 4096u, P727_LOAD_CHUNK = 256u, P727_LOAD_CHUNKS = 16u,
    P727_LOAD_TAG_OFFSET = 0xF06u, P727_LOAD_TAG = 0x5441u,
};
enum { P727_OK, P727_SAME, P727_RANGE, P727_INCOMPLETE, P727_CRC, P727_BUSY, P727_GUARD,
       P727_ERASE, P727_PROGRAM, P727_VERIFY, P727_BAD };

typedef struct {
    void *context;
    int (*read)(void *, uint32_t offset, uint8_t *out, uint32_t n);
    int (*erase)(void *, uint32_t offset);   /* one 4 KiB sector */
    int (*program)(void *, uint32_t offset, const uint8_t *data, uint32_t n);   /* one 256-byte page */
} p727_io;

typedef struct {
    uint8_t data[P727_LOAD_SECTOR];
    uint32_t sector;     /* flash offset of the staged sector */
    uint16_t staged;     /* one bit per chunk received */
} p727_stage;

static inline int p727_window(uint32_t offset, uint32_t n)
{
    return n && percussion_flash_range(offset, n);
}

/* The VM windows sit below the OTA staging area; USRFLASH (FL_SMP) is above it. */
static inline int p727_vm(uint32_t sector) { return sector < FL_OTA_LO; }

static inline int p727_tag_safe(const uint8_t *sector_bytes)
{
    unsigned tag = sector_bytes[P727_LOAD_TAG_OFFSET] | (unsigned)sector_bytes[P727_LOAD_TAG_OFFSET + 1u] << 8;
    return (tag & ~(unsigned)P727_LOAD_TAG & 0xffffu) != 0;
}

static inline int p727_read(const p727_io *io, uint32_t offset, uint8_t *out, uint32_t n)
{
    if (!p727_window(offset, n) || n > P727_LOAD_CHUNK) return P727_RANGE;
    return io->read(io->context, offset, out, n) ? P727_BAD : P727_OK;
}

static inline int p727_stage_chunk(p727_stage *stage, uint32_t sector, unsigned chunk, const uint8_t *data)
{
    if (sector % P727_LOAD_SECTOR || !p727_window(sector, P727_LOAD_SECTOR) || chunk >= P727_LOAD_CHUNKS)
        return P727_RANGE;
    if (stage->sector != sector) {
        stage->sector = sector;
        stage->staged = 0;
    }
    memcpy(stage->data + chunk * P727_LOAD_CHUNK, data, P727_LOAD_CHUNK);
    stage->staged |= (uint16_t)(1u << chunk);
    return P727_OK;
}

static inline uint32_t p727_crc(const uint8_t *data, uint32_t n)
{
    stub_crc_t crc;
    stub_crc32_init(&crc);
    return stub_crc32_bytes(&crc, data, n);
}

/* busy: the caller's own refusal (transport running, safe mode). */
static inline int p727_commit(p727_stage *stage, const p727_io *io, uint32_t sector, uint32_t crc, int busy)
{
    uint8_t page[P727_LOAD_CHUNK];
    if (sector % P727_LOAD_SECTOR || !p727_window(sector, P727_LOAD_SECTOR)) return P727_RANGE;
    if (stage->sector != sector || stage->staged != 0xffffu) return P727_INCOMPLETE;
    if (p727_crc(stage->data, P727_LOAD_SECTOR) != crc) return P727_CRC;
    if (busy) return P727_BUSY;
    if (p727_vm(sector)) {
        uint8_t old[2];
        if (io->read(io->context, sector + P727_LOAD_TAG_OFFSET, old, 2)) return P727_BAD;
        unsigned tag = old[0] | (unsigned)old[1] << 8;
        if (!p727_tag_safe(stage->data) || !(tag & ~(unsigned)P727_LOAD_TAG & 0xffffu)) return P727_GUARD;
    }
    int same = 1;
    for (uint32_t at = 0; same && at < P727_LOAD_SECTOR; at += P727_LOAD_CHUNK) {
        if (io->read(io->context, sector + at, page, P727_LOAD_CHUNK)) return P727_BAD;
        same = !memcmp(page, stage->data + at, P727_LOAD_CHUNK);
    }
    if (same) {
        stage->staged = 0;
        return P727_SAME;
    }
    if (io->erase(io->context, sector)) return P727_ERASE;
    for (uint32_t at = 0; at < P727_LOAD_SECTOR; at += P727_LOAD_CHUNK)
        if (io->program(io->context, sector + at, stage->data + at, P727_LOAD_CHUNK)) return P727_PROGRAM;
    for (uint32_t at = 0; at < P727_LOAD_SECTOR; at += P727_LOAD_CHUNK) {
        if (io->read(io->context, sector + at, page, P727_LOAD_CHUNK)) return P727_BAD;
        if (memcmp(page, stage->data + at, P727_LOAD_CHUNK)) return P727_VERIFY;
    }
    stage->staged = 0;
    return P727_OK;
}

/* MIDI 8-in-7 packing: each group of up to seven bytes is led by one byte carrying their top bits. */
static inline uint32_t p727_unpack7(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t room)
{
    uint32_t written = 0;
    while (n) {
        uint8_t high = *in++;
        n--;
        for (unsigned bit = 0; bit < 7 && n; bit++, n--) {
            if (written >= room || (*in & 0x80u)) return 0;
            out[written++] = (uint8_t)(*in++ | ((high >> bit) & 1u) << 7);
        }
    }
    return written;
}

static inline uint32_t p727_pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t written = 0;
    for (uint32_t at = 0; at < n; at += 7) {
        uint8_t high = 0;
        uint32_t lead = written++;
        for (unsigned bit = 0; bit < 7 && at + bit < n; bit++) {
            high |= (uint8_t)((in[at + bit] >> 7) << bit);
            out[written++] = in[at + bit] & 0x7fu;
        }
        out[lead] = high;
    }
    return written;
}

static inline uint32_t p727_get21(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 7 | (uint32_t)p[2] << 14; }
static inline uint32_t p727_get35(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 7 | (uint32_t)p[2] << 14 | (uint32_t)p[3] << 21 | (uint32_t)p[4] << 28;
}
static inline void p727_put21(uint8_t *p, uint32_t v)
{
    p[0] = v & 127u; p[1] = (v >> 7) & 127u; p[2] = (v >> 14) & 127u;
}
