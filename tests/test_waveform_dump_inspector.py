#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import importlib.util
import io
import tempfile
import unittest
import zlib
from contextlib import redirect_stdout
from pathlib import Path

SCRIPT = Path(__file__).with_name("inspect_technox_waveform_dump.py")
SPEC = importlib.util.spec_from_file_location("waveform_inspector", SCRIPT)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class WaveformDumpInspectorTests(unittest.TestCase):
    def test_streaming_hash_and_histogram_across_chunk_boundaries(self) -> None:
        payload = bytes([0x00, 0xFF, 0x01, 0x01, 0x80, 0x00, 0x7F]) * 19
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "candidate.bin"
            path.write_bytes(payload)
            report = MODULE.inspect_file(path, chunk_size=5)

        self.assertEqual(report["size_bytes"], len(payload))
        self.assertEqual(report["crc32"], f"{zlib.crc32(payload) & 0xFFFFFFFF:08x}")
        self.assertEqual(report["sha256"], hashlib.sha256(payload).hexdigest())
        self.assertEqual(report["zero_bytes"], payload.count(0))
        self.assertEqual(report["ff_bytes"], payload.count(0xFF))
        self.assertEqual(report["unique_byte_values"], len(set(payload)))
        self.assertFalse(report["decoded"])
        self.assertEqual(report["format_assumptions"], [])

    def test_empty_image_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "empty.bin"
            path.write_bytes(b"")
            with self.assertRaisesRegex(ValueError, "empty image"):
                MODULE.inspect_file(path)

    def test_zero_chunk_size_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "candidate.bin"
            path.write_bytes(b"data")
            with self.assertRaisesRegex(ValueError, "chunk_size"):
                MODULE.inspect_file(path, chunk_size=0)

    def test_cli_expected_size_and_hash(self) -> None:
        payload = b"TechnoX-waveform-candidate"
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "candidate.bin"
            path.write_bytes(payload)
            with redirect_stdout(io.StringIO()):
                code = MODULE.main([
                    str(path), "--chunk-size", "3",
                    "--expected-size", str(len(payload)),
                    "--expected-sha256", hashlib.sha256(payload).hexdigest(),
                    "--json",
                ])
        self.assertEqual(code, 0)


if __name__ == "__main__":
    unittest.main()
