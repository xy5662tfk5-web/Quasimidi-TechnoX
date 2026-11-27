#!/usr/bin/env python3
import json
import struct
import subprocess
import sys
import tempfile
import unittest
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from build_sam9407_sample_bank import (
    SampleBankBuildError,
    build_sample_bank,
    read_wav_as_mono_i16,
)


def write_wav(path, channels, width, rate, frames):
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(width)
        wav.setframerate(rate)
        raw = bytearray()
        for frame in frames:
            values = frame if channels == 2 else (frame,)
            for value in values:
                if width == 1:
                    raw.append(int(value) & 0xff)
                else:
                    raw.extend(int(value).to_bytes(width, "little", signed=True))
        wav.writeframes(bytes(raw))


class SampleBankBuilderTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "captures"
        self.source.mkdir()
        self.raw = self.root / "SampleBank" / "waveforms.raw"
        self.mapping = self.root / "SampleBank" / "sample_map.json"

    def test_packs_sorted_mono_16bit_and_stereo_24bit(self):
        write_wav(self.source / "Snare.wav", 2, 3, 32000, [
            (0x123400, 0x123400), (-0x234500, -0x234500)
        ])
        write_wav(self.source / "Kick.wav", 1, 2, 44100, [0, 32767, -32768])
        report = build_sample_bank(
            self.source, self.raw, self.mapping, bank=2, start_program=10
        )
        self.assertEqual(struct.unpack("<5h", self.raw.read_bytes()),
                         (0, 32767, -32768, 0x1234, -0x2345))
        self.assertEqual(report["sample_count"], 2)
        self.assertEqual(report["word_count"], 5)
        data = json.loads(self.mapping.read_text(encoding="utf-8"))
        self.assertEqual(data["format"], "technox-sam9407-map-v1")
        self.assertEqual(data["entries"][0]["source_name"], "Kick.wav")
        self.assertEqual(data["entries"][0]["bank"], 2)
        self.assertEqual(data["entries"][0]["program"], 10)
        self.assertEqual(data["entries"][0]["start_word"], 0)
        self.assertEqual(data["entries"][0]["end_word"], 3)
        self.assertEqual(data["entries"][1]["program"], 11)
        self.assertEqual(data["entries"][1]["start_word"], 3)
        self.assertEqual(data["entries"][1]["end_word"], 5)
        self.assertEqual(data["entries"][1]["sample_rate"], 32000)
        self.assertEqual(data["entries"][1]["loop_mode"], "one_shot")

    def test_24bit_sign_extension_and_stereo_downmix(self):
        path = self.source / "stereo24.wav"
        write_wav(path, 2, 3, 48000, [
            (0x123400, 0x123400), (-0x234500, -0x234500), (0x100000, -0x100000)
        ])
        rate, samples = read_wav_as_mono_i16(path)
        self.assertEqual(rate, 48000)
        self.assertEqual(samples, [0x1234, -0x2345, 0])

    def test_8bit_unsigned_pcm(self):
        path = self.source / "8bit.wav"
        write_wav(path, 1, 1, 22050, [0, 128, 255])
        self.assertEqual(read_wav_as_mono_i16(path), (22050, [-32768, 0, 32512]))

    def test_rejects_empty_directory(self):
        with self.assertRaisesRegex(SampleBankBuildError, "no WAV files"):
            build_sample_bank(self.source, self.raw, self.mapping)

    def test_rejects_program_overflow(self):
        write_wav(self.source / "one.wav", 1, 2, 44100, [100])
        write_wav(self.source / "two.wav", 1, 2, 44100, [200])
        # Program 255 is a valid starting index, but two files cannot fit.
        with self.assertRaisesRegex(SampleBankBuildError, "do not fit"):
            build_sample_bank(self.source, self.raw, self.mapping, start_program=255)

    def test_rejects_three_channels(self):
        path = self.source / "three.wav"
        with wave.open(str(path), "wb") as wav:
            wav.setnchannels(3)
            wav.setsampwidth(2)
            wav.setframerate(44100)
            wav.writeframes(b"\x00" * 6)
        with self.assertRaisesRegex(SampleBankBuildError, "expected 1 or 2 channels"):
            read_wav_as_mono_i16(path)

    def test_rejects_invalid_rate_and_empty_wav(self):
        slow = self.source / "slow.wav"
        write_wav(slow, 1, 2, 2000, [1])
        with self.assertRaisesRegex(SampleBankBuildError, "sample rate"):
            read_wav_as_mono_i16(slow)
        empty = self.source / "empty.wav"
        write_wav(empty, 1, 2, 44100, [])
        with self.assertRaisesRegex(SampleBankBuildError, "no frames"):
            read_wav_as_mono_i16(empty)

    def test_cli_outputs_waveform_and_map_pair(self):
        write_wav(self.source / "Hit.wav", 1, 2, 44100, [1, -1])
        script = Path(__file__).resolve().parents[1] / "tools" / "build_sam9407_sample_bank.py"
        result = subprocess.run([
            sys.executable, str(script), str(self.source),
            "--waveform-out", str(self.raw), "--map-out", str(self.mapping),
        ], capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(self.raw.exists())
        self.assertTrue(self.mapping.exists())
        self.assertIn("1 samples", result.stdout)


if __name__ == "__main__":
    unittest.main()
