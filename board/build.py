#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the SMK-37 Elite review package with an offline PI32 toolchain."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from ota_stage import loader_header, stage as stage_ota
from usb_clock import stage as stage_clock

ROOT = Path(__file__).resolve().parents[1]
SMK_MODELS = {
    "pro": {"identity": "SMK-37 Pro", "stub_lock": "stub_smk37.lock"},
    "elite": {"identity": "SMK-37 Elite", "stub_lock": "stub_smk37-elite.lock"},
    "mke": {"identity": "MKE-P37", "stub_lock": "stub_smk37-mke.lock"},
}
UNITS = ["board/main.c", "pi32/runtime.c", "synth/telegram101.c", "synth/presets.c", "synth/fx101.c", "synth/store101.c", "synth/catalog101.c", "synth/midi101.c", "synth/clock101.c"]
DRUM_RENDER_HOT = ("bb_drum808_render_q23", "bd_tick", "sd_tick", "tom_tick", "cl_tick", "rs_tick", "cp_tick",
                   "hat_tick", "cy_tick", "shape", "shaper", "bd_reset", "env_arm")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stage_audio_restart(source):
    signature = "FM1_INLINE void fm1_audio_init(int32_t *buf, uint32_t half_words, void (*isr)(void), uint32_t prio)"
    attach = "    fm1_irq_attach(FM1_IRQ_ALNK0, isr, prio);"
    boundary = "FM1_INLINE uint8_t fm1_audio_pending(void)"
    for expected in (signature, attach, boundary):
        if source.count(expected) != 1:
            raise ValueError("Unexpected audio HAL at the Elite restart hook")
    source = source.replace(signature, "FM1_INLINE void fm1_audio_configure(int32_t *buf, uint32_t half_words)")
    source = source.replace(attach, "}\n\nFM1_INLINE void fm1_audio_start(void)\n{")
    return source.replace(boundary, """FM1_INLINE void fm1_audio_init(int32_t *buf, uint32_t half_words, void (*isr)(void), uint32_t prio)
{
    fm1_audio_configure(buf, half_words);
    fm1_irq_attach(FM1_IRQ_ALNK0, isr, prio);
    fm1_audio_start();
}

FM1_INLINE void fm1_audio_resume(int32_t *buf, uint32_t half_words)
{
    fm1_audio_configure(buf, half_words);
    /* The vector and its priority are already installed and write-protected. */
    FM1_ICFG(FM1_IRQ_ALNK0) |= 1u << ((FM1_IRQ_ALNK0 & 7u) * 4u);
    /* The caller is still masked. Start DMA immediately before it restores
     * interrupts, so a pending half cannot age through the cold boot delay. */
    FM1_ALNK_CON0 |= 0x800u;
}

""" + boundary)


def stage_usb_tx(source):
    start = source.index("static USB_OUTLINE void ep1_tx(void)")
    end = source.index("\n}", start) + 2
    return source[:start] + (ROOT / "board/usb_tx.inc").read_text().rstrip() + source[end:]


def stage_usb_identity(source):
    # Give the MIDI-only app its own descriptor cache version. The loader is
    # built from the untouched vendor tree before this staging step.
    replacements = {
        '"ChoralRoot", "ChoralRoot FM-1"': '"Big Beat", ELITE_USB_PRODUCT',
        'FELUCCA_USB_PID >> 8, 0x00, 0x03,': 'FELUCCA_USB_PID >> 8, 0x02, 0x03,',
    }
    for old, new in replacements.items():
        if source.count(old) != 1:
            raise ValueError(f"Unexpected USB source at Elite identity: {old}")
        source = source.replace(old, new)
    return '#include "board/usb_diag.h"\n' + source


def font_header(path, dest):
    glyphs = {}
    for glyph in path.read_text().split("STARTCHAR ")[1:]:
        code = int(re.search(r"^ENCODING (\d+)", glyph, re.M)[1])
        if 32 <= code <= 126:
            rows = glyph.split("BITMAP\n", 1)[1].split("ENDCHAR", 1)[0].splitlines()
            if len(rows) != 16 or any(len(row) != 2 for row in rows):
                raise ValueError(f"Invalid ASCII glyph {code}")
            glyphs[code] = rows
    lines = ["/* Terminus, SIL OFL 1.1. See board/vendor/font. */",
             "static const uint8_t font[95][16] = {"]
    for code in range(32, 127):
        lines.append("    {" + ", ".join("0x" + row for row in glyphs[code]) + "},")
    dest.write_text("\n".join([*lines, "};", ""]))


