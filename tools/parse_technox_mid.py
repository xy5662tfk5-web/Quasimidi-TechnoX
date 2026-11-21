#!/usr/bin/env python3
"""Lossless extractor for the Quasimidi TechnoX 50-Performance SMF dump."""
from __future__ import annotations
import argparse, json, struct, sys
from dataclasses import dataclass
from pathlib import Path
from typing import List

HEADER = bytes((0xF0, 0x3F, 0x00, 0x22, 0x44))
EXPECTED_BLOCK_SELECTORS = (0x05, 0x00, 0x01, 0x02, 0x03, 0x04)
EXPECTED_SIZES = {0x05: 17, 0x00: 74, 0x01: 33, 0x02: 33, 0x03: 33, 0x04: 33}

class MidiFormatError(ValueError): pass

@dataclass
class Frame:
    raw: bytes
    track: int
    event_offset: int
    @property
    def record_index(self): return self.raw[5] if len(self.raw) > 5 else None
    @property
    def block_type(self): return self.raw[6] if len(self.raw) > 6 else None

@dataclass
class Performance:
    index: int
    name: str
    name_frame: bytes
    common_frame: bytes
    part_frames: List[bytes]

def _read_vlq(data: bytes, pos: int):
    value = 0
    for _ in range(4):
        if pos >= len(data): raise MidiFormatError("truncated variable-length quantity")
        b = data[pos]; pos += 1
        value = (value << 7) | (b & 0x7F)
        if not (b & 0x80): return value, pos
    raise MidiFormatError("VLQ exceeds four bytes")

def _chunks(data: bytes):
    if len(data) < 14 or data[:4] != b"MThd": raise MidiFormatError("missing MThd header")
    hlen = int.from_bytes(data[4:8], "big")
    if hlen < 6 or len(data) < 8 + hlen: raise MidiFormatError("invalid MIDI header length")
    fmt, ntracks, division = struct.unpack(">HHH", data[8:14])
    if fmt not in (0, 1, 2): raise MidiFormatError(f"unsupported SMF format {fmt}")
    pos = 8 + hlen
    tracks = []
    for ti in range(ntracks):
        if data[pos:pos+4] != b"MTrk": raise MidiFormatError(f"missing MTrk chunk {ti}")
        size = int.from_bytes(data[pos+4:pos+8], "big"); start = pos + 8; end = start + size
        if end > len(data): raise MidiFormatError(f"truncated track {ti}")
        tracks.append((ti, data[start:end])); pos = end
    return fmt, division, tracks

def extract_sysex(data: bytes) -> List[Frame]:
    _, _, tracks = _chunks(data)
    frames: List[Frame] = []
    for ti, tr in tracks:
        p = 0; running = None; pending = None; pending_offset = 0
        while p < len(tr):
            _, p = _read_vlq(tr, p)
            if p >= len(tr): raise MidiFormatError("missing event status")
            status = tr[p]
            has_status_byte = status >= 0x80
            if not has_status_byte:
                if running is None: raise MidiFormatError("running status without prior channel status")
                status = running
            else: p += 1
            if status == 0xFF:
                if p >= len(tr): raise MidiFormatError("truncated meta event")
                p += 1; n, p = _read_vlq(tr, p); p += n
                if p > len(tr): raise MidiFormatError("truncated meta payload")
                running = None
            elif status in (0xF0, 0xF7):
                n, p = _read_vlq(tr, p); payload = tr[p:p+n]; p += n
                if len(payload) != n: raise MidiFormatError("truncated SysEx event")
                if status == 0xF0:
                    if pending is not None: raise MidiFormatError("new F0 before previous SysEx completed")
                    pending = bytearray(b"\xF0" + payload); pending_offset = p - n
                else:
                    if pending is None:
                        if payload.startswith(b"\xF0"): pending = bytearray(payload)
                        else: continue
                    else: pending.extend(payload)
                if pending is not None and pending.endswith(b"\xF7"):
                    frames.append(Frame(bytes(pending), ti, pending_offset)); pending = None
                running = None
            elif 0x80 <= status <= 0xEF:
                running = status
                n = 1 if (status & 0xF0) in (0xC0, 0xD0) else 2
                p += n
                if p > len(tr): raise MidiFormatError("truncated channel event")
            else:
                raise MidiFormatError(f"unsupported system status 0x{status:02X}")
        if pending is not None: raise MidiFormatError(f"unterminated SysEx in track {ti}")
    return frames

def decode_bank(data: bytes, strict: bool = True) -> List[Performance]:
    frames = extract_sysex(data)
    if strict and len(frames) != 300: raise MidiFormatError(f"expected 300 SysEx frames, found {len(frames)}")
    if len(frames) % 6: raise MidiFormatError("SysEx frame count is not divisible by six")
    result = []
    for base in range(0, len(frames), 6):
        group = frames[base:base+6]
        if any(not f.raw.startswith(HEADER) or not f.raw.endswith(b"\xF7") for f in group):
            raise MidiFormatError(f"invalid TechnoX SysEx envelope at group {base//6}")
        selectors = tuple(f.block_type for f in group)
        if selectors != EXPECTED_BLOCK_SELECTORS: raise MidiFormatError(f"unexpected block selector sequence {selectors} at group {base//6}")
        indexes = [f.record_index for f in group]
        if len(set(indexes)) != 1: raise MidiFormatError(f"record index mismatch at group {base//6}")
        for f in group:
            if strict and len(f.raw) != EXPECTED_SIZES[f.block_type]:
                raise MidiFormatError(f"unexpected size {len(f.raw)} for block {f.block_type:#04x}")
            if any(x > 0x7F for x in f.raw[1:-1]): raise MidiFormatError("non-7-bit data byte in SysEx payload")
        name = group[0].raw[8:16].decode("ascii", errors="replace").rstrip(" \x00")
        result.append(Performance(indexes[0], name, group[0].raw, group[1].raw,
                                  [f.raw for f in group[2:6]]))
    if strict:
        if len(result) != 50: raise MidiFormatError(f"expected 50 Performances, found {len(result)}")
        if [p.index for p in result] != list(range(5, 55)): raise MidiFormatError("Performance indexes are not 5..54")
    return result

def export_json(perfs: List[Performance], path: Path):
    obj = {"format":"Quasimidi TechnoX Performance bank", "lossless":True,
           "note":"Undocumented bytes remain opaque and are preserved as complete SysEx frames.",
           "performances":[{"index":p.index,"name":p.name,"name_frame_hex":p.name_frame.hex(" "),
             "common_frame_hex":p.common_frame.hex(" "),
             "parts":[{"part":i+13,"block_selector":i+1,"raw_sysex_hex":raw.hex(" ")} for i,raw in enumerate(p.part_frames)]}
             for p in perfs]}
    path.write_text(json.dumps(obj, indent=2, ensure_ascii=False)+"\n", encoding="utf-8")

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("midi", type=Path)
    ap.add_argument("-o","--output", type=Path)
    ap.add_argument("--allow-nonstandard", action="store_true")
    args=ap.parse_args()
    try:
        perfs=decode_bank(args.midi.read_bytes(), strict=not args.allow_nonstandard)
        if args.output: export_json(perfs,args.output)
        print(f"OK: {len(perfs)} Performances; {sum(5 for _ in perfs)} blocks per record")
        for p in perfs: print(f"{p.index:02d}  {p.name}")
    except (OSError, MidiFormatError) as e:
        print(f"ERROR: {e}", file=sys.stderr); return 1
    return 0
if __name__ == "__main__": raise SystemExit(main())
