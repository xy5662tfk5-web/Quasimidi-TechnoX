#!/usr/bin/env python3
"""Parse Technox.ins into structured preset and controller metadata."""
from __future__ import annotations
import argparse, json, re
from pathlib import Path

def parse(path: Path):
    sections = {}
    current = None
    for raw in path.read_text(encoding="latin-1").splitlines():
        line = raw.strip()
        if not line or line.startswith((";", ".")):
            continue
        match = re.fullmatch(r"\[([^\]]+)\]", line)
        if match:
            current = match.group(1)
            sections[current] = {}
            continue
        if "=" in line and current:
            key, value = line.split("=", 1)
            sections[current][key.strip()] = value.strip()
    banks = []
    for letter in "ABCD":
        section = sections.get(f"Bank {letter} Technox", {})
        programs = [{"program": n, "name": section.get(str(n), "")} for n in range(128)]
        if any(not p["name"] for p in programs):
            raise ValueError(f"Bank {letter} is missing one or more program names")
        banks.append({"bank": letter, "programs": programs})
    ch10 = sections.get("Ch 10 Technox", {})
    drum_kits = [{"program": n, "name": ch10.get(str(n), "")} for n in range(32)]
    if any(not p["name"] for p in drum_kits):
        raise ValueError("Channel 10 drum-kit table is incomplete")
    controllers = [{"cc": int(k), "name": v} for k, v in sections.get("Technox Controllers", {}).items()
                   if k.isdigit()]
    controllers.sort(key=lambda x: x["cc"])
    instrument = sections.get("Technox", {})
    drum_assignments = []
    for key, value in instrument.items():
        match = re.fullmatch(r"Key\[(\d+),(\d+)\]", key)
        if match:
            drum_assignments.append({"bank": int(match.group(1)), "program": int(match.group(2)), "map": value})
    return {
        "format": "Quasimidi TechnoX Cakewalk/Sonar instrument definition",
        "source": path.name,
        "program_banks": banks,
        "channel_10_drum_kits": drum_kits,
        "controllers": controllers,
        "drum_map_sections": sorted({x["map"] for x in drum_assignments}),
        "drum_map_program_assignments": sorted(drum_assignments, key=lambda x: (x["bank"], x["program"])),
        "bank_select_method": instrument.get("BankSelMethod", "")
    }

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input", type=Path)
    ap.add_argument("-o", "--output", type=Path, required=True)
    args = ap.parse_args()
    obj = parse(args.input)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"OK: {sum(len(b['programs']) for b in obj['program_banks'])} programs; "
          f"{len(obj['channel_10_drum_kits'])} drum kits; {len(obj['controllers'])} named controllers; "
          f"{len(obj['drum_map_sections'])} drum maps")

if __name__ == "__main__":
    main()
