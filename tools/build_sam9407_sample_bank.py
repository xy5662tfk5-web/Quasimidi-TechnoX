#!/usr/bin/env python3
"""Convert captured PCM WAVs into the TechnoX external sample-bank format.

This imports audio captures; it does not decode the internal TechnoX waveform ROM.
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys
import wave
from pathlib import Path
from typing import List, Sequence, Tuple

MAX_WORD_COUNT = 0x02000000
MAX_PROGRAMS_PER_BANK = 256
MIN_SAMPLE_RATE = 4000
MAX_SAMPLE_RATE = 192000


class SampleBankBuildError(ValueError):
    pass


def _decode_sample(data: bytes, width: int) -> int:
    if width == 1:
        return (data[0] - 128) << 8  # WAV 8-bit PCM is unsigned.
    if width == 2:
        return int.from_bytes(data, "little", signed=True)
    if width == 3:
        return int.from_bytes(data, "little", signed=True) >> 8
    if width == 4:
        return int.from_bytes(data, "little", signed=True) >> 16
    raise SampleBankBuildError("only 8/16/24/32-bit PCM WAV is supported")


def read_wav_as_mono_i16(path: Path) -> Tuple[int, List[int]]:
    try:
        with wave.open(str(path), "rb") as wav:
            channels = wav.getnchannels()
            width = wav.getsampwidth()
            sample_rate = wav.getframerate()
            frame_count = wav.getnframes()
            if wav.getcomptype() != "NONE":
                raise SampleBankBuildError(
                    "{}: compressed WAV is unsupported".format(path.name)
                )
            if channels not in (1, 2):
                raise SampleBankBuildError(
                    "{}: expected 1 or 2 channels".format(path.name)
                )
            if width not in (1, 2, 3, 4):
                raise SampleBankBuildError(
                    "{}: unsupported PCM bit depth".format(path.name)
                )
            if not MIN_SAMPLE_RATE <= sample_rate <= MAX_SAMPLE_RATE:
                raise SampleBankBuildError(
                    "{}: sample rate outside {}..{} Hz".format(
                        path.name, MIN_SAMPLE_RATE, MAX_SAMPLE_RATE
                    )
                )
            if frame_count <= 0:
                raise SampleBankBuildError("{}: WAV contains no frames".format(path.name))
            raw = wav.readframes(frame_count)
    except (OSError, EOFError, wave.Error) as exc:
        raise SampleBankBuildError("{}: cannot read WAV: {}".format(path.name, exc))

    stride = channels * width
    if len(raw) != frame_count * stride:
        raise SampleBankBuildError("{}: truncated PCM data".format(path.name))

    result = []
    offset = 0
    for _ in range(frame_count):
        left = _decode_sample(raw[offset:offset + width], width)
        offset += width
        if channels == 2:
            right = _decode_sample(raw[offset:offset + width], width)
            offset += width
            value = int((left + right) / 2)  # truncation toward zero
        else:
            value = left
        result.append(max(-32768, min(32767, value)))
    return sample_rate, result


def build_sample_bank(input_directory: Path,
                      waveform_output: Path,
                      map_output: Path,
                      bank: int = 0,
                      start_program: int = 0,
                      root_note: int = 60,
                      gain: int = 127,
                      pan: int = 64) -> dict:
    if not input_directory.is_dir():
        raise SampleBankBuildError("input directory does not exist: {}".format(input_directory))
    if not 0 <= bank <= 255 or not 0 <= start_program <= 255:
        raise SampleBankBuildError("bank and start-program must be in 0..255")
    if not 0 <= root_note <= 127 or not 0 <= gain <= 127 or not 0 <= pan <= 127:
        raise SampleBankBuildError("root-note, gain and pan must be in 0..127")

    wav_files = sorted(
        (p for p in input_directory.iterdir()
         if p.is_file() and p.suffix.lower() == ".wav"),
        key=lambda p: (p.name.casefold(), p.name),
    )
    if not wav_files:
        raise SampleBankBuildError("no WAV files found")
    if len(wav_files) > MAX_PROGRAMS_PER_BANK - start_program:
        raise SampleBankBuildError("WAV files do not fit in the selected 256-program bank")

    waveform_output.parent.mkdir(parents=True, exist_ok=True)
    map_output.parent.mkdir(parents=True, exist_ok=True)
    waveform_temp = waveform_output.with_name(waveform_output.name + ".tmp")
    map_temp = map_output.with_name(map_output.name + ".tmp")
    entries = []
    next_word = 0
    try:
        with waveform_temp.open("wb") as raw_file:
            for index, wav_path in enumerate(wav_files):
                sample_rate, samples = read_wav_as_mono_i16(wav_path)
                end_word = next_word + len(samples)
                if end_word > MAX_WORD_COUNT:
                    raise SampleBankBuildError(
                        "25-bit word-address space exceeded at {}".format(wav_path.name)
                    )
                raw_file.write(struct.pack("<{}h".format(len(samples)), *samples))
                entries.append({
                    "bank": bank,
                    "program": start_program + index,
                    "start_word": next_word,
                    "end_word": end_word,
                    "sample_rate": sample_rate,
                    "root_note": root_note,
                    "tune_cents": 0,
                    "gain": gain,
                    "pan": pan,
                    "byte_order": "little_endian",
                    "loop_mode": "one_shot",
                    "source_name": wav_path.name,
                })
                next_word = end_word

        with map_temp.open("w", encoding="utf-8", newline="\n") as map_file:
            json.dump({
                "format": "technox-sam9407-map-v1",
                "entries": entries,
            }, map_file, ensure_ascii=False, indent=2)
            map_file.write("\n")

        os.replace(str(waveform_temp), str(waveform_output))
        os.replace(str(map_temp), str(map_output))
    except Exception:
        for temp in (waveform_temp, map_temp):
            try:
                temp.unlink()
            except FileNotFoundError:
                pass
        raise

    return {
        "sample_count": len(entries),
        "word_count": next_word,
        "byte_count": next_word * 2,
        "entries": entries,
    }


def main(argv: Sequence[str] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Pack captured WAVs as mono 16-bit PCM and generate a sample map."
    )
    parser.add_argument("input_directory", type=Path)
    parser.add_argument("--waveform-out", type=Path, required=True)
    parser.add_argument("--map-out", type=Path, required=True)
    parser.add_argument("--bank", type=int, default=0)
    parser.add_argument("--start-program", type=int, default=0)
    parser.add_argument("--root-note", type=int, default=60)
    parser.add_argument("--gain", type=int, default=127)
    parser.add_argument("--pan", type=int, default=64)
    args = parser.parse_args(argv)
    try:
        report = build_sample_bank(
            args.input_directory, args.waveform_out, args.map_out,
            args.bank, args.start_program, args.root_note, args.gain, args.pan
        )
    except (OSError, SampleBankBuildError, struct.error) as exc:
        print("Sample-bank build failed: {}".format(exc), file=sys.stderr)
        return 2

    print("Built {} samples: {} bytes ({} 16-bit words)".format(
        report["sample_count"], report["byte_count"], report["word_count"]
    ))
    print("Waveforms: {}".format(args.waveform_out))
    print("Map:       {}".format(args.map_out))
    print("Imported WAV captures; not a TechnoX internal waveform-ROM dump.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
