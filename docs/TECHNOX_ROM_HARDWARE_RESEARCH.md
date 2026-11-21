# TechnoX ROM and Hardware Research

Date: 2026-11-21

## Confirmed from MAME source

Source: `mamedev/mame/src/mame/skeleton/qmquasar.cpp`

### ROM set

MAME declares two regions for `technox`:

- `program`, size `0x20000` (128 KiB): `mx27c1000_v105.ic4`
  - CRC32: `4d393cd9`
  - SHA1: `9ee853f1e2f88fbfc86589c4d0f91003d0ff146f`
  - Comment in source: Version No 1.05b
- `samples`, size `0x20000` (128 KiB): `nm27c010_v105.ic51`
  - CRC32: `d669570e`
  - SHA1: `0645a1b022ed25a836152933e6df32da4448f49a`

Important: MAME labels IC51 as the samples region, but does not implement sample playback or document its encoding. This is evidence that the dump is associated with sample data, not proof that it contains the complete factory waveform library.

The MAME driver applies `init_quasar()` to TechnoX as well as Quasar. It descrambles the program region in 256-byte blocks. Any firmware disassembly must account for this transformation. Do not apply this transformation to the samples region unless hardware evidence establishes that it is needed.

### CPU, memory map and peripherals

- CPU configured as Intel 8032 / MCS-51 family at 12 MHz in MAME. Some third-party listings report 11.44 MHz; exact hardware clock remains to be verified.
- Program address map: `0x0000-0xffff` mapped to ROM; MAME comments that banking may be needed.
- External data map includes RAM at `0x0000-0x38ff`.
- ACIA 6850 mapped at `0xa000-0xa003`, with IRQ connected to the CPU.
- HD44780 LCD controller mapped at `0xb000-0xb003`.
- TechnoX LCD configured as 2 rows x 16 characters.
- MAME marks the machine `MACHINE_NO_SOUND | MACHINE_NOT_WORKING`. The sound engine is not emulated.

## Major unresolved issue: sample memory capacity

Period review/specification sources describe approximately 6 MB of sample memory; another reference describes a 64-Mbit ROM sample bank. The single MAME sample dump is only 128 KiB. These figures do not reconcile.

Possible explanations include additional undumped ROMs, a partial/incomplete MAME ROM set, different memory terminology, or an error in a secondary specification. Do not assume IC51 is the complete sound library. Board photographs and a full chip inventory are required.

## What is needed next

### Priority A — essential files from the user or a lawful owner dump

1. The complete `technox.zip` archive or the two extracted files:
   - `mx27c1000_v105.ic4`
   - `nm27c010_v105.ic51`
   Preserve the original bytes; do not rename/re-save in an editor.
2. The TechnoX Owner's Manual PDF (53 pages), especially MIDI implementation, SysEx format, sound parameters, drum parameters, and factory bank appendices.
3. Any TechnoX SysEx dumps:
   - factory sound-name banks;
   - factory performance bank;
   - drum kits;
   - multisetups;
   - full device dump, if available.
4. High-resolution photographs of the complete main PCB, both sides, with every IC marking readable. Include close-ups of all ROM/EPROM/Flash chips and labels.
5. If possible, raw dumps of every ROM chip on the board, not just the two files currently listed by MAME.
6. Short, dry audio reference recordings from a real TechnoX, with MIDI files or exact MIDI notes/velocities/CC automation used to produce them.

### Priority B — strongly useful

- MAME source file `qmquasar.cpp` (already reviewed; keep a pinned copy/revision in project references).
- Any Quasimidi Raven / Quasar / TechnoX firmware or ROM version documentation, clearly identified by model and revision. Related-model ROMs must not be substituted as TechnoX ROMs.
- Service notes, repair photos, chip datasheets, oscillator/clock measurements, and PCB board numbers.
- MIDI implementation chart and SysEx examples from the manual.

## Analysis workflow after ROM files are available

1. Verify file sizes, CRC32 and SHA1 against the values above.
2. Keep pristine copies and create working copies.
3. Analyze IC4 both raw and after MAME's 256-byte address descrambling. Identify reset vectors, interrupt vectors, strings, tables, UART/ACIA access, LCD routines, and memory accesses.
4. Analyze IC51 independently: entropy, repeated blocks, byte distributions, headers, likely sample boundaries, possible signed/unsigned PCM and ADPCM patterns. Do not assume a codec before evidence.
5. Compare ROM contents and board inventory. Determine whether further sample ROMs are missing.
6. Parse manual SysEx formats and build validated import/export tests.
7. Only after the memory map and sample format are supported by evidence, implement the audio engine.

## Current project status

The JUCE project is an interface/build scaffold only. Its audio processor intentionally outputs silence. The research above does not yet provide a working synthesizer engine, complete waveform data, or verified AU-in-Logic operation.
