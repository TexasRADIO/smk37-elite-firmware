#!/usr/bin/env python3
"""Verify the public BB-37 packages use their reserved Hub identities."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXPECTED = {
    "BB-37-Pro-Build-800.fwsc": "FM-1_92370001",
    "BB-37-Elite-Build-800.fwsc": "FM-1_92370002",
    "BB-37-MKE-P37-Build-800.fwsc": "FM-1_92370003",
}


def identity(path: Path) -> str:
    raw = path.read_bytes()
    if len(raw) < 960:
        raise ValueError(f"{path.name}: package is too short")
    chars = []
    for index in range(20):
        marker = raw[index * 48 + 47]
        if marker != 0x7D:
            chars.append(chr((marker - index - 1) & 0xFF))
    return "".join(chars)


for name, expected in EXPECTED.items():
    path = ROOT / "firmware" / name
    actual = identity(path)
    if actual != expected:
        raise SystemExit(f"{name}: expected {expected}, found {actual}")
    print(f"{name}: {actual}")
