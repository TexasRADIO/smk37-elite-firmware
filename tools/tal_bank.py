#!/usr/bin/env python3
"""Package calibrated local audition drafts for the optional Elite library.

No third-party patch data is copied into tracked sources. The generated manifest
retains original names, source hashes, adaptations and measured limitations.
"""
import argparse
import hashlib
import json
import math
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VALUES = ('SAW PULSE SUB NOISE CUTOFF RESONANCE ENV TRACK ATTACK DECAY SUSTAIN RELEASE WIDTH PWM LFO_RATE FILTER_LFO GLIDE VIBRATO DELAY REVERB BEND_PITCH BEND_FILTER MOD_PITCH MOD_FILTER').split()
MODES = {'trigger': ['TG_GATE_TRIG', 'TG_GATE', 'TG_LFO_TRIG'],
         'pwm': ['TG_PWM_MANUAL', 'TG_PWM_LFO', 'TG_PWM_ENV'],
         'sub_mode': [0, 1, 2], 'amp_gate': [0, 1],
         'glide_mode': ['TG_GLIDE_OFF', 'TG_GLIDE_AUTO', 'TG_GLIDE_ON'],
         'range': ['TG_RANGE_16', 'TG_RANGE_8', 'TG_RANGE_4', 'TG_RANGE_2'],
         'lfo_wave': ['TG_LFO_TRI', 'TG_LFO_SQUARE', 'TG_LFO_RANDOM', 'TG_LFO_NOISE'],
         'osc_octave': [-2,-1,0,1,2], 'key_octave': [-2,-1,0,1,2], 'env_wide': [0,1], 'filter_low': [0,1], 'lfo_retrigger': [0,1], 'filter_idle_hold': [0,1]}
CATEGORIES = ('ALL', 'BASS', 'LEAD', 'KEYS', 'PADS', 'PERC', 'FX', 'SEQ/ARP', 'BRASS', 'WINDS', 'OTHER', 'PLAY FIRST', 'FM1 RECIPES')
FOLDERS = {'bass': 'BASS', 'lead': 'LEAD', 'leads': 'LEAD', 'keys': 'KEYS', 'piano': 'KEYS',
           'pads': 'PADS', 'chords': 'PADS', 'drums': 'PERC', 'percussion': 'PERC',
           'percusion': 'PERC', 'fx': 'FX', 'sfx': 'FX', 'sequence': 'SEQ/ARP',
           'sequencer': 'SEQ/ARP', 'seq': 'SEQ/ARP', 'arp': 'SEQ/ARP', 'arpeggiator': 'SEQ/ARP'}
ROLE_PREFIX = {'bs': 'BASS', 'ld': 'LEAD', 'ky': 'KEYS', 'pd': 'PADS', 'br': 'BRASS'}
ROLE_WORDS = {
    'BASS': {'bass', 'bassline'},
    'LEAD': {'lead', 'leads'},
    'KEYS': {'keys', 'piano', 'organ', 'clavinet', 'accordion', 'vibraphone', 'harpsichord'},
    'PADS': {'pad', 'pads', 'strings', 'chords', 'violin'},
    'PERC': {'kick', 'kickdrum', 'snare', 'snaredrum', 'tom', 'hihat', 'rimshot',
             'drum', 'drums', 'drumloop', 'clap', 'bongo', 'claves', 'glockenspiel', 'gong', 'xylophone'},
    'FX': {'fx', 'sfx', 'effect'},
    'SEQ/ARP': {'arp', 'arpeggio', 'arpeggiator', 'seq', 'sequence', 'sequencer'},
    'BRASS': {'brass', 'trumpet', 'trombone', 'tuba', 'horn'},
    'WINDS': {'clarinet', 'oboe', 'flute', 'bassoon', 'saxophone', 'whistle', 'panpipes'},
}


