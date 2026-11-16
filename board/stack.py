#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Account for C frames and retained assembly entries in the linked Elite build."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "pi32"))
from audit import asm_instructions, prefix, require, sha, stack_allocation


def audit(build):
    record = json.loads((build / "build.json").read_text())
    for name, digest in record["sha256"].items():
        require(sha(build / name) == digest, f"Build artifact drift: {name}")
    for name, digest in record["source_sha256"].items():
        require(sha(ROOT / name) == digest, f"Build source drift: {name}")
    require(not (build / "unresolved.txt").read_text().strip(), "Unresolved symbols")
    require("ELF32-pi32v2" in (build / "sections.txt").read_text(), "Wrong ISA")

    # Bind the RAM flash calls and the journal callbacks explicitly. Reject
    # new call sites or a changed table instead of silently treating indirect
    # calls as leaf functions. Every reachable callback is included in the
    # peak calculation, conservatively including all three for save().
    main = (build / "main.s").read_text()
    far = set(re.findall(r"\br\d+ = (fl_\w+_ram)\b", main))
    require(far == {"fl_jedec_ram", "fl_erase4k_ram", "fl_prog_ram", "fl_read_ram"}, f"Unexpected far calls: {far}")
    table = re.search(r"^library_io:\n(.*?)^\s*\.size\s+library_io, 16$", main, re.M | re.S)
    require(table is not None, "Missing immutable journal callback table")
    require(re.findall(r"^\s*\.long\s+(\w+)\s*$", table[1], re.M) ==
            ["0", "library_read", "library_erase", "library_program"], "Unexpected journal callbacks")
    require('.section\t.rodata.library_io,' in main, "Journal callback table is not read-only")
    adapter = (ROOT / "board/library_storage.inc").read_text()
    require('static const tg_store_io library_io = {0, library_read, library_erase, library_program};' in adapter,
            "Journal table binding changed")
    journal_apis = ["load", "can_save", "save"]
    if record.get("drums808_preview"):
        journal_apis.append("save_tail")
    for api in journal_apis:
        calls = re.findall(r"\btg_store_" + api + r"\(([^,]+),", adapter)
        require(calls and all(arg == "&library_io" for arg in calls), f"Unbound journal {api} call")
    indirect_targets = {
        # The compiler can outline the library service and its flash ID check.
        "main_loop": (22 if re.search(r"^library_service:", main, re.M) else 23, far),
        "library_service": (1, {"fl_jedec_ram"}),
        "elite_stage_loader": (7, {"fl_erase4k_ram", "fl_prog_ram"}),
        "ota_staged": (1, {"fl_read_ram"}),
        "library_erase": (1, {"fl_erase4k_ram"}),
        "library_program": (1, {"fl_prog_ram"}),
        "current": (1, {"library_read"}),
        "tg_store_load": (1, {"library_read"}),
        "tg_store_load_tail": (1, {"library_read"}),
        # Current PI32 output unrolls the two-bank preflight: offset 0 and
        # 16384 each load io->read from the same immutable callback table.
        "tg_store_can_save": (2, {"library_read"}),
        # The shared bank selector checks the v15 guard. The save itself
        # calls erase, program and readback through the same table.
        "tg_store_save": (8, {"library_read", "library_erase", "library_program"}),
        "tg_store_save_tail": (8, {"library_read", "library_erase", "library_program"}),
    }
    ram_veneers = {}
    q23_veneer = "cr_v_bb_drum808_render_q23"
    if record.get("drum_render_in_ram"):
        renderer = "bb_drum808_render_q23" if record.get("drum_render_q23") else "bb_drum808_render_dry"
        ram_veneers = {
            f"cr_v_{renderer}": renderer,
            "cr_v_bd_reset": "bd_reset",
            "cr_v_env_arm": "env_arm",
        }
    if record.get("dillo_render_in_ram"):
        ram_veneers["elite_far_bass303_run"] = "bass303_run"
    indirect_targets.update({name: (1, {target}) for name, target in ram_veneers.items()})

    functions = {}
    for file in sorted(build.glob("*.s")):
        for match in re.finditer(
            r"^\s*\.type\s+([^,]+),@function\s*\n(.*?)^\s*\.size\s+\1,",
            file.read_text(), re.M | re.S,
        ):
            name, body = match.groups()
            require(name not in functions, f"Ambiguous assembly symbol: {name}")
            functions[name] = (file.name, body)

    addresses, sizes = {}, {}
    for line in (build / "symbols.txt").read_text().splitlines():
        fields = line.split()
        if len(fields) < 4 or not re.fullmatch(r"[0-9a-fA-F]+", fields[0]):
            continue
        address, name = int(fields[0], 16), fields[-1]
        if "F" in fields:
            require(name not in addresses, f"Ambiguous linked function: {name}")
            addresses[name] = address
            sizes[name] = int(fields[-2], 16)

    instructions = {}
    for line in (build / "linked.dis").read_text().splitlines():
        match = re.match(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} +)+\t+(.*)", line)
        if match:
            text = match[2].strip()
            if text != "}":
                instructions[int(match[1], 16)] = text
    machine_addresses = sorted(instructions)

    graph = {}
    def visit(name):
        if name in graph:
            return
        require(name in functions and name in addresses, f"Missing linked function: {name}")
        file, body = functions[name]
        lines = asm_instructions(body)
        entry = prefix(lines)
        require([size for line in lines if (size := stack_allocation(line))] == entry,
                f"Stack allocation outside prologue: {name}")
        address = addresses[name]
        require(address in instructions, f"No linked disassembly at entry of {name}")
        index = machine_addresses.index(address)
        machine = [instructions[item] for item in machine_addresses[index:index + len(entry)]]
        require([stack_allocation(line) for line in machine] == entry,
                f"Linked prologue does not match assembly: {name}")

        local_labels = set(re.findall(r"^(\.[\w.$]+):", body, re.M))
        callees = set()
        indirect = Counter()
        for line in lines:
            if re.search(r"\bsp\s*(?:=|-=|\+=)", line):
                require(re.fullmatch(r"sp \+= -?\d+", line) is not None,
                        f"Dynamic or unknown stack change in {name}: {line}")
            if "[--sp]" in line:
                require(stack_allocation(line) > 0, f"Unrecognised stack push in {name}: {line}")
            if re.search(r"\bpc\s*=", line):
                require(line == "pc = [sp++]", f"Unresolved PC assignment in {name}: {line}")
            for match in re.finditer(r"\b(call|goto)\s+([\w.$]+)", line):
                opcode, target = match.groups()
                if target.startswith("."):
                    require(opcode == "goto" and target in local_labels,
                            f"Branch leaves function {name}: {line}")
                elif re.fullmatch(r"r\d+", target):
                    expected_opcode = "goto" if name in ram_veneers and name != q23_veneer else "call"
                    require(name in indirect_targets and opcode == expected_opcode,
                            f"Unresolved indirect branch in {name}: {line}")
                    indirect[target] += 1
                else:
                    callees.add(target)

        if indirect:
            expected, targets = indirect_targets[name]
            require(sum(indirect.values()) == expected, f"Unexpected indirect calls in {name}: {indirect}")
            callees.update(targets)
        if name in ram_veneers:
            target = addresses[ram_veneers[name]]
            linked = [instructions[item] for item in machine_addresses[index:] if item < address + sizes[name]]
            register = next(iter(indirect), None)
            # The four-argument Q23 veneer uses a callee-saved register for
            # the far target, so the compiler emits a normal call and return.
            if name == q23_veneer:
                require(len(linked) == 4 and entry == [8] and linked[0] == f"[--sp] = {{rets, {register}}}" and
                        linked[3] == f"{{pc, {register}}} = [sp++]",
                        f"Unexpected linked Q23 veneer frame in {name}")
                linked = linked[1:3]
                branch = f"call {register}"
            else:
                require(not entry, f"Unexpected stack frame in {name}")
                branch = f"goto {register}"
            load = re.fullmatch(rf"{register} = (\d+)(?: <.*>)?", linked[0]) if len(linked) == 2 else None
            require(load and int(load[1]) == target and linked[1] == branch and
                    0x01C00000 <= target < 0x01C00000 + record["regions"]["ramtext"][0],
                    f"Unbound linked RAM call in {name}")

        table_branches = sum(bool(re.search(r"\b(?:tbb|tbh)\b", line)) for line in lines)
        table_targets = re.findall(r"^\s*\.(?:byte|short|long)\s+\(?(\.LBB\w+)\s*-", body, re.M)
        if table_branches:
            require(table_targets and set(table_targets) <= local_labels,
                    f"Unbound compiler jump table in {name}")
        graph[name] = {
            "assembly": file, "address": hex(address), "frame_bytes": sum(entry),
            "callees": sorted(callees), "indirect_calls": dict(indirect),
            "local_jump_tables": table_branches,
        }
        for callee in sorted(callees):
            visit(callee)

    def peak(name, path=()):
        require(name not in path, f"Recursive call chain: {path + (name,)}")
        children = [peak(child, path + (name,)) for child in graph[name]["callees"]]
        size, chain = max(children, default=(0, []))
        return graph[name]["frame_bytes"] + size, [name] + chain

    roots = ("fm1_cstart", "fm1_alnk0_irq", "fm1_timer5_irq", "fm1_fault_c")
    stacks = {}
    for name in roots:
        visit(name)
        size, chain = peak(name)
        stacks[name] = {
            "c_bytes": size,
            "chain": [{"function": node, "frame_bytes": graph[node]["frame_bytes"]} for node in chain],
        }

    # These wrapper sizes follow the exact pinned fm1_isr.S and fm1_vec.S
    # register lists. Sum the audio, scanner and fault paths conservatively.
    normal_irq = 2 * 28 + stacks["fm1_alnk0_irq"]["c_bytes"] + stacks["fm1_timer5_irq"]["c_bytes"]
    fault_irq = normal_irq + 104 + stacks["fm1_fault_c"]["c_bytes"]
    fg = stacks["fm1_cstart"]["c_bytes"]
    regions = record["regions"]
    require(fault_irq < regions["interrupt_stack"], "IRQ stack overflow")
    require(fg < regions["foreground_stack"], "Foreground stack overflow")
    return {
        "elf_sha256": record["sha256"]["elite-review.elf"],
        "scope": "Static linked C and entry-wrapper frames. Actual high-water marks and timing need a unit.",
        "foreground": {"used": fg, "reserved": regions["foreground_stack"],
                       "remaining": regions["foreground_stack"] - fg},
        "normal_nested_irq": {"used": normal_irq, "reserved": regions["interrupt_stack"],
                              "remaining": regions["interrupt_stack"] - normal_irq},
        "nested_irq_with_fault": {"used": fault_irq, "reserved": regions["interrupt_stack"],
                                  "remaining": regions["interrupt_stack"] - fault_irq},
        "roots": stacks, "functions": graph,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    args = parser.parse_args()
    report = audit(args.build)
    (args.build / "stack-check.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k != "functions"}, indent=2))
