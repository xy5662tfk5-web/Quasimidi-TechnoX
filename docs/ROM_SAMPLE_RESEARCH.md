# TechnoX ROM and sample-format investigation

Status: ROM identity and byte layout verified; sample encoding and playback format remain unknown.

## 1. Verified ROM inventory

The repository contains two 128 KiB images:

| IC | Role in MAME | Size | CRC32 | SHA-1 |
|---|---|---:|---|---|
| IC4 mx27c1000 v1.05 | program | 131,072 bytes | 4d393cd9 | 9ee853f1e2f88fbfc86589c4d0f91003d0ff146f |
| IC51 nm27c010 v1.05 | samples region | 131,072 bytes | d669570e | 0645a1b022ed25a836152933e6df32da4448f49a |

The values match the MAME ROM declarations. MAME's TechnoX driver identifies an I8032-family CPU configuration (the source comments that the exact type and clock are unknown), an ACIA 6850 and the LCD. It registers the TechnoX as MACHINE_NO_SOUND | MACHINE_NOT_WORKING. The sample region is declared, but no sample playback device, codec or sample-address decoder is implemented.

Source: https://github.com/mamedev/mame/blob/master/src/mame/skeleton/qmquasar.cpp

## 2. Program ROM handling

MAME's init_quasar() descrambles the 128 KiB program region in 256-byte blocks using the address permutation and XOR shown in the source. The function operates on the program region only. It does not transform the IC51 sample region.

Do not apply the IC4 address permutation to IC51.

## 3. IC51 byte-level observations

A byte-level pass over the supplied IC51 image found:

- Size: 131,072 bytes (0x20000).
- Shannon entropy: approximately 4.8051 bits per byte.
- Zero bytes: 34,850.
- 0xFF bytes: 30,663.
- The image contains long 0xFF-filled areas and two apparent active-data spans:
  - 0x0000–0xE4FB
  - 0x10000–0x1AEFB
- The 0xFF-filled gaps/tails include 0xE4FC–0xFF36, 0xFF38–0xFFFF, 0x1AEFC–0x1FF36 and 0x1FF38–0x1FFFF. There are isolated non-0xFF bytes at 0xFF37 (0xF7) and 0x1FF37 (0xE5).
- No common RIFF/WAVE, AIFF, Ogg, FLAC or Standard MIDI container signatures were found in the image.

These are byte-layout observations, not proof of sample boundaries. The two active spans may reflect banks, tables, packed sample data or another firmware-specific layout. The isolated marker bytes are not assigned a meaning.

Entropy and adjacent-byte statistics do not identify a PCM/ADPCM codec. In particular, do not infer sample rate, bit depth, signedness, channel count, loop points or sample offsets from these statistics alone.

## 4. The 64 MB specification discrepancy

Some secondary TechnoX specification pages describe a 64 MB ROM sample bank. The MAME dump inventory currently exposes only a 128 KiB IC51 image as the samples region. These statements are not reconciled by the available evidence. The IC51 dump must not be treated as a complete 64 MB waveform library.

Reference: https://www.vintagesynth.com/quasimidi/technox

## 5. What is still needed to decode sound

1. Complete, verified ROM/EPROM inventory from a physical TechnoX, including PCB photographs and IC markings.
2. A disassembly of the descrambled IC4 firmware, especially the routines that read IC51 and configure the sound hardware.
3. A real hardware MIDI/audio capture set: known single notes, velocities, note-off/release, program changes, pitch bend, modulation, and selected Part parameter changes.
4. Repeated tests at different sample rates/notes to identify sample playback rates, loop boundaries and interpolation.
5. Only after these steps: implement a candidate decoder and compare its output against hardware recordings.

## 6. Consequence for M.A.S.S.

The project now has a voice allocator, a 16-Part parameter store, a modulation matrix, per-Part Mod Wheel source storage and a backend interface. The backend remains deliberately silent. This separates the already verified MIDI/Part/control model from the unknown audio engine and avoids inventing a sound that merely resembles the TechnoX.
