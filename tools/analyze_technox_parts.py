#!/usr/bin/env python3
"""Describe observed values in TechnoX Part SysEx bytes without guessing semantics."""
from __future__ import annotations
import argparse, json
from collections import Counter
from pathlib import Path
from parse_technox_mid import decode_bank

def analyze(midi_bytes: bytes):
    performances = decode_bank(midi_bytes)
    report = {
        "source": "TECHNOX.MID",
        "performances": len(performances),
        "part_frames": len(performances) * 4,
        "frame_layout": {
            "complete_frame_bytes": 33,
            "header_hex": "F0 3F 00 22 44",
            "type_byte_offset": 6,
            "performance_index_offset": 5,
            "opaque_data_start_offset": 8,
            "reserved_byte_offset": 7,\n            "opaque_data_bytes": 24,
            "terminator_offset": 32
        },
        "parts": []
    }
    for part_number in range(4):
        frames = [p.part_frames[part_number] for p in performances]
        offsets = []
        for data_index in range(24):
            absolute_offset = 8 + data_index
            values = [frame[absolute_offset] for frame in frames]
            counts = Counter(values)
            offsets.append({
                "data_index": data_index,
                "frame_offset": absolute_offset,
                "unique_values": len(counts),
                "min": min(values),
                "max": max(values),
                "constant_value": values[0] if len(counts) == 1 else None,
                "frequency": [{"value": value, "count": count}
                              for value, count in counts.most_common()]
            })
        report["parts"].append({
            "part_number": part_number + 1,
            "sysex_type": part_number + 1,
            "performance_count": len(frames),
            "byte_statistics": offsets
        })
    return report

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("midi", type=Path)
    ap.add_argument("-o", "--output", type=Path)
    args = ap.parse_args()
    report = analyze(args.midi.read_bytes())
    rendered = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")

if __name__ == "__main__":
    main()
