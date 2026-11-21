#!/usr/bin/env python3
"""Evidence-first analysis of the Quasimidi TechnoX v1.05 ROM images.

The IC4 transform exactly mirrors MAME's init_quasar() program-ROM address
descrambler. IC51 is inspected as raw bytes only; it is never transformed.
This is reconnaissance, not a complete 8051 disassembler or sample decoder.
"""
from __future__ import annotations

import argparse
import hashlib
import zlib
from pathlib import Path

EXPECTED = {
    "ic4": {"size": 0x20000, "crc32": "4d393cd9",
            "sha1": "9ee853f1e2f88fbfc86589c4d0f91003d0ff146f"},
    "ic51": {"size": 0x20000, "crc32": "d669570e",
             "sha1": "0645a1b022ed25a836152933e6df32da4448f49a"},
}
VECTORS = (0x0000, 0x0003, 0x000B, 0x0013, 0x001B, 0x0023, 0x002B)


def bitswap8(value: int, order: tuple[int, ...]) -> int:
    result = 0
    for bit in order:
        result = (result << 1) | ((value >> bit) & 1)
    return result


def descramble_ic4(raw: bytes) -> bytes:
    if len(raw) != 0x20000:
        raise ValueError(f"IC4 must be 0x20000 bytes, got 0x{len(raw):X}")
    result = bytearray(len(raw))
    # MAME: bitswap<8>(offset, 1,2,3,6,5,7,1,0) ^ 0x40 ^ ((offset & 0x11) << 3)
    order = (1, 2, 3, 6, 5, 7, 1, 0)
    for base in range(0, len(raw), 0x100):
        block = raw[base:base + 0x100]
        for offset in range(0x100):
            source = bitswap8(offset, order) ^ 0x40 ^ ((offset & 0x11) << 3)
            result[base + offset] = block[source]
    return bytes(result)


def report(label: str, data: bytes) -> None:
    crc = f"{zlib.crc32(data) & 0xffffffff:08x}"
    sha1 = hashlib.sha1(data).hexdigest()
    expected = EXPECTED[label]
    size_ok = len(data) == expected["size"]
    crc_ok = crc == expected["crc32"]
    sha1_ok = sha1 == expected["sha1"]
    print(f"{label.upper()}: size=0x{len(data):X} CRC32={crc} SHA1={sha1}")
    print(f"  checks: size={'OK' if size_ok else 'FAIL'} crc32={'OK' if crc_ok else 'FAIL'} sha1={'OK' if sha1_ok else 'FAIL'}")
    if not sha1_ok:
        print(f"  SHA1 debug: actual={sha1!r} expected={expected['sha1']!r} actual_len={len(sha1)} expected_len={len(expected['sha1'])}")
        print(f"  SHA1 differing positions: {[(i, a, b) for i, (a, b) in enumerate(zip(sha1, expected['sha1'])) if a != b]}")
    if not (size_ok and crc_ok and sha1_ok):
        raise SystemExit(
            f"{label.upper()} identity check failed: "
            f"expected size=0x{expected['size']:X} CRC32={expected['crc32']} SHA1={expected['sha1']}"
        )


def print_vectors(code: bytes) -> None:
    print("\nIC4 descrambled 8051 vector table (raw bytes):")
    for address in VECTORS:
        op = code[address]
        target = int.from_bytes(code[address + 1:address + 3], "big")
        if op in (0x02, 0x12):  # LJMP / LCALL immediate 16-bit target
            name = "LJMP" if op == 0x02 else "LCALL"
            detail = f"{name} 0x{target:04X}"
        else:
            detail = f"opcode 0x{op:02X} (not LJMP/LCALL)"
        print(f"  0x{address:04X}: {code[address:address+3].hex(' ').upper():8s} {detail}")


def scan_code_references(code: bytes) -> None:
    # These are byte-pattern candidates only. A real disassembler/control-flow
    # pass is required before treating any hit as an instruction.
    patterns = {
        "LCALL (0x12)": 0x12,
        "LJMP (0x02)": 0x02,
        "MOV DPTR,#imm16 (0x90)": 0x90,
        "MOVX A,@DPTR (0xE0)": 0xE0,
        "MOVX @DPTR,A (0xF0)": 0xF0,
    }
    print("\nRaw opcode-byte candidate counts (not yet instruction-validated):")
    for name, opcode in patterns.items():
        hits = [i for i, value in enumerate(code[:-2]) if value == opcode]
        print(f"  {name}: {len(hits)}")
        if opcode in (0x90, 0x12, 0x02):
            for address in hits[:24]:
                if opcode == 0x90:
                    operand = int.from_bytes(code[address + 1:address + 3], "big")
                    print(f"    0x{address:04X} -> DPTR candidate 0x{operand:04X}")
                else:
                    target = int.from_bytes(code[address + 1:address + 3], "big")
                    print(f"    0x{address:04X} -> target candidate 0x{target:04X}")


def inspect_ic51(data: bytes) -> None:
    report("ic51", data)
    spans = []
    start = None
    for i, value in enumerate(data + b"\xff"):
        active = value != 0xFF
        if active and start is None:
            start = i
        elif not active and start is not None:
            if i - start >= 32:
                spans.append((start, i - 1))
            start = None
    print("\nIC51 raw non-0xFF spans (>=32 bytes; not asserted sample boundaries):")
    for start, end in spans:
        print(f"  0x{start:05X}-0x{end:05X} ({end-start+1} bytes)")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ic4", type=Path, default=Path("technox_IC4_mx27c1000_v105.bin"))
    parser.add_argument("--ic51", type=Path, default=Path("technox_IC51_nm27c010_v105.bin"))
    parser.add_argument("--write-descrambled-ic4", type=Path,
                        help="optional output path for a working copy of descrambled IC4")
    args = parser.parse_args()

    raw_ic4 = args.ic4.read_bytes()
    report("ic4", raw_ic4)
    code = descramble_ic4(raw_ic4)
    print("\nIC4 descrambled working image:")
    print(f"  size=0x{len(code):X} CRC32={zlib.crc32(code) & 0xffffffff:08x} SHA1={hashlib.sha1(code).hexdigest()}")
    print_vectors(code)
    scan_code_references(code)
    if args.write_descrambled_ic4:
        args.write_descrambled_ic4.parent.mkdir(parents=True, exist_ok=True)
        args.write_descrambled_ic4.write_bytes(code)
        print(f"\nWrote descrambled IC4 working copy: {args.write_descrambled_ic4}")

    if args.ic51.exists():
        inspect_ic51(args.ic51.read_bytes())
    else:
        print(f"\nIC51 not found at {args.ic51}; skipped. No IC51 transform was applied.")


if __name__ == "__main__":
    main()
