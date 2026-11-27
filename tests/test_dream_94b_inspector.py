#!/usr/bin/env python3
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from inspect_dream_94b_bank import (  # noqa: E402
    Dream94BFormatError,
    extract_sample_payload,
    inspect_bank,
)


def chunk(tag, payload, include_padding=True):
    header = tag.encode("ascii") + struct.pack("<I", len(payload))
    padding = b"\x00" if len(payload) & 1 and include_padding else b""
    return header + payload + padding


def valid_bank(extra_chunks=()):
    return b"".join([
        chunk("PRIO", b"GMSBK040\x00\x00"),
        chunk("IMAP", b"imap-data"),
        *extra_chunks,
        chunk("RLOC", b"relocation"),
        chunk("PARA", b"parameters"),
        chunk("SAMP", b"\x00\x7f\x80\xff\x10"),
    ])


class Dream94BInspectorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "candidate.94b"

    def write(self, data):
        self.path.write_bytes(data)
        return str(self.path)

    def test_valid_chunk_order_and_sample_digest(self):
        report = inspect_bank(self.write(valid_bank()))
        self.assertEqual(report["bank_identifier"], "GMSBK040")
        self.assertEqual(report["required_chunks"], ["PRIO", "IMAP", "RLOC", "PARA", "SAMP"])
        self.assertEqual([c["tag"] for c in report["chunks"]],
                         ["PRIO", "IMAP", "RLOC", "PARA", "SAMP"])
        sample = b"\x00\x7f\x80\xff\x10"
        self.assertEqual(report["sample_payload"]["size"], len(sample))
        self.assertEqual(report["sample_payload"]["sha256"],
                         hashlib.sha256(sample).hexdigest())
        self.assertEqual(sum(report["sample_payload"]["byte_histogram"]), len(sample))
        self.assertFalse(report["audio_decoded"])
        self.assertFalse(report["technox_compatibility_verified"])

    def test_unknown_chunk_is_preserved_and_reported(self):
        report = inspect_bank(self.write(valid_bank([chunk("XTRA", b"opaque")])))
        self.assertIn("XTRA", report["unknown_chunks"])

    def test_odd_payload_uses_one_alignment_byte(self):
        report = inspect_bank(self.write(valid_bank([chunk("ODD!", b"123")])))
        odd = next(c for c in report["chunks"] if c["tag"] == "ODD!")
        following = next(c for c in report["chunks"] if c["tag"] == "RLOC")
        self.assertEqual(following["offset"], odd["data_offset"] + odd["size"] + 1)

    def test_rejects_truncated_chunk_header(self):
        data = valid_bank() + b"IM"
        with self.assertRaisesRegex(Dream94BFormatError, "truncated chunk header"):
            inspect_bank(self.write(data))

    def test_rejects_payload_overrun(self):
        data = b"PRIO" + struct.pack("<I", 128) + b"tiny"
        with self.assertRaisesRegex(Dream94BFormatError, "extends beyond the file"):
            inspect_bank(self.write(data))

    def test_rejects_missing_required_chunk(self):
        data = b"".join([
            chunk("PRIO", b"GMSBK040\x00\x00"),
            chunk("IMAP", b"x"),
            chunk("RLOC", b"x"),
            chunk("PARA", b"x"),
        ])
        with self.assertRaisesRegex(Dream94BFormatError, "missing required chunks: SAMP"):
            inspect_bank(self.write(data))

    def test_rejects_duplicate_required_chunk(self):
        data = valid_bank([chunk("IMAP", b"second-map")])
        with self.assertRaisesRegex(Dream94BFormatError, "duplicate required IMAP"):
            inspect_bank(self.write(data))

    def test_rejects_prio_not_first(self):
        data = chunk("IMAP", b"map") + valid_bank()
        with self.assertRaisesRegex(Dream94BFormatError, "PRIO must be the first chunk"):
            inspect_bank(self.write(data))

    def test_rejects_missing_odd_chunk_alignment(self):
        # The final odd-sized SAMP chunk has no terminal pad byte. Keeping
        # it at EOF makes the missing byte unambiguous.
        data = b"".join([
            chunk("PRIO", b"GMSBK040\x00\x00"),
            chunk("IMAP", b"imap-data"),
            chunk("RLOC", b"relocation"),
            chunk("PARA", b"parameters"),
            chunk("SAMP", b"odd", include_padding=False),
        ])
        with self.assertRaisesRegex(Dream94BFormatError, "SAMP chunk.*extends beyond the file"):
            inspect_bank(self.write(data))

    def test_cli_json_report(self):
        path = self.write(valid_bank())
        script = Path(__file__).resolve().parents[1] / "tools" / "inspect_dream_94b_bank.py"
        result = subprocess.run(
            [sys.executable, str(script), path, "--json"],
            check=False, capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report["bank_identifier"], "GMSBK040")
        self.assertFalse(report["audio_decoded"])


    def test_extracts_opaque_samp_payload_byte_exactly(self):
        source = self.write(valid_bank())
        output = Path(self.temp.name) / "extracted-samp.bin"
        result = extract_sample_payload(source, str(output))
        expected = b"\x00\x7f\x80\xff\x10"
        self.assertEqual(output.read_bytes(), expected)
        self.assertEqual(result["size"], len(expected))
        self.assertEqual(result["sha256"], hashlib.sha256(expected).hexdigest())
        self.assertFalse(result["audio_decoded"])
        self.assertFalse(result["technox_compatibility_verified"])

    def test_extraction_refuses_to_overwrite_source(self):
        source = self.write(valid_bank())
        before = self.path.read_bytes()
        with self.assertRaisesRegex(Dream94BFormatError, "must not overwrite"):
            extract_sample_payload(source, source)
        self.assertEqual(self.path.read_bytes(), before)

    def test_extraction_requires_existing_output_directory(self):
        source = self.write(valid_bank())
        output = Path(self.temp.name) / "missing" / "samp.bin"
        with self.assertRaisesRegex(Dream94BFormatError, "directory does not exist"):
            extract_sample_payload(source, str(output))
        self.assertFalse(output.exists())

    def test_cli_extracts_samp_and_reports_opaque_status(self):
        source = self.write(valid_bank())
        output = Path(self.temp.name) / "cli-samp.bin"
        script = Path(__file__).resolve().parents[1] / "tools" / "inspect_dream_94b_bank.py"
        result = subprocess.run(
            [sys.executable, str(script), source, "--extract-samp", str(output), "--json"],
            check=False, capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(output.read_bytes(), b"\x00\x7f\x80\xff\x10")
        self.assertFalse(report["sample_payload"]["extraction"]["audio_decoded"])


if __name__ == "__main__":
    unittest.main()