def build(args):
    model = SMK_MODELS[args.model]
    product = f"{model['identity']}_{args.build_number}"
    out = args.out.resolve()
    out.mkdir(exist_ok=False)
    imported = None
    if (args.studio_bank or args.fm1_bank) and not args.imported_bank:
        raise ValueError('Studio and standalone FM-1 drafts require the optional TAL base bank')
    if args.imported_bank:
        spec = importlib.util.spec_from_file_location("tal_bank", ROOT / "tools/tal_bank.py")
        bank_tool = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(bank_tool)
        imported = bank_tool.compile_bank(args.imported_bank, out, args.studio_bank, args.studio_audit, args.fm1_bank)
    synth_presets = None
    if args.synth_presets:
        bank = args.synth_presets / "factory_synth_phrases.inc"
        manifest = args.synth_presets / "factory_synth_phrases.json"
        catalog = json.loads(manifest.read_text())
        bank_hash = sha(bank)
        if bank_hash != catalog["include_sha256"]:
            raise ValueError("Synth preset include does not match its manifest")
        synth_presets = {
            "include_sha256": bank_hash, "manifest_sha256": sha(manifest),
            "counts": {name: len(catalog["entries"][name]) for name in ("telegram", "dillo")},
        }
        if not all(synth_presets["counts"].values()):
            raise ValueError("Both synths must have factory phrases")
        shutil.copyfile(bank, out / bank.name)
    band_presets = None
    if args.band_presets:
        bank = args.band_presets / "factory_band_presets.inc"
        manifest = args.band_presets / "factory_band_presets.json"
        catalogue = json.loads(manifest.read_text())
        if sha(bank) != catalogue["include_sha256"] or (
            catalogue["synth_include_sha256"] != synth_presets["include_sha256"] or
            catalogue["synth_manifest_sha256"] != synth_presets["manifest_sha256"]
        ):
            raise ValueError("Band presets do not match the synth phrase bank")
        band_presets = {"include_sha256": sha(bank), "manifest_sha256": sha(manifest),
                        "count": len(catalogue["presets"])}
        shutil.copyfile(bank, out / bank.name)
    percussion = None
    if args.percussion_plan:
        directory = args.percussion_plan / "percussion_flash.inc"
        manifest = args.percussion_plan / "plan.json"
        plan = json.loads(manifest.read_text())
        if sha(directory) != plan["directory_sha256"]:
            raise ValueError("Percussion directory does not match its flash plan")
        percussion = {"directory_sha256": sha(directory), "plan_sha256": sha(manifest),
                      "bytes": plan["pool_bytes"], "clips": len(plan["clips"])}
        shutil.copyfile(directory, out / directory.name)
    tc = args.toolchain.resolve()
    staged = out / "bsp"
    board_pins = json.loads((ROOT / "board/BOARD-PINS.json").read_text())["files"]
    dsp_pins = json.loads((ROOT / "SOURCE-PINS.json").read_text())
    for file, pin in board_pins.items():
        if sha(ROOT / "board/vendor" / file) != pin:
            raise ValueError(f"Board source changed: {file}")
    for file, pin in dsp_pins.items():
        if sha(ROOT / file) != pin["sha256"]:
            raise ValueError(f"DSP source changed: {file}")
    shutil.copytree(ROOT / "board/vendor", staged)
    font_header(staged / "font/ter-u16n.bdf", out / "font.inc")
    os.environ["JIELI_TOOLCHAIN"] = str(tc)
    os.environ.pop("FM1_STUB_LOCK", None)
    spec = importlib.util.spec_from_file_location("retained_builder", staged / "tools/build.py")
    bsp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bsp)
    pack = bsp.fm1pkg_make
    pack.SDK = staged / "tools/sdk"
    pack.set_board("smk37")
    for file, pin in bsp.SDK_SHA256.items():
        if hashlib.sha256(pack.sdk_file(file)).hexdigest() != pin:
            raise ValueError(f"SDK source changed: {file}")
    bsp.BOARD = "smk37"
    if args.model != "pro":
        bsp.BOARD_DEFS["smk37"] += [f'-DSMK_MODEL="{model["identity"]}"']
    bsp.BOARD_LOCK["smk37"] = model["stub_lock"]
    bsp.VERSION = "Elite review"
    stub = bsp.build_stub()
    ota = bsp.build_loader()

    # Add read-only diagnostics to the running app after building the pinned loader.
    app_usb = staged / "firmware/src/usb.c"
    usb = stage_usb_identity(stage_usb_tx(stage_clock(app_usb.read_text())))
    hooks = {
        "static USB_COLD void ep0_service(void)":
            '#include "board/usb_diag.inc"\n\nstatic USB_COLD void ep0_service(void)',
        "    case 0x8006: {":
            """    case (0x40 << 8) | ELITE_USB_RECONNECT_REQUEST:
        if (wvalue != ELITE_USB_DIAG_VALUE || wlength ||
            !elite_usb_queue_reconnect((uint16_t)(s[4] | (uint16_t)s[5] << 8)))
            goto stall;
        goto ack;
    case (0xC0 << 8) | ELITE_USB_DIAG_REQUEST:
        if (wvalue != ELITE_USB_DIAG_VALUE || s[5] != 0)
            goto stall;
        if (s[4] == ELITE_USB_DIAG_SCHEMA && wlength == sizeof(elite_usb_diag)) {
            elite_usb_diag_send(wlength);
            return;
        }
        if (s[4] == ELITE_USB_AUDIO_SCHEMA && wlength == sizeof(elite_usb_audio)) {
            elite_usb_audio_send(wlength);
            return;
        }
        if (s[4] == ELITE_USB_MIDI_SCHEMA && wlength == sizeof(elite_usb_midi)) {
            elite_usb_midi_send(wlength);
            return;
        }
        if (s[4] == ELITE_USB_CLOCK_SCHEMA && wlength == sizeof(elite_usb_clock)) {
            elite_usb_clock_send(wlength);
            return;
        }
        if (s[4] == ELITE_USB_BOOT_SCHEMA && wlength == sizeof(elite_usb_boot)) {
            elite_usb_boot_send(wlength);
            return;
        }
        if (s[4] == ELITE_USB_ACTIVITY_SCHEMA && wlength == sizeof(elite_usb_activity)) {
            elite_usb_activity_send(wlength);
            return;
        }
#ifdef ELITE_DRUMS808
        if (s[4] == ELITE_USB_CONDUCTOR_SCHEMA && wlength == sizeof(elite_usb_conductor)) {
            elite_usb_conductor_send(wlength);
            return;
        }
        if (s[4] == ELITE_USB_CONDUCTOR_LIVE_SCHEMA && wlength == sizeof(elite_usb_conductor_live)) {
            elite_usb_conductor_live_send(wlength);
            return;
        }
        if (s[4] == ELITE_USB_CONDUCTOR_KITS_SCHEMA && wlength == sizeof(elite_usb_conductor_kits)) {
            elite_usb_conductor_kits_send(wlength);
            return;
        }
        if (s[4] == ELITE_USB_CONDUCTOR_CUES_SCHEMA && wlength == sizeof(elite_usb_conductor_cues)) {
            elite_usb_conductor_cues_send(wlength);
            return;
        }
#ifdef ELITE_RENDER_PROFILE
        if (s[4] == ELITE_USB_RENDER_PROFILE_SCHEMA && wlength == sizeof(elite_usb_render_profile)) {
            elite_usb_render_profile_send(wlength);
            return;
        }
#endif
        if (s[4] == ELITE_USB_RECALL_SCHEMA && wlength == sizeof(elite_usb_recall)) {
            elite_usb_recall_send(wlength);
            return;
        }
#endif
        goto stall;
    case 0x8006: {""",
        "        usb.config = s[2];": (
            "        elite_tx_sysex_events = 0;\n        usb.config = s[2];"
        ),
        "    usb.rx_pkts++;": "    elite_usb_diag_rx(ep1rx, n);\n    usb.rx_pkts++;",
    }
    for before, after in hooks.items():
        if usb.count(before) != 1:
            raise ValueError(f"Unexpected USB source at diagnostic hook: {before}")
        usb = usb.replace(before, after)
    app_usb.write_text(usb)
    app_audio = staged / "firmware/hal/fm1_audio.h"
    app_audio.write_text(stage_audio_restart(app_audio.read_text()))
    app_ota = staged / "firmware/src/ota.c"
    app_ota.write_text(stage_ota(app_ota.read_text()))
    (out / "elite_ota_loader.inc").write_text(loader_header(ota))


    cc = tc / "pi32v2/bin/clang"
    ld = tc / "pi32v2/bin/ld"
    objdump = tc / "common/bin/objdump"
    objcopy = tc / "common/bin/objcopy"
    commands = []

    def run(log, *command):
        command = [str(value) for value in command]
        commands.append(command)
        (out / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=120)
        (out / log).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise SystemExit(f"{log}: {result.stderr or result.stdout}")
        return result.stdout

    version = run("compiler.txt", cc, "--version").strip()
    flags = ["-target", "pi32v2", "-mcpu=r3", "-mfprev1"]
    c_flags = [
        *flags, "-std=gnu11", "-ffreestanding", "-fno-builtin", "-fno-common",
        "-ffunction-sections", "-fdata-sections", "-ffp-contract=off", "-O2",
        "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-missing-field-initializers",
        f"-DELITE_BUILD={args.build_number}",
        *bsp.BOARD_DEFS["smk37"], f'-DFELUCCA_ID="{product}"',
        "-I.", "-Itools/model-headers", "-Ivendor/bb", "-Ivendor/bb/x0x", "-Ivendor/smk37",
        f"-I{staged / 'firmware/hal'}", f"-I{staged / 'firmware/src'}", f"-I{out}",
    ]
    if imported:
        c_flags.append("-DTG_IMPORT_BANK=1")
    if synth_presets:
        c_flags.append("-DELITE_SYNTH_PRESETS=1")
    if band_presets:
        c_flags.append("-DELITE_BAND_PRESETS=1")
    units = UNITS
    layer_headers = []
    if args.dillo:
        c_flags += ["-DELITE_DILLO=1", "-DBASS303_CTRL=1"]
        layer_headers += ["dillo/layer303.h", "dillo/phrase303.h", "conductor/dry_bus.h", "vendor/bb/bb_dillo.h",
                         "vendor/bb/x0x/bass303.h", "vendor/bb/x0x/fastmath.h"]
        units = units + ["dillo/layer303.c", "dillo/phrase303.c", "vendor/bb/bb_core.c", "vendor/bb/bb_dillo.c", "vendor/bb/x0x/bass303.c"]
    if args.render_profile:
        c_flags += ["-DELITE_RENDER_PROFILE=1"]
    if args.drums:
        c_flags += ["-DELITE_DRUMS808=1", "-DCONDUCTOR_DRY_STRIDE=1"]
        layer_headers += ["board/conductor_storage.inc", "conductor/launch.h", "conductor/mixer.h", "drums/layer808.h", "vendor/bb/x0x/drum808.h", "vendor/bb/x0x/x0x_param.h"]
        units = units + ["conductor/launch.c", "conductor/mixer.c", "drums/layer808.c", "vendor/bb/x0x/drum808.c"]
    if percussion:
        c_flags.append("-DELITE_PERCUSSION_727=1")
        layer_headers += ["board/percussion_flash.h", "board/percussion_bench.inc", "board/layer_rhythms.inc", "drums/layer727.h"]
        units = units + ["drums/layer727.c"]
    if args.single_808:
        c_flags.append("-DELITE_SINGLE_808=1")
    if args.drum_render_in_ram:
        c_flags.append("-DELITE_808_RAM=1")
    if args.dillo_render_in_ram:
        c_flags.append("-DELITE_DILLO_RAM=1")
    objects = []
    for unit in units:
        name = Path(unit).stem
        assembly, obj = out / (name + ".s"), out / (name + ".o")
        run(name + "-compile.log", cc, *c_flags, "-S", unit, "-o", assembly)
        run(name + "-assemble.log", cc, *flags, "-c", assembly, "-o", obj)
        if args.drum_render_in_ram and name == "drum808":
            sizes, calls = bsp.ram_code.place(obj, DRUM_RENDER_HOT)
            (out / "drum-ram-placement.json").write_text(json.dumps({"bytes": sizes, "veneers": calls}, indent=2) + "\n")
        if args.dillo_render_in_ram and name == "bass303":
            sizes, _ = bsp.ram_code.place(obj, ("bass303_run",))
            (out / "dillo-ram-placement.json").write_text(json.dumps({"bytes": sizes}, indent=2) + "\n")
        objects.append(obj)
    for unit in ("crt0.S", "hal/fm1_vec.S", "hal/fm1_isr.S"):
        source = staged / "firmware" / unit
        obj = out / (source.stem + ".o")
        run(source.stem + "-assemble.log", cc, *flags, "-c", source, "-o", obj)
        objects.append(obj)
    elf = out / "elite-review.elf"
    run("link.log", ld, "--gc-sections", "--no-undefined", "-T", staged / "firmware/app.ld",
        "-Map", out / "elite-review.map", "-o", elf, *objects)
    sections = run("sections.txt", objdump, "-h", elf)
    symbols = run("symbols.txt", objdump, "-t", elf)
    dis = run("linked.dis", objdump, "-d", elf)
    ram = run("ram.dis", objdump, "-d", "-j", ".ram_text", elf)
    if run("unresolved.txt", tc / "pi32v2/bin/nm", "-u", elf).strip():
        raise ValueError("Unresolved firmware symbols")

    def sym(name):
        return int(re.search(rf"^([0-9a-f]+) .*\s{name}$", symbols, re.M)[1], 16)

    body = bytearray()
    for section, address in ((".text", bsp.APP_XIP), (".ram_text", sym("_rt_load")),
                             (".data", sym("_data_load"))):
        entry = re.search(rf"^\s*\d+\s+{re.escape(section)}\s+([0-9a-f]+)", sections, re.M)
        if not entry or int(entry[1], 16) == 0:
            continue
        blob = out / (section[1:] + ".bin")
        run(blob.name + ".log", objcopy, "-O", "binary", "-j", section, elf, blob)
        gap = address - bsp.APP_XIP - len(body)
        if gap < 0 or address % 4:
            raise ValueError(f"Invalid load address for {section}")
        body += b"\xff" * gap + blob.read_bytes()
    if sym("_c1_start") != sym("_c1_end"):
        raise ValueError("Unexpected core 1 code")
    body += b"\xff" * (-len(body) % 4)
    app = stub + bytes(body)
    errors, notes = bsp.check(app, symbols, dis, ram)
    if errors:
        raise ValueError("\n".join(errors))
    (out / "bsp-checks.txt").write_text("\n".join(notes) + "\n")
    (out / "elite-review-app.bin").write_bytes(app)
    package = out / "elite-review.fwsc"
    package.write_bytes(pack.ufw(pack.flash_image(app, pack.KEY), ota, product))
    report = {
        "product": product, "model": args.model, "compiler": version, "imported_bank": imported,
        "synth_presets": synth_presets, "band_presets": band_presets,
        "dillo_preview": args.dillo, "drums808_preview": args.drums, "render_profile": args.render_profile,
        "drum_render_in_ram": args.drum_render_in_ram,
        "dillo_render_in_ram": args.dillo_render_in_ram,
        "drum_render_q23": args.drums,
        "single_808": args.single_808, "percussion_plan": percussion,
        "sha256": {file.name: sha(file) for file in (package, elf, out / "elite-review-app.bin")},
        "stub_sha256": hashlib.sha256(stub).hexdigest(),
        "loader_sha256": hashlib.sha256(ota).hexdigest(),
        "toolchain_sha256": {p.name: sha(p) for p in (cc, ld, objdump, objcopy)},
        "source_sha256": {path: sha(ROOT / path) for path in
                          [*units, *layer_headers, "board/build.py", "board/BOARD-PINS.json", "synth/telegram101.h", "synth/filter101.inc", "synth/fx101.h",
                           "synth/store101.h", "synth/catalog101.h", "synth/midi101.h", "synth/clock101.h", "board/usb_clock.py", "board/midi_performance.inc", "board/midi_output.inc", "tools/tal_bank.py", "board/library_region.h", "board/library_storage.inc", "board/library_audio.inc", "board/ota_lifecycle.inc", "board/ota_safe_stage.inc", "board/ota_stage.py", "board/ota_trace.h",
                           "board/panel101.inc", "board/pad_feedback.inc", "board/dillo_pattern_sounds.inc", "board/instrument_view.inc", "board/view_chrome.inc", "board/picnic_aa.inc", "board/view_tel.inc", "board/view_dillo.inc", "board/view_band.inc",
                           "board/view_808.inc", "board/drum_presets.inc", "board/view_footer.inc", "board/view_focus.inc", "board/picnic_title.inc", "board/PICNIC-PROVENANCE.json", "SOURCE-PINS.json", "tools/model-headers/string.h",
                           "board/usb_diag.h", "board/usb_diag.inc", "board/usb_reconnect.inc", "board/usb_tx.inc", "Makefile"]},
        "regions": {
            "app_slot": [len(body), bsp.XIP_LEN],
            "ramtext": [sym("_rt_end") - sym("_rt_start"), bsp.RAMTEXT_LEN],
            "ram": [sym("_bss_end") - 0x01C08000, bsp.RAM_LEN],
            "pool": [sym("_pool_end") - sym("_pool_start"), bsp.POOL_LEN],
            "noinit": [int(re.search(r"^\s*\d+\s+\.noinit\s+([0-9a-f]+)", sections, re.M)[1], 16),
                       bsp.NOINIT_LEN],
            "foreground_stack": sym("_ustack_top") - sym("_ustack_lo"),
            "interrupt_stack": sym("_sstack_top") - sym("_sstack_lo"),
        },
    }
    (out / "build.json").write_text(json.dumps(report, indent=2) + "\n")
    print("\n".join(notes))
    print(f"{package.name}: {package.stat().st_size} B, SHA-256 {sha(package)}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--build-number", type=int, choices=range(475, 1000), default=512, metavar="475–999")
    parser.add_argument("--model", choices=sorted(SMK_MODELS), default="elite",
                        help="SMK-37 updater identity (default: elite)")
    parser.add_argument("--imported-bank", type=Path, help="optional local calibrated audition-bank JSON")
    parser.add_argument("--synth-presets", type=Path, help="generated synth phrase bank directory")
    parser.add_argument("--band-presets", type=Path, help="generated Band preset directory")
    parser.add_argument("--studio-bank", type=Path, help="optional Studio audition-draft JSON")
    parser.add_argument("--studio-audit", type=Path, help="measurements for the exact Studio draft JSON")
    parser.add_argument("--dillo", action="store_true", help="include the two-voice integration preview")
    parser.add_argument("--drums", action="store_true", help="include the synthesized 808 with --dillo")
    parser.add_argument("--fm1-bank", type=Path, help="optional measured standalone FM-1 recipe translations")
    parser.add_argument("--single-808", action="store_true",
                        help="render only 808 A and keep both parts in saved setups")
    parser.add_argument("--percussion-plan", type=Path,
                        help="optional local 727 flash plan directory")
    parser.add_argument("--render-profile", action="store_true",
                        help="capture per-lane ticks on the first audio deadline fault")
    parser.add_argument("--drum-render-in-ram", action="store_true",
                        help="place the 808 dry render loop and its helpers in executable RAM")
    parser.add_argument("--dillo-render-in-ram", action="store_true",
                        help="place Dillo's oscillator and filter loop in executable RAM")
    args = parser.parse_args()
    if bool(args.studio_bank) != bool(args.studio_audit):
        parser.error("--studio-bank and --studio-audit are required together")
    if args.drums and not args.dillo:
        parser.error("--drums requires --dillo")
    if args.synth_presets and not args.drums:
        parser.error("--synth-presets requires --drums")
    if args.band_presets and not args.synth_presets:
        parser.error("--band-presets requires --synth-presets")
    if args.single_808 and not args.drums:
        parser.error("--single-808 requires --drums")
    if args.percussion_plan and not args.single_808:
        parser.error("--percussion-plan requires --single-808")
    if args.render_profile and not args.drums:
        parser.error("--render-profile requires --drums")
    if args.drum_render_in_ram and not args.drums:
        parser.error("--drum-render-in-ram requires --drums")
    if args.dillo_render_in_ram and not args.dillo:
        parser.error("--dillo-render-in-ram requires --dillo")
    build(args)
