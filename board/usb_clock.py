# SPDX-License-Identifier: GPL-3.0-only
"""Checked app-only clock routing; preserve the pinned vendor and boot loader."""
def stage(source):
    before = '''    if (cin == 0xFu && (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu)) {
        midi_enqueue(0xFu | st << 8, 1u);
        return;
    }'''
    after = '''    if (cin == 0xFu && (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu)) {
        if ((pkt & 255u) == 0xFu && !(pkt & 0xffff0000u)) midi_enqueue(pkt, 1u);
        return;
    }
    if (cin == 3u && st == 0xF2u) {
        if ((pkt & 255u) == 3u && !(pkt & 0x80800000u)) midi_enqueue(pkt, 1u);
        return;
    }'''
    if source.count(before) != 1:
        raise ValueError('Unexpected retained USB real-time parser; clock hook not applied')
    return source.replace(before, after)