def musical_category(record):
    raw = record.get('category', '')
    if not isinstance(raw, str):
        raise ValueError('invalid source category')
    leaf = re.split(r'[/\\]', raw)[-1].strip().casefold()
    folder = FOLDERS.get(leaf)
    parts = re.split(r'[/\\]', record.get('member', ''))
    member = parts[-1]
    if not folder and len(parts) > 1:
        folder = FOLDERS.get(parts[-2].strip().casefold())
    prefix = re.split(r'[\W_]+', member.casefold(), maxsplit=1)[0]
    if prefix in ROLE_PREFIX:
        return CATEGORIES.index(ROLE_PREFIX[prefix])

    # Author folders and generic layers carry no instrument role. Explicit
    # instrument names can refine those, while mixed names defer to the folder.
    words = {re.sub(r'\d+$', '', word) for word in re.split(r'[\W_]+', record['name'].casefold())}
    roles = [role for role, terms in ROLE_WORDS.items() if words & terms]
    if len(roles) == 1 and (not folder or roles[0] in ('BRASS', 'WINDS')):
        return CATEGORIES.index(roles[0])
    if folder:
        return CATEGORIES.index(folder)
    if not roles:
        if 'pan' in words and 'pipes' in words:
            return CATEGORIES.index('WINDS')
        # Studio's synthPresets entries include source phrases. TAL states
        # also contain a default sequence, so require varied active steps.
        sequence = record.get('source_sequence') or {}
        count = int(sequence.get('settings', {}).get('numberofsteps', 0))
        steps = sequence.get('steps', [])[:count]
        varied = len({(step.get('noteoffset', 0), step.get('pause', 0), step.get('slide', 0))
                      for step in steps}) > 1
        if member == 'synthPresets.ts' or varied:
            return CATEGORIES.index('SEQ/ARP')
    return CATEGORIES.index('OTHER')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def native_sources():
    return {name: sha(ROOT / name) for name in ('synth/telegram101.c', 'synth/telegram101.h', 'synth/filter101.inc', 'synth/clock101.h')}


def validate_controls(r):
    if not isinstance(r.get('name'), str) or not 1 <= len(r['name']) <= 200 or any(ord(c) < 32 for c in r['name']):
        raise ValueError('invalid patch name')
    values = r['values']
    if set(values) != {'TG_' + k for k in VALUES} or any(type(v) is not int or not 0 <= v <= 127 for v in values.values()):
        raise ValueError('invalid patch controls')
    # Older private banks omit the optional range flags and retain original ranges.
    if not set(MODES)-{'env_wide','filter_low','lfo_retrigger','filter_idle_hold'} <= set(r['modes']) <= set(MODES) or any(r['modes'].get(k,0) not in allowed for k, allowed in MODES.items()):
        raise ValueError('invalid patch mode')
    return values


def validate_record(r):
    values = validate_controls(r)
    h = r.get('source_sha256', '')
    if len(h) != 64 or any(c not in '0123456789abcdef' for c in h):
        raise ValueError('invalid source identity')
    m = r['calibration_measurement']
    if m['name'] != r['name'] or m['source_sha256'] != h or m['candidate_resonance'] != values['TG_RESONANCE']:
        raise ValueError('measurement identity mismatch')
    cases = m['cases']
    if len(cases) != 6 or {(c['note'], c['warm']) for c in cases} != {(n, w) for n in (36, 48, 60) for w in (0, 1)}:
        raise ValueError('incomplete audibility coverage')
    if any(not math.isfinite(c['peak_dbfs']) or not -200 <= c['peak_dbfs'] <= 0 for c in cases):
        raise ValueError('invalid audio measurement')
    if any(c['resonance'] != values['TG_RESONANCE'] or c['draft_index'] != m['draft_index'] for c in cases):
        raise ValueError('measurement case mismatch')
    # Earlier measurements omit onset. Keep their original peak-only flag.
    onsets = [c.get('onset_frames', 0) for c in cases]
    if any(type(onset) is not int or not -1 <= onset < 44100 for onset in onsets):
        raise ValueError('invalid onset measurement')
    quiet = min(c['peak_dbfs'] for c in cases) < -60 or any(
        not 0 <= onset <= 33075 for onset in onsets)  # A three-quarter-second audition.
    return (1 if quiet else 0) | (2 if m['original_resonance'] != m['candidate_resonance'] else 0) | (4 if r['feature_gaps'] else 0)


