#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Decode the review package and check it against the linked artifacts."""
import argparse
import binascii
import hashlib
import json
import re
from pathlib import Path
import struct
import zlib

if __package__:
    from .vendor.tools.lz4blk import decompress
else:
    from vendor.tools.lz4blk import decompress


def check(condition, message):
    if not condition:
        raise ValueError(message)


def u16(data, pos=0):
    return struct.unpack_from("<H", data, pos)[0]


def crc(data):
    return binascii.crc_hqx(data, 0)


def decode(data, key=0xffff):
    decoded = bytearray()
    for byte in data:
        decoded.append(byte ^ (key & 255))
        key = ((key << 1) ^ (0x1021 if key & 0x8000 else 0)) & 0xffff
    return bytes(decoded)


def entry(data):
    check(len(data) == 32 and crc(data[2:]) == u16(data), "JLFS entry CRC")
    checksum, offset, size = struct.unpack_from("<HII", data, 2)
    name = data[16:32].split(b"\0", 1)[0]
    return name, offset, size, checksum


def verify(build, *, raw=None, factory_only=False):
    record = json.loads((build / "build.json").read_text())
    package = "elite-review.fwsc"
    if raw is None:
        raw = (build / package).read_bytes()
    package_sha = hashlib.sha256(raw).hexdigest()
    check(package_sha == record["sha256"][package], f"Build artifact drift: {package}")
    linked_elf = b""
    for name, sha in record["sha256"].items():
        if name != package:
            data = (build / name).read_bytes()
            check(hashlib.sha256(data).hexdigest() == sha, f"Build artifact drift: {name}")
            if name == "elite-review.elf":
                linked_elf = data

    if factory_only:
        check("imported_bank" in record and record["imported_bank"] is None,
              "Factory-only verification requires a manifest without a private preset bank")
        check(linked_elf.startswith(b"\x7fELF") and b"tg_presets\0" in linked_elf,
              "Factory-only verification requires the unstripped linked ELF")
        private_symbols = (b"tg_imported_presets\0", b"tg_imported_offsets\0", b"tg_imported_order\0")
        check(not (build / "imported_bank.inc").exists() and
              not any(symbol in linked_elf for symbol in private_symbols),
              "Factory-only verification found private preset data in the build artifacts")

    markers = raw[47:960:48]
    identity = "".join(chr((marker - i - 1) & 255) for i, marker in enumerate(markers) if marker != 0x7d)
    check(identity == record["product"] and re.fullmatch(r"(?:SMK-37 Pro|SMK-37 Elite|MKE-P37)_[0-9]+", identity),
          "FWSC product marker")
    logical = b"".join(raw[start:start + 47] for start in range(0, 960, 48)) + raw[960:]
    header = decode(logical[:64])
    total, count = struct.unpack_from("<IH", header, 4)
    check(total == len(logical) and count == 2 and header[16:22] == b"AC791N", "UFW header fields")
    directory = logical[64:64 + 80 * count]
    check(crc(header[2:]) == u16(header) and crc(directory) == u16(header, 2), "UFW header/directory CRC")
    files = {}
    end = 0x400
    for index in range(count):
        row = decode(directory[index * 80:(index + 1) * 80])
        kind, slot, checksum, _, offset, size, padded = struct.unpack_from("<HHHHIII", row)
        name = row[64:80].rstrip(b"\0")
        check(slot == index and kind == (0, 100)[index] and name == (b"flash.bin", b"ota.bin")[index],
              "Unexpected UFW file entry")
        check(end <= offset and offset % 256 == 0 and offset + size <= total and padded == (size + 31) & ~31,
              "UFW entry bounds")
        end = offset + size
        files[name] = logical[offset:end]
        check(crc(files[name]) == checksum, "UFW payload CRC")
    check(end == total, "Trailing UFW bytes")

    flash = files[b"flash.bin"]
    check(len(flash) == 0x9d000, "Elite PRCT length")
    check(flash[0x93000:] == b"\xff" * 0xa000, "Unexpected PRCT tail content")
    head = decode(flash[:32])
    check(crc(head[2:]) == u16(head), "Flash head CRC")
    name, offset, size, checksum = entry(decode(flash[64:96]))
    isd = flash[offset:offset + size]
    check(name == b"isd_config.ini" and offset + size <= 0x4000 and crc(isd) == checksum,
          "Flash head key record")
    check(crc(isd[:32]) == u16(isd, 32), "Flash key blob CRC")
    # The retained packer emits this synthetic, known key blob. The loader
    # compares the package to the installed device's own key at update time.
    key = sum((isd[15 - bit] == isd[16 + bit]) << bit for bit in range(16))
    check(key == 0x980f, "Review package chip key")
    area = b"".join(decode(flash[0x4000 + pos:0x4000 + pos + 32], key ^ (pos >> 2))
                    for pos in range(0, 0x8f000, 32))
    name, offset, size, checksum = entry(area[:32])
    check(name == b"app_area_head" and offset == 0x02000120 and crc(area[32:size]) == checksum,
          "App directory CRC")
    name, offset, size, checksum = entry(area[32:64])
    check((name, offset, size, checksum) == (b"app.bin", 0x120, 0x8dfbc, 0x4352), "App slot layout")
    app = area[offset:offset + size]
    check(crc(app) == checksum, "Fixed app slot CRC")
    regions = [entry(area[pos:pos + 32])[:3] for pos in range(96, 288, 32)]
    check(regions == [
        (b"VM", 0x9d000, 0x23000), (b"PRCT", 0, 0x9d000),
        (b"BTIF", 0xc0000, 0x1000), (b"USRTRIM", 0xc1000, 0x1000),
        (b"USRFLASH", 0xc2000, 0x29000), (b"USR", 0xf4000, 0xa000),
    ], "Elite region table")

    expected_app = (build / "elite-review-app.bin").read_bytes()
    check(app[:len(expected_app)] == expected_app, "Packaged image differs from linked image")
    check(app[:4] == app[0xee0:0xee4] == bytes.fromhex("04818000"), "SPL and body entries")
    stub_sha = hashlib.sha256(app[:0xee0]).hexdigest()
    lock_names = {"pro": "stub_smk37.lock", "elite": "stub_smk37-elite.lock", "mke": "stub_smk37-mke.lock"}
    legacy_models = {"SMK-37 Pro": "pro", "SMK-37 Elite": "elite", "MKE-P37": "mke"}
    model = record.get("model") or legacy_models.get(identity.rsplit("_", 1)[0])
    check(model in lock_names, "Build model")
    lock = (build / "bsp/firmware/stub" / lock_names[model]).read_text().split()
    check(lock == ["3", stub_sha] and stub_sha == record["stub_sha256"], "Locked SMK-37 v3 stub")
    mark_offset = 0x91fe0 - 0x4120
    marker = app[mark_offset:mark_offset + 20]
    magic, fmt, length, body_crc, mark_crc = struct.unpack("<4sIIII", marker)
    check(magic == b"CRBM" and fmt == 1 and length == len(expected_app) - 0xee0 and
          zlib.crc32(app[0xee0:0xee0 + length]) == body_crc and zlib.crc32(marker[:16]) == mark_crc,
          "Boot commit marker")
    check(app[len(expected_app):mark_offset] == b"\xff" * (mark_offset - len(expected_app)),
          "Unexpected payload between body and commit marker")

    ota = files[b"ota.bin"]
    path = build / "bsp/build/loader"
    check(ota == (path / "ota.bin").read_bytes() and len(ota) <= 0x4000, "Loader staging footprint")
    loader_sha = hashlib.sha256(ota).hexdigest()
    check(loader_sha == record["loader_sha256"], "Loader hash")
    check(entry(ota[:32]) == (b"usb_hid_ota.bin", 32, len(ota) - 32, crc(ota[32:])),
          "OTA loader outer entry")
    image = (path / "loader.bin").read_bytes()
    check(entry(ota[32:64]) == (b"usb_hid_ota.bin", len(image), 0x01c0a800, crc(image)),
          "OTA loader load address and image CRC")
    decoded = bytearray()
    pos = 64
    while pos < len(ota):
        compressed, size = struct.unpack_from("<II", ota, pos)
        pos += 8
        check(0 < compressed <= 4096 and 0 < size <= 4096 and pos + compressed <= len(ota),
              "OTA loader compressed block bounds")
        block = decompress(ota[pos:pos + compressed])
        check(len(block) == size, "OTA loader decompressed block size")
        decoded += block
        pos += compressed
    check(decoded == image, "Packaged loader differs from linked loader")
    report = {"product": identity, "flash_payload_bytes": len(flash), "body_bytes": length,
              "body_crc32": f"{body_crc:08x}", "stub_sha256": stub_sha, "ota_bytes": len(ota),
              "loader_sha256": loader_sha, "package_sha256": package_sha}
    if factory_only:
        report["factory_only_verified"] = True
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--factory-only", action="store_true",
                        help="require a factory-only preset catalogue")
    args = parser.parse_args()
    try:
        report = json.dumps(verify(args.build, factory_only=args.factory_only), indent=2) + "\n"
    except (OSError, ValueError) as error:
        parser.exit(1, f"Elite package verification: {error}\n")
    (args.build / "package-check.json").write_text(report)
    print(report, end="")
