#!/usr/bin/env python3
"""Audit the exact assembly passed to the PI32 assembler and its linked entry frames."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
ROOTS = ("elite_pi32_block", "elite_pi32_configure", "elite_pi32_start", "elite_pi32_stop")

def require(condition, message):
    if not condition:
        raise ValueError(message)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def asm_instructions(body):
    lines = (line.split(";", 1)[0].strip() for line in body.splitlines())
    return [line for line in lines if line and not line.startswith(".")
            and not line.endswith(":") and line not in ("{", "}")]

def stack_allocation(line):
    single = re.fullmatch(r"\[--sp\]\s*=\s*(rets|r\d+)", line)
    if single:
        return 4
    group = re.fullmatch(r"\[--sp\]\s*=\s*\{([^}]+)\}", line)
    if group:
        words = 0
        for item in group[1].split(","):
            item = item.strip()
            if re.fullmatch(r"rets|r\d+", item):
                words += 1
            else:
                registers = re.fullmatch(r"r(\d+)-r(\d+)", item)
                require(registers is not None, f"Unknown pushed registers: {line}")
                words += abs(int(registers[1]) - int(registers[2])) + 1
        return 4 * words
    immediate = re.fullmatch(r"sp\s*\+=\s*-(\d+)", line)
    return int(immediate[1]) if immediate else 0

def prefix(instructions):
    frame = []
    for line in instructions:
        if not stack_allocation(line):
            break
        frame.append(stack_allocation(line))
    return frame

def audit(build):
    record = json.loads((build / "build.json").read_text())
    for key, base in [("units_sha256", ROOT), ("object_sha256", build), ("assembly_sha256", build)]:
        for path, digest in record[key].items():
            require(sha(base / path) == digest, f"Build input drift: {path}")
    for key, base in [("inputs_sha256", ROOT), ("evidence_sha256", build)]:
        for path, digest in record[key].items():
            require(sha(base / path) == digest, f"Build evidence drift: {path}")
    require(sha(ROOT / "pi32/probe.ld") == record["linker_script_sha256"], "Linker script drift")
    require(sha(build / "elite-probe.elf") == record["elf_sha256"], "Linked ELF drift")
    require(sha(build / "linked.dis") == record["linked_dis_sha256"], "Disassembly drift")
    require(not (build / "unresolved.txt").read_text().strip(), "Unresolved symbols")
    require("file format ELF32-pi32v2" in (build / "sections.txt").read_text(), "Wrong ISA")

    # These source bindings restrict the only function pointers in the graph.
    bindings = {
        "pi32/probe.c": "elite_render(&state, output, FM1_BLOCK_FRAMES)",
        "elite.c": "bb_process_render(&state->clock, frames, trigger_drum, trigger_note, render_span, state);",
        "vendor/bb/bb_core.c": "bb_process_render_hook(c,frames,hit,note,render,NULL,ctx);",
    }
    for path, binding in bindings.items():
        require(binding in (ROOT / path).read_text(), f"Missing callback binding: {path}")

    functions = {}
    for file in sorted(build.glob("*.s")):
        for match in re.finditer(
            r"^\s*\.type\s+([^,]+),@function\s*\n(.*?)^\s*\.size\s+\1,",
            file.read_text(), re.M | re.S,
        ):
            name, body = match.groups()
            require(name not in functions, f"Ambiguous assembly symbol: {name}")
            functions[name] = (file.name, body)

    addresses = {}
    symbols = {}
    for line in (build / "symbols.txt").read_text().splitlines():
        fields = line.split()
        if len(fields) < 4 or not re.fullmatch(r"[0-9a-fA-F]+", fields[0]):
            continue
        address, name = int(fields[0], 16), fields[-1]
        if "F" in fields:
            require(name not in addresses, f"Ambiguous linked function: {name}")
            addresses[name] = address
        if name.startswith("__") or name in ("state", "output"):
            symbols[name] = {"address": address, "bytes": int(fields[-2], 16)}

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
                    require(name == "bb_process_render_hook" and opcode == "call",
                            f"Unresolved indirect branch in {name}: {line}")
                    indirect[target] += 1
                else:
                    callees.add(target)

        if indirect:
            require(indirect == {"r2": 3, "r6": 1, "r10": 2},
                    f"Unexpected sequencer callback sites: {indirect}")
            # Inspect conservatively as the union of the three actual bindings.
            # bb_process_render passes zero for the optional before callback.
            callees.update(("trigger_drum", "trigger_note", "render_span"))

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

    stacks = {}
    for name in ROOTS:
        visit(name)
        used, chain = peak(name)
        kind = "audio" if name == "elite_pi32_block" else "control"
        reserved = symbols[f"__{kind}_stack_top"]["address"] - symbols[f"__{kind}_stack_bottom"]["address"]
        require(used < reserved, f"Stack overflow in {name}")
        stacks[name] = {
            "c_chain_bytes": used, "budget_bytes": reserved, "remaining_bytes": reserved - used,
            "chain": [{"function": f, "frame_bytes": graph[f]["frame_bytes"]} for f in chain],
        }
    require(set(graph) == set(addresses), "Some linked functions have no audited call chain")
    budgets = {}
    for label, begin in [("app", 0x02000120), ("ram", 0x01C08000), ("pool", 0x01C20000)]:
        limit = symbols[f"__{label}_limit"]["address"]
        used_end = symbols[f"__{label}_used_end"]["address"]
        require(begin <= used_end <= limit, f"Link exceeds {label} budget")
        budgets[label] = {
            "origin": hex(begin), "budget_bytes": limit - begin,
            "used_bytes": used_end - begin, "remaining_bytes": limit - used_end,
        }
    return {
        "elf_sha256": record["elf_sha256"],
        "scope": "Linked probe C call chains. Board startup, IRQ entry, nested IRQs and watermarks are excluded.",
        "linked_functions_audited": len(graph),
        "memory": budgets, "stacks": stacks, "functions": graph,
        "state_bytes": symbols["state"]["bytes"], "pcm_bytes": symbols["output"]["bytes"],
    }

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    args = parser.parse_args()
    result = audit(args.build)
    (args.build / "audit.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k != "functions"}, indent=2))

if __name__ == "__main__":
    main()