def studio_records(draft_path, audit_path):
    drafts = json.loads(draft_path.read_text())
    audit = json.loads(audit_path.read_text())
    if (drafts.get('kind') != 'private-studio-audition' or
            drafts.get('native_sources') != native_sources() or
            audit.get('drafts_sha256') != sha(draft_path)):
        raise ValueError('stale or unrecognized Studio audition data')
    records = drafts['records']
    measured = audit['results']
    ids = [r['id'] for r in records]
    if len(ids) != len(set(ids)) or len(measured) != len(records):
        raise ValueError('Studio audition coverage mismatch')
    selected = []
    for draft, result in zip(records, measured):
        draft = {**draft, 'modes': {'key_octave': 0, **draft['modes']}}
        validate_controls(draft)
        if (draft['id'], draft['name']) != (result['id'], result['name']):
            raise ValueError('Studio audition identity mismatch')
        peak = result['worst_peak_dbfs']
        if not math.isfinite(peak) or not -200 <= peak <= 0:
            raise ValueError('invalid Studio measurement')
        if peak >= -60:
            selected.append({**draft, 'category': draft['source_category']})
    return selected


def fm1_records(path):
    bank = json.loads(path.read_text())
    if (bank.get('kind') != 'private-fm1-audition' or bank.get('schema') != 1 or
            bank.get('native_sources') != native_sources() or
            bank.get('source_sha256') != 'bef34cad83a7a11ccb01c7da057cf93c2cd832057f2e1e7a7fe204a66dc1cba3'):
        raise ValueError('unrecognized or stale standalone FM-1 audition bank')
    records = bank['records']
    expected_cases = {(note, warm) for note in (36, 48, 60, 72) for warm in (0, 1)}
    if len(records) != 6 or len({r['name'] for r in records}) != len(records):
        raise ValueError('standalone FM-1 recipe coverage mismatch')
    for index, record in enumerate(records):
        validate_controls(record)
        cases = record['measurement']['cases']
        if (record['index'] != index or record['category'] not in CATEGORIES[1:CATEGORIES.index('PLAY FIRST')] or
                len(cases) != len(expected_cases) or
                {(case['note'], case['warm']) for case in cases} != expected_cases or
                any(not math.isfinite(case['native']['peak_dbfs']) or
                    not -55 <= case['native']['peak_dbfs'] <= 0 for case in cases)):
            raise ValueError('invalid standalone FM-1 audition evidence')
    return records


def first_play(records, groups):
    """A single page of measured audition candidates, balanced by family."""
    slots = []
    for family, quota in (('BASS', 4), ('LEAD', 4), ('KEYS', 2), ('PADS', 2),
                          ('BRASS', 2), ('WINDS', 2)):
        candidates = []
        for index in groups[CATEGORIES.index(family)]:
            record, flags = records[index]
            if flags & (1 | 8 | 16):  # Quiet or without the comparable TAL RMS measurements.
                continue
            cases = record['calibration_measurement']['cases']
            if any('rms_dbfs' not in case or 'onset_frames' not in case for case in cases):
                continue
            rms = [case['rms_dbfs'] for case in cases]
            if (not all(math.isfinite(value) for value in rms) or min(rms) < -44 or
                    max(rms) - min(rms) > 12 or
                    not all(0 <= case['onset_frames'] <= 11025 for case in cases)):
                continue
            category = record.get('category', '')
            source = re.split(r'[/\\]', category, maxsplit=1)[0] or record.get('collection', '')
            candidates.append((bool(flags & 4), bool(flags & 2), -min(rms), max(rms) - min(rms), index, source.casefold()))
        source_counts = {}
        ranked = []
        for gaps, adapted, loudness, spread, index, source in sorted(candidates):
            key = (gaps, adapted, source)
            rank = source_counts.get(key, 0)
            source_counts[key] = rank + 1
            ranked.append((gaps, adapted, rank, loudness, spread, index))
        slots.extend(item[-1] for item in sorted(ranked)[:quota])
    return slots


