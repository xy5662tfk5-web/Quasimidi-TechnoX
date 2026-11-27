#!/usr/bin/env python3
"""Dump all firmware-visible IC51 descriptors and command streams as JSONL.

This is structural extraction only; it does not decode PCM or identify the
external sound processor's command meanings.
"""
import argparse
import json
from collections import Counter
from pathlib import Path

BANK = 0x10000
IMAGE = 2 * BANK


def descriptor(data, address):
    if address + 8 > BANK:
        raise ValueError(f"descriptor out of range: {address:#06x}")
    fixed = data[address:address + 8]
    pos = address + 8
    groups = []
    for _ in range(6):
        group = data[pos]
        pos += 1
        if group >= 5:
            break
        entries = []
        for _ in range(256):
            selector, a, b = data[pos:pos + 3]
            pos += 3
            entries.append([selector, a, b])
            if selector == 0x7f:
                break
        else:
            raise ValueError("unterminated Table A group")
        groups.append({"id": group, "entries": entries})
    else:
        raise ValueError("unterminated Table A")
    table_b = []
    for _ in range(256):
        key = data[pos]
        pos += 1
        if key >= 6:
            break
        table_b.append([key, data[pos], data[pos + 1]])
        pos += 2
    else:
        raise ValueError("unterminated Table B")
    stream = data[pos] | (data[pos + 1] << 8)
    return {"fixed_header": fixed.hex(), "table_a": groups,
            "table_a_end": group, "table_b": table_b,
            "table_b_end": key, "stream_pointer": stream}


def command_stream(data, pointer, flag_0a):
    pos, commands = pointer, []
    state = "slot_limit"
    while len(commands) < 16:
        opcode = data[pos]
        if opcode == 0xff:
            state = "terminator"
            break
        group = opcode & 0x38
        if group == 0x00:
            length, kind = 4, "conditional"
            if not flag_0a and data[pos + 3] & 8:
                length = 7
        elif group == 0x08:
            length, kind = 7, "pitch"
        elif group == 0x10:
            length = 7
            kind = "empty_multi" if data[pos + 1] == 0 else "multi"
        elif group == 0x18:
            length, kind = 4, "four_byte"
        elif group == 0x20:
            length, kind = 4, "terminal"
        elif group == 0x38:
            length, kind = 1, "slot_marker"
        else:
            length, kind = 1, "single_byte"
        raw = data[pos:pos + length]
        if len(raw) != length:
            raise ValueError("stream crosses bank boundary")
        commands.append({"offset": pos, "group": group,
                         "kind": kind, "bytes": raw.hex()})
        pos += length
        if group == 0x20:
            state = "terminal"
            break
    filler = 16 - len(commands) if state == "terminal" and not flag_0a else 0
    return {"commands": commands, "end_state": state,
            "slots_consumed": len(commands), "filler_slots": filler,
            "output_slots": len(commands) + filler, "bytes_consumed": pos-pointer}


def extract(image, flag_0a):
    if len(image) != IMAGE:
        raise ValueError(f"IC51 image must be 0x20000 bytes, got {len(image):#x}")
    resources, descriptor_sets = [], [set(), set()]
    for program_bank in range(4):
        ic51_bank = program_bank // 2
        half = program_bank & 1
        data = image[ic51_bank * BANK:(ic51_bank + 1) * BANK]
        for sound in range(128):
            index = sound + (0x80 if half else 0)
            address = (data[2*index] << 8) | data[2*index+1]
            if address in descriptor_sets[ic51_bank]:
                raise ValueError(f"duplicate descriptor {address:#06x} in IC51 bank {ic51_bank}")
            descriptor_sets[ic51_bank].add(address)
            desc = descriptor(data, address)
            stream = command_stream(data, desc["stream_pointer"], flag_0a)
            resources.append({"program_bank": program_bank, "sound": sound,
                "ic51_bank": ic51_bank, "pointer_index": index,
                "descriptor_address": address, **desc, "stream": stream})
    if [len(s) for s in descriptor_sets] != [256, 256]:
        raise ValueError("expected 256 unique descriptors in each IC51 bank")
    return resources


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("rom", type=Path)
    ap.add_argument("-o", "--output", type=Path, default=Path("technox_ic51_resources.jsonl"))
    ap.add_argument("--flag-0a-set", action="store_true")
    ap.add_argument("--show", metavar="BANK:SOUND")
    args = ap.parse_args()
    resources = extract(args.rom.read_bytes(), args.flag_0a_set)
    with args.output.open("w", encoding="utf-8") as out:
        for item in resources:
            out.write(json.dumps(item, separators=(",", ":")) + "\n")
    kinds = Counter(c["kind"] for r in resources for c in r["stream"]["commands"])
    endings = Counter(r["stream"]["end_state"] for r in resources)
    print(f"Extracted {len(resources)} programs to {args.output}")
    print("Unique descriptors per IC51 bank:", *(len(s) for s in
          [{r["descriptor_address"] for r in resources if r["ic51_bank"] == b}
           for b in range(2)]))
    print("Command types:", dict(sorted(kinds.items())))
    print("Stream endings:", dict(endings))
    if args.show:
        bank, sound = (int(v, 0) for v in args.show.split(":"))
        item = next((r for r in resources
                     if r["program_bank"] == bank and r["sound"] == sound), None)
        if item is None:
            raise SystemExit("bank must be 0..3; sound must be 0..127")
        print(json.dumps(item, indent=2))


if __name__ == "__main__":
    main()
