#!/usr/bin/env python3
"""Summarize documented TechnoX Part parameters and observed SysEx byte distributions."""
from __future__ import annotations
import argparse, json
from collections import Counter
from pathlib import Path
from parse_technox_mid import decode_bank

PARAMETERS = [
    ("Bank number", 0, 3, 0), ("Sound number", 0, 127, 0),
    ("Track mode", 0, 3, 0), ("Level", 0, 127, 0),
    ("Panorama", 0, 20, 0), ("FX1 send", 0, 63, 0), ("FX2 send", 0, 63, 0),
    ("Transpose", 0, 48, -24), ("Tune", 0, 127, -64),
    ("Cutoff frequency", 0, 127, -64), ("Resonance frequency", 0, 127, -64),
    ("EG attack", 0, 127, -64), ("EG decay", 0, 127, -64),
    ("EG release", 0, 127, -64), ("Vibrato rate", 0, 127, -64),
    ("Vibrato depth", 0, 127, -64), ("Vibrato delay", 0, 127, -64),
    ("Velocity curve", 0, 14, 0), ("Hold pedal", 0, 1, 0),
    ("Modulation depth", 0, 127, 0), ("Pitch sensitivity", 0, 24, -12),
    ("Volume modulation sensitivity", 0, 127, -64),
    ("Tone modulation sensitivity", 0, 127, -64), ("Portamento time", 0, 127, 0)
]

def analyze(midi_bytes: bytes):
    performances = decode_bank(midi_bytes)
    report = {
        "source": "TECHNOX.MID",
        "performances": len(performances),
        "part_frames": len(performances) * 4,
        "frame_layout": {
            "complete_frame_bytes": 33,
            "header_hex": "F0 3F 00 22 44",
            "performance_address_high_offset": 5,
            "block_selector_offset": 6,
            "reserved_byte_offset": 7,
            "part_parameter_data_start_offset": 8,
            "part_parameter_count": 24,
            "terminator_offset": 32
        },
        "parts": []
    }
    for part_slot in range(4):
        frames = [p.part_frames[part_slot] for p in performances]
        offsets = []
        for data_index, (name, minimum, maximum, display_offset) in enumerate(PARAMETERS):
            absolute_offset = 8 + data_index
            values = [frame[absolute_offset] for frame in frames]
            counts = Counter(values)
            offsets.append({
                "parameter_offset": data_index,
                "frame_offset": absolute_offset,
                "parameter": name,
                "documented_minimum": minimum,
                "documented_maximum": maximum,
                "display_offset": display_offset,
                "unique_values": len(counts),
                "observed_min": min(values),
                "observed_max": max(values),
                "constant_value": values[0] if len(counts) == 1 else None,
                "out_of_documented_range_count": sum(v < minimum or v > maximum for v in values),
                "frequency": [{"value": value, "count": count}
                              for value, count in counts.most_common()]
            })
        report["parts"].append({
            "part_number": part_slot + 13,
            "block_selector": part_slot + 1,
            "performance_count": len(frames),
            "parameter_statistics": offsets
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
