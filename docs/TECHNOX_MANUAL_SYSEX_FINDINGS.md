# TechnoX Owner's Manual and MIDI Findings

A 53-page English Owner's Manual PDF has been located online:
https://www.bn1studio.co.uk/wp-content/uploads/2022/05/quasimidi-technox-manual-478196.pdf

Useful sections:
- Pages 32-33: MIDI controllers, NRPN/RPN.
- Pages 34-38: TechnoX SysEx format, address map and parameter offsets.
- Pages 40-48: factory Performance, single-sound and drum-set lists.
- Page 51: technical specifications.

## Confirmed protocol details from the manual

- Quasimidi manufacturer ID: `3F`.
- TechnoX device ID: `22`.
- Identity reply includes a four-character ASCII firmware version.
- SysEx supports request-data (`52`) and dump-data (`44`) operations.
- Data payload bytes are 7-bit.
- Address map includes system parameters, temporary common/part/drum parameters, 50 Performance memories, 14 Multisetups, 8 user drumsets, and four sound-name banks.
- Sound-name entries are documented as request-only.
- The manual documents 16 parts; part 10 is the dedicated drum channel in normal use.
- The 512 single sounds are arranged as four banks. MIDI selection uses Bank Select controller 0 (values 0-3) followed by Program Change 0-127.

## What this means for the emulator

The manual is sufficient to begin a documented SysEx parser, parameter model, bank/name tables, and MIDI behavior. We should not wait for the user to upload another copy of the manual unless a local archival copy is desired.

The manual does NOT contain the ROM waveform data, CPU firmware dump, board schematics, or DSP/sample playback implementation.

## Remaining user-supplied material

Highest priority:
1. `technox.zip` from a lawful source, or the two raw ROM files listed in `TECHNOX_ROM_HARDWARE_RESEARCH.md`.
2. Any factory SysEx bulk dumps, especially performances, drumsets, and user banks.
3. Photos of both sides of the complete main PCB, with all chip markings readable.
4. Any additional ROM dumps found on the board, beyond IC4 and IC51.
5. Optional but valuable: dry audio references and matching MIDI performances captured from a real TechnoX.

## Caution

MAME's `samples` region for IC51 is only 128 KiB, whereas period/secondary specifications describe a substantially larger sample memory. Treat the MAME set as incomplete until the board's complete ROM inventory and actual sample storage are established.
