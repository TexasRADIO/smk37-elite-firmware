#!/usr/bin/env python3
"""Build the serial Elite link probe using a local JieLi PI32v2 toolchain."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
UNITS = [
    "pi32/probe.c", "pi32/runtime.c", "fixture.c", "elite.c",
    "vendor/bb/bb_core.c", "vendor/bb/bb_dillo.c", "vendor/bb/bb_telegram.c",
    "vendor/bb/bb_output.c", "vendor/bb/x0x/drum808.c", "vendor/bb/x0x/bass303.c",
]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    output = args.out.resolve()
    output.mkdir(exist_ok=False)
    toolchain = args.toolchain.resolve()
    bins = toolchain / "pi32v2/bin"
    cc, ld, objdump = [bins / tool for tool in ("clang", "ld", "objdump")]

    commands = []
    def run(label, command):
        command = list(map(str, command))
        commands.append(command)
        (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=60)
        (output / label).write_text(result.stdout + result.stderr)
        result.check_returncode()
        return result.stdout

    version = run("compiler.txt", [cc, "--version"])
    flags = ["-target", "pi32v2", "-mcpu=r3", "-mfprev1"]
    c_flags = [
        *flags, "-std=c11", "-ffreestanding", "-fno-builtin", "-fno-common",
        "-ffunction-sections", "-fdata-sections", "-ffp-contract=off", "-O2",
        "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
        "-Wno-missing-field-initializers", "-DBASS303_CTRL=1",
        "-Itools/model-headers", "-Ivendor/bb", "-Ivendor/bb/x0x", "-Ivendor/smk37",
    ]
    source_pins = json.loads((ROOT / "SOURCE-PINS.json").read_text())
    for path, pin in source_pins.items():
        if sha(ROOT / path) != pin["sha256"]:
            raise ValueError(f"Vendored source changed: {path}")
    inputs = set(source_pins) | set(UNITS) | {
        "elite.h", "fixture.h", "tools/model-headers/string.h", "pi32/probe.ld",
        "pi32/build.py", "SOURCE-PINS.json",
    }

    objects = []
    for unit in UNITS:
        name = Path(unit).stem
        assembly = output / (name + ".s")
        obj = output / (name + ".o")
        run(name + "-compile.log", [cc, *c_flags, "-S", unit, "-o", assembly])
        run(name + "-assemble.log", [cc, *flags, "-c", assembly, "-o", obj])
        objects.append(obj)

    elf = output / "elite-probe.elf"
    run("link.log", [
        ld, "--gc-sections", "--no-undefined", "-T", ROOT / "pi32/probe.ld",
        "-Map", output / "elite-probe.map", "-o", elf, *objects,
    ])
    run("sections.txt", [objdump, "-h", elf])
    run("symbols.txt", [objdump, "-t", elf])
    run("linked.dis", [objdump, "-d", elf])
    run("unresolved.txt", [bins / "nm", "-u", elf])
    if (output / "unresolved.txt").read_text().strip():
        raise ValueError("Unresolved symbols in linked probe")
    report = {
        "profile": "PI32v2 r3 hard float standalone Elite audio link probe",
        "compiler": version.strip(),
        "toolchain_sha256": {p.name: sha(p) for p in (cc, ld, objdump)},
        "units_sha256": {unit: sha(ROOT / unit) for unit in UNITS},
        "inputs_sha256": {name: sha(ROOT / name) for name in sorted(inputs)},
        "linker_script_sha256": sha(ROOT / "pi32/probe.ld"),
        "object_sha256": {obj.name: sha(obj) for obj in objects},
        "assembly_sha256": {obj.with_suffix(".s").name: sha(obj.with_suffix(".s")) for obj in objects},
        "elf_sha256": sha(elf),
        "linked_dis_sha256": sha(output / "linked.dis"),
        "evidence_sha256": {
            name: sha(output / name) for name in
            ("sections.txt", "symbols.txt", "unresolved.txt", "elite-probe.map", "commands.json")
        },
        "undefined_symbols": 0,
    }
    (output / "build.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Linked {elf.name} ({report['elf_sha256']})")

if __name__ == "__main__":
    main()
