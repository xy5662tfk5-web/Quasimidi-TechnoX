#!/usr/bin/env python3
"""Evidence-only structural inspector for Dream .94B sound-bank containers.

This tool validates the chunk container. It does not decode sample codecs,
instrument envelopes, relocation semantics, or map any bank to TechnoX presets.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys
from typing import Any, Dict, List, Tuple


REQUIRED_CHUNKS = ("PRIO", "IMAP", "RLOC", "PARA", "SAMP")
SAMPLE_READ_SIZE = 64 * 1024


class Dream94BFormatError(ValueError):
    """Raised when a .94B container is truncated or structurally invalid."""


def _read_exact(stream, count: int, context: str) -> bytes:
    data = stream.read(count)
    if len(data) != count:
        raise Dream94BFormatError(
            f"truncated {context}: expected {count} bytes, received {len(data)}"
        )
    return data


def _skip_exact(stream, count: int, context: str) -> None:
    remaining = count
    while remaining:
        block = stream.read(min(remaining, SAMPLE_READ_SIZE))
        if not block:
            raise Dream94BFormatError(
                f"truncated {context}: {remaining} payload bytes were not present"
            )
        remaining -= len(block)


def _read_sample_payload(stream, count: int) -> Tuple[str, List[int]]:
    digest = hashlib.sha256()
    histogram = [0] * 256
    remaining = count
    while remaining:
        block = stream.read(min(remaining, SAMPLE_READ_SIZE))
        if not block:
            raise Dream94BFormatError(
                f"truncated SAMP payload: {remaining} sample bytes were not present"
            )
        digest.update(block)
        for value in block:
            histogram[value] += 1
        remaining -= len(block)
    return digest.hexdigest(), histogram


def _decode_bank_name(payload: bytes) -> str:
    # Published reference banks use an eight-character ASCII bank identifier,
    # padded to ten bytes in PRIO (for example: GMSBK040\0\0).
    name_bytes = payload.split(b"\x00", 1)[0].rstrip(b" ")
    if not name_bytes:
        raise Dream94BFormatError("PRIO chunk does not contain a bank identifier")
    try:
        name = name_bytes.decode("ascii")
    except UnicodeDecodeError as exc:
        raise Dream94BFormatError("PRIO bank identifier is not ASCII") from exc
    if any(ord(char) < 0x20 or ord(char) > 0x7E for char in name):
        raise Dream94BFormatError("PRIO bank identifier contains non-printable bytes")
    return name


def inspect_bank(path: str) -> Dict[str, Any]:
    """Validate a Dream .94B-style chunk container using bounded memory."""
    file_size = os.path.getsize(path)
    chunks: List[Dict[str, Any]] = []
    counts: Dict[str, int] = {}
    bank_name = None
    sample_sha256 = None
    sample_histogram = None

    with open(path, "rb") as stream:
        while stream.tell() < file_size:
            chunk_offset = stream.tell()
            remaining_file = file_size - chunk_offset
            if remaining_file < 8:
                raise Dream94BFormatError(
                    f"truncated chunk header at file offset 0x{chunk_offset:X}"
                )

            header = _read_exact(stream, 8, "chunk header")
            raw_tag = header[:4]
            try:
                tag = raw_tag.decode("ascii")
            except UnicodeDecodeError as exc:
                raise Dream94BFormatError(
                    f"non-ASCII chunk identifier at file offset 0x{chunk_offset:X}"
                ) from exc
            if any(ord(char) < 0x20 or ord(char) > 0x7E for char in tag):
                raise Dream94BFormatError(
                    f"non-printable chunk identifier at file offset 0x{chunk_offset:X}"
                )

            payload_size = struct.unpack("<I", header[4:])[0]
            data_offset = chunk_offset + 8
            padded_size = payload_size + (payload_size & 1)
            if padded_size > file_size - data_offset:
                raise Dream94BFormatError(
                    f"{tag} chunk at 0x{chunk_offset:X} declares {payload_size} "
                    "payload bytes but extends beyond the file"
                )

            counts[tag] = counts.get(tag, 0) + 1
            if tag in REQUIRED_CHUNKS and counts[tag] > 1:
                raise Dream94BFormatError(f"duplicate required {tag} chunk")

            chunk_record: Dict[str, Any] = {
                "tag": tag,
                "offset": chunk_offset,
                "data_offset": data_offset,
                "size": payload_size,
            }

            if tag == "PRIO":
                if chunk_offset != 0:
                    raise Dream94BFormatError("PRIO must be the first chunk")
                if payload_size == 0 or payload_size > 64:
                    raise Dream94BFormatError(
                        f"unexpected PRIO identifier length: {payload_size}"
                    )
                bank_name = _decode_bank_name(
                    _read_exact(stream, payload_size, "PRIO payload")
                )
                chunk_record["bank_identifier"] = bank_name
            elif tag == "SAMP":
                if payload_size == 0:
                    raise Dream94BFormatError("SAMP chunk is empty")
                sample_sha256, sample_histogram = _read_sample_payload(
                    stream, payload_size
                )
                chunk_record["sha256"] = sample_sha256
            else:
                _skip_exact(stream, payload_size, f"{tag} payload")

            if payload_size & 1:
                _read_exact(stream, 1, f"{tag} alignment byte")

            chunks.append(chunk_record)

    if not chunks:
        raise Dream94BFormatError("file contains no chunks")
    if chunks[0]["tag"] != "PRIO":
        raise Dream94BFormatError("PRIO must be the first chunk")

    missing = [tag for tag in REQUIRED_CHUNKS if counts.get(tag, 0) == 0]
    if missing:
        raise Dream94BFormatError(
            "missing required chunks: " + ", ".join(missing)
        )

    if sample_histogram is None or sample_sha256 is None:
        raise Dream94BFormatError("SAMP payload was not inspected")

    unknown_chunks = [chunk["tag"] for chunk in chunks
                      if chunk["tag"] not in REQUIRED_CHUNKS]
    return {
        "format": "dream-94b-chunk-container",
        "validation": "structural-only",
        "file_size": file_size,
        "bank_identifier": bank_name,
        "chunks": chunks,
        "required_chunks": list(REQUIRED_CHUNKS),
        "unknown_chunks": unknown_chunks,
        "sample_payload": {
            "offset": next(c["data_offset"] for c in chunks if c["tag"] == "SAMP"),
            "size": next(c["size"] for c in chunks if c["tag"] == "SAMP"),
            "sha256": sample_sha256,
            "byte_histogram": sample_histogram,
        },
        "audio_decoded": False,
        "technox_compatibility_verified": False,
    }



def extract_sample_payload(bank_path: str, output_path: str) -> Dict[str, Any]:
    """Extract the opaque SAMP bytes without interpreting or converting them."""
    report = inspect_bank(bank_path)
    sample = report["sample_payload"]
    source_real = os.path.realpath(bank_path)
    output_real = os.path.realpath(output_path)
    if source_real == output_real:
        raise Dream94BFormatError("SAMP output must not overwrite the source bank")

    output_parent = os.path.dirname(output_real) or "."
    if not os.path.isdir(output_parent):
        raise Dream94BFormatError("SAMP output directory does not exist")

    temporary_path = output_real + ".tmp"
    digest = hashlib.sha256()
    remaining = sample["size"]
    try:
        with open(bank_path, "rb") as source, open(temporary_path, "wb") as target:
            source.seek(sample["offset"])
            while remaining:
                block = source.read(min(remaining, SAMPLE_READ_SIZE))
                if not block:
                    raise Dream94BFormatError(
                        f"truncated SAMP payload during extraction: {remaining} bytes missing"
                    )
                target.write(block)
                digest.update(block)
                remaining -= len(block)

        extracted_sha256 = digest.hexdigest()
        if extracted_sha256 != sample["sha256"]:
            raise Dream94BFormatError(
                "SAMP payload changed between inspection and extraction"
            )
        os.replace(temporary_path, output_real)
    except Exception:
        try:
            os.remove(temporary_path)
        except FileNotFoundError:
            pass
        raise

    return {
        "output": output_real,
        "size": sample["size"],
        "sha256": extracted_sha256,
        "audio_decoded": False,
        "technox_compatibility_verified": False,
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Validate Dream .94B chunk structure (no PCM decoding)."
    )
    parser.add_argument("bank", help="path to a .94B candidate bank")
    parser.add_argument("--json", action="store_true", help="emit JSON report")
    parser.add_argument("--extract-samp", metavar="PATH",
                        help="extract opaque SAMP bytes; does not decode audio")
    args = parser.parse_args(argv)

    try:
        report = inspect_bank(args.bank)
    except (OSError, Dream94BFormatError) as exc:
        print(f"94B inspection failed: {exc}", file=sys.stderr)
        return 2

    if args.extract_samp:
        try:
            report["sample_payload"]["extraction"] = extract_sample_payload(
                args.bank, args.extract_samp
            )
        except (OSError, Dream94BFormatError) as exc:
            print(f"94B SAMP extraction failed: {exc}", file=sys.stderr)
            return 2

    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(f"Dream 94B-like container: {args.bank}")
        print(f"Bank identifier: {report['bank_identifier']}")
        print(f"File size: {report['file_size']} bytes")
        for chunk in report["chunks"]:
            print(
                f"  {chunk['tag']}: offset=0x{chunk['offset']:X}, "
                f"size={chunk['size']} bytes"
            )
        sample = report["sample_payload"]
        print(f"SAMP SHA-256: {sample['sha256']}")
        if args.extract_samp:
            print(f"SAMP extracted (opaque bytes): {args.extract_samp}")
        print("Structural validation only; no audio decoding or TechnoX mapping.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
