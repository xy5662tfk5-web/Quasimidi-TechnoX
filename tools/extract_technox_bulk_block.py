#!/usr/bin/env python3
"""Extract one IC4 0xAF1A bulk-transfer block from a raw IC51 image.

The firmware reads four bytes at DPTR=2*R7: a big-endian source address
followed by nested DJNZ loop counters. A zero low counter represents 256
iterations. The script does not assume a PCM format or codec.
"""
import argparse
from pathlib import Path

BANK_SIZE = 0x10000
IMAGE_SIZE = 0x20000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--bank", type=int, choices=(0, 1), required=True)
    parser.add_argument("--r7", type=lambda value: int(value, 0), default=0x48)
    parser.add_argument("-o", "--output", type=Path, required=True)
    args = parser.parse_args()
    if not 0 <= args.r7 <= 0xff:
        parser.error("R7 must fit in one byte")

    image = args.rom.read_bytes()
    if len(image) != IMAGE_SIZE:
        parser.error(f"expected a 0x20000-byte IC51 image, got {len(image):#x}")
    bank = image[args.bank * BANK_SIZE:(args.bank + 1) * BANK_SIZE]
    offset = args.r7 * 2
    source = (bank[offset] << 8) | bank[offset + 1]
    count_high, count_low = bank[offset + 2:offset + 4]
    count = count_high * 256 + (count_low if count_low else 256)
    payload = bytes(bank[(source + index) & 0xffff] for index in range(count))
    args.output.write_bytes(payload)

    print(f"IC51 bank {args.bank}, R7={args.r7:#04x}, table offset={offset:#04x}")
    print(f"header: {bank[offset:offset + 4].hex(' ')}")
    print(f"source: {source:#06x}; transfer: {count:#06x} ({count} bytes)")
    print(f"source end (exclusive, 16-bit wrap): {(source + count) & 0xffff:#06x}")
    print(f"wrote {len(payload)} bytes to {args.output}")


if __name__ == "__main__":
    main()
