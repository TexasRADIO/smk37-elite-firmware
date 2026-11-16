/* SPDX-License-Identifier: GPL-3.0-only */
/* Temporary LATIN pattern with a full-kit hit on the downbeat and
 * overlapping Hanan and Compás tails throughout the bar. */
static void bench_percussion_fixture(conductor_patterns *patterns)
{
    conductor_drum_part *part = &patterns->drums_b;
    static const drum_pattern percussion = {
        .hits = {0x1111, 0x5555, 0x1111, 0x4445, 0x2223, 0x8889, 0x0101, 0x2425, 0x4445, 0x1111, 0x4041},
        .accents = {0x0101, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    };
    part->pattern = percussion;
    part->enabled = 1;
    part->muted = 0;
    part->percussion_kit = P727_LATIN + 1u;
}
