#!/usr/bin/env python3
"""Evidence-first integrity inspection for a candidate TechnoX waveform dump.

This tool deliberately does not assume a PCM codec, sample rate, byte order,
chip count, address permutation, or loop format. It produces reproducible
integrity metadata for a raw binary image using bounded-memory streaming.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
import zlib
from pathlib import Path
from typing import Any

DEFAULT_CHUNK_SIZE = 1024 * 1024
REPORTED_WAVEFORM_CAPACITY = (
    "6 MB (The Mix review, April 1995; later 64 MB listings conflict; not verified on this board)"
)


def inspect_file(path: Path, chunk_size: int = DEFAULT_CHUNK_SIZE) -> dict[str, Any]:
    """Return integrity statistics without loading the image into memory."""
    if chunk_size <= 0:
        raise ValueError("chunk_size must be greater than zero")
    if not path.exists():
        raise FileNotFoundError(path)
    if not path.is_file():
        raise ValueError(f"not a regular file: {path}")

    sha256 = hashlib.sha256()
    crc32 = 0
    histogram = [0] * 256
    byte_count = 0

    with path.open("rb") as stream:
        while True:
            chunk = stream.read(chunk_size)
            if not chunk:
                break
            byte_count += len(chunk)
            sha256.update(chunk)
            crc32 = zlib.crc32(chunk, crc32)
            for value in chunk:
                histogram[value] += 1

    if byte_count == 0:
        raise ValueError(f"empty image: {path}")

    return {
        "file": str(path),
        "size_bytes": byte_count,
        "crc32": f"{crc32 & 0xFFFFFFFF:08x}",
        "sha256": sha256.hexdigest(),
        "unique_byte_values": sum(1 for count in histogram if count),
        "zero_bytes": histogram[0],
        "ff_bytes": histogram[255],
        "zero_fraction": histogram[0] / byte_count,
        "ff_fraction": histogram[255] / byte_count,
        "byte_histogram": histogram,
        "reported_capacity_context": REPORTED_WAVEFORM_CAPACITY,
        "decoded": False,
        "format_assumptions": [],
    }


def parse_size(value: str) -> int:
    """Parse decimal or prefixed integer sizes (e.g. 67108864 or 0x4000000)."""
    try:
        parsed = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid size: {value}") from exc
    if parsed <= 0:
        raise argparse.ArgumentTypeError("size must be greater than zero")
    return parsed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="candidate raw waveform-memory dump")
    parser.add_argument(
        "--chunk-size", type=parse_size, default=DEFAULT_CHUNK_SIZE,
        help=f"streaming read size in bytes (default: {DEFAULT_CHUNK_SIZE})"
    )
    parser.add_argument(
        "--expected-size", type=parse_size,
        help="optional exact byte-count check; no size is assumed by default"
    )
    parser.add_argument(
        "--expected-sha256", help="optional SHA-256 check from a second read/dump"
    )
    parser.add_argument("--json", action="store_true", help="print machine-readable JSON")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        report = inspect_file(args.image, args.chunk_size)
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    checks: dict[str, bool] = {}
    if args.expected_size is not None:
        checks["size"] = report["size_bytes"] == args.expected_size
    if args.expected_sha256:
        checks["sha256"] = report["sha256"].lower() == args.expected_sha256.lower()
    report["checks"] = checks
    report["checks_passed"] = all(checks.values()) if checks else None

    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(f"File:   {report['file']}")
        print(f"Size:   {report['size_bytes']} bytes (0x{report['size_bytes']:X})")
        print(f"CRC32:  {report['crc32']}")
        print(f"SHA256: {report['sha256']}")
        print(f"Unique byte values: {report['unique_byte_values']}/256")
        print(f"0x00 bytes: {report['zero_bytes']} ({report['zero_fraction']:.3%})")
        print(f"0xFF bytes: {report['ff_bytes']} ({report['ff_fraction']:.3%})")
        print(f"Reference capacity: {REPORTED_WAVEFORM_CAPACITY}")
        print("Decode status: NOT DECODED (codec/address/loop metadata remain unknown)")
        for name, passed in checks.items():
            print(f"Check {name}: {'OK' if passed else 'FAIL'}")
    return 0 if report["checks_passed"] is not False else 1


if __name__ == "__main__":
    raise SystemExit(main())