def compile_bank(path, out, studio_path=None, studio_audit=None, fm1_path=None):
    bank = json.loads(path.read_text())
    if bank.get('schema') != 1 or bank.get('kind') != 'private-tal-audition' or bank.get('native_sources') != native_sources():
        raise ValueError('unrecognized or stale private bank; regenerate against current calibrated voice')
    records = bank['records']
    if not 1 <= len(records) <= 512:
        raise ValueError('private bank must contain 1 to 512 patches')
    if bool(studio_path) != bool(studio_audit):
        raise ValueError('Studio drafts and audition evidence are required together')
    studio = studio_records(studio_path, studio_audit) if studio_path else []
    fm1 = fm1_records(fm1_path) if fm1_path else []
    combined = [(r, validate_record(r)) for r in records] + [(r, 8) for r in studio] + [(r, 16) for r in fm1]
    if len(combined) > 640:
        raise ValueError('combined audition bank exceeds 640 patches')
    lines = ['/* Private audition data; see the source manifests for provenance and limits. */',
             'static const tg_imported_preset tg_imported_presets[] = {']
    groups = [[] for _ in CATEGORIES]
    seen = [set() for _ in CATEGORIES]
    for index, (r, flags) in enumerate(combined):
        category = CATEGORIES.index(r['category']) if flags & 16 else musical_category(r)
        # Category pages expose one copy of each exact native sound. All
        # retains aliases, source identities and independent phrase metadata.
        fingerprint = (tuple(r['values'][f'TG_{key}'] for key in VALUES),
                       tuple(r['modes'].get(key, 0) for key in MODES))
        if fingerprint not in seen[category]:
            seen[category].add(fingerprint)
            groups[category].append(index)
        # The LCD font is ASCII; the manifest keeps the exact full source name.
        name = json.dumps(''.join(c if 32 <= ord(c) <= 126 else '?' for c in r['name']))
        values = ', '.join(f'[{k}] = {v}' for k, v in r['values'].items())
        modes = ', '.join(f'.{k} = {v}' for k, v in r['modes'].items())
        lines.append(f'    {{{{{name}, {{.value = {{{values}}}, {modes}}}}}, {flags}, {category}}},')
    lines.append('};')
    groups[CATEGORIES.index('PLAY FIRST')] = first_play(combined, groups)
    groups[CATEGORIES.index('FM1 RECIPES')] = list(range(len(records) + len(studio), len(combined)))
    order, offsets = [], [0]
    for group in groups:
        order.extend(group)
        offsets.append(len(order))
    lines.append('static const uint16_t tg_imported_order[] = {' + ','.join(map(str, order)) + '};')
    lines.append('static const uint16_t tg_imported_offsets[TG_IMPORT_CATEGORY_COUNT+1] = {' + ','.join(map(str, offsets)) + '};')
    out.mkdir(parents=True, exist_ok=True)
    (out / 'imported_bank.inc').write_text('\n'.join(lines) + '\n')
    (out / 'imported-bank.json').write_text(json.dumps(bank, indent=2) + '\n')
    report = {'count': len(combined), 'manifest_sha256': sha(out / 'imported-bank.json'),
              'category_counts': {name: len(combined) if i == 0 else len(groups[i]) for i, name in enumerate(CATEGORIES)},
              'play_first_slots': groups[CATEGORIES.index('PLAY FIRST')],
              'include_sha256': sha(out / 'imported_bank.inc'), 'status': 'private audition; listening and device acceptance pending'}
    if studio_path:
        (out / 'studio-drafts.json').write_bytes(studio_path.read_bytes())
        (out / 'studio-audit.json').write_bytes(studio_audit.read_bytes())
        report['studio'] = {'count': len(studio), 'first_index': len(records),
                            'drafts_sha256': sha(studio_path), 'audit_sha256': sha(studio_audit)}
    if fm1_path:
        (out / 'fm1-bank.json').write_bytes(fm1_path.read_bytes())
        report['fm1'] = {'count': len(fm1), 'first_index': len(records) + len(studio),
                        'manifest_sha256': sha(fm1_path)}
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('drafts', type=Path)
    p.add_argument('--count', type=int, default=58)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    records = json.loads(args.drafts.read_text())
    if not 1 <= args.count <= min(512, len(records)):
        raise ValueError('invalid selection count')
    selected = records[:args.count]
    for r in selected:
        validate_record(r)
    # Keep the existing ordering: original 58 candidates first, then adaptations.
    bank = {'schema': 1, 'kind': 'private-tal-audition', 'drafts_sha256': sha(args.drafts),
            'native_sources': native_sources(), 'records': selected,
            'limits': 'Approximate mono translations; no acoustic-equivalence claim. Source phrases are retained as metadata, not loaded into the sound bank.'}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('x') as f:
        f.write(json.dumps(bank, indent=2) + '\n')
    print(json.dumps({'count': len(selected), 'sha256': sha(args.out), 'installed': False}))


if __name__ == '__main__':
    main()
