#!/usr/bin/env python3
"""Audit Quasimidi TechnoX v1.05 ROM dumps against MAME and descramble IC4.

Usage:
  python3 tools/rom_audit.py technox_IC4_mx27c1000_v105.bin technox_IC51_nm27c010_v105.bin

The script never modifies its input files. It writes IC4_descrambled.bin beside IC4.
"""
from __future__ import annotations
import argparse
import hashlib
import math
import zlib
from collections import Counter
from pathlib import Path

EXPECTED = {
    "IC4": {
        "size": 0x20000,
        "crc32": "4d393cd9",
        "sha1": "9ee853f1f2e88fbfc86589c4d0f91003d0ff146f",
    },
    "IC51": {
        "size": 0x20000,
        "crc32": "d669570e",
        "sha1": "0645a1b022ed25a836152933e6df32da4448f49a",
    },
}

def bitswap8(value: int) -> int:
    # MAME bitswap<8>(offset, 1, 2, 3, 6, 5, 7, 1, 0)
    result = 0
    for bit in (1, 2, 3, 6, 5, 7, 1, 0):
        result = (result << 1) | ((value >> bit) & 1)
    return result

def descramble_ic4(data: bytes) -> bytes:
    if len(data) != EXPECTED["IC4"]["size"]:
        raise ValueError(f"IC4 must be 0x20000 bytes, got 0x{len(data):x}")
    out = bytearray(len(data))
    for base in range(0, len(data), 0x100):
        block = data[base:base + 0x100]
        for offset in range(0x100):
            source = bitswap8(offset) ^ 0x40 ^ ((offset & 0x11) << 3)
            out[base + offset] = block[source]
    return bytes(out)

def report(label: str, path: Path, data: bytes) -> bool:
    expected = EXPECTED[label]
    crc = f"{zlib.crc32(data) & 0xffffffff:08x}"
    sha = hashlib.sha1(data).hexdigest()
    print(f"{label}: {path}")
    print(f"  size:  {len(data)} bytes (expected {expected['size']})")
    print(f"  CRC32: {crc} (expected {expected['crc32']})")
    print(f"  SHA1:  {sha} (expected {expected['sha1']})")
    ok = len(data) == expected["size"] and crc == expected["crc32"] and sha == expected["sha1"]
    print(f"  result: {'PASS' if ok else 'FAIL'}")
    return ok

def entropy(data: bytes) -> float:
    counts = Counter(data)
    total = len(data)
    return -sum((n / total) * math.log2(n / total) for n in counts.values())

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("ic4", type=Path)
    parser.add_argument("ic51", type=Path)
    args = parser.parse_args()
    ic4, ic51 = args.ic4.read_bytes(), args.ic51.read_bytes()
    ok4 = report("IC4", args.ic4, ic4)
    ok51 = report("IC51", args.ic51, ic51)

    decoded = descramble_ic4(ic4)
    out_path = args.ic4.with_name("IC4_descrambled.bin")
    out_path.write_bytes(decoded)
    print(f"IC4 descrambled copy: {out_path}")
    print(f"  reset vector bytes: {decoded[:3].hex(' ')}")
    print(f"  first 32 bytes:     {decoded[:32].hex(' ')}")

    counts = Counter(ic51)
    print("IC51 preliminary byte statistics (not a codec identification):")
    print(f"  Shannon entropy: {entropy(ic51):.4f} bits/byte")
    print(f"  unique byte values: {len(counts)}/256")
    print(f"  zero bytes: {counts[0]} ({counts[0] / len(ic51):.1%})")
    print(f"  0xFF bytes: {counts[255]} ({counts[255] / len(ic51):.1%})")
    print("  first 64 bytes:", ic51[:64].hex(" "))
    return 0 if ok4 and ok51 else 1

if __name__ == "__main__":
    raise SystemExit(main())
