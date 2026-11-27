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


## Candidate sound processor lead: Dream SAM9407 (not confirmed)

A secondary sound-chip reference lists the Dream/Atmel SAM9407 among chips used in Quasimidi instruments including the TechnoX. This is a useful lead, not primary proof of the TechnoX bill of materials. No board photograph, readable marking, or schematic in this repository currently identifies a SAM9407 on the TechnoX PCB.

The SAM9407 objective datasheet (February 1996) describes an integrated sound engine with:
- a 16-bit CISC P16 control processor and a separate RISC synthesis/DSP core;
- MIDI UART and host-interface FIFO/burst logic;
- wavetable synthesis, 16-bit samples, 44.1 kHz operation, interpolation, alternate loops and a 24 dB resonant filter per voice;
- external ROM/SRAM/DRAM shared through an MMU for samples and delay lines;
- an external host interface using A0–A2, CS, RD and WR, plus READY/CS16 handshake signals;
- up to 64 MB of 8-bit sample address space in the maximum memory configuration;
- a 144-lead TQFP package.

The host-register description is particularly worth comparing against the TechnoX firmware: SAM9407 A0–A2 selects eight registers; register 0 is the MPU-401 data register, register 1 is status/control, register 2 is the 16-bit burst-transfer port, and the upper address range is game-compatible sound registers. The TechnoX IC4 code instead writes four sequential byte offsets at `0x8000–0x8003`, and has a separate `0x0DCE` helper that changes register-3 control bits and reads back offsets 2/3. That is architecturally suggestive of a host/peripheral protocol, but it is **not enough to equate the two address maps**: SAM9407's bus is 16-bit and uses its own A0–A2/CS/RD/WR pin timing. We have not shown that the 8032's XDATA port is wired directly to those pins.

There is also a package discrepancy to resolve. An earlier project note mentions an 80-pin QFP in the unidentified hardware path, whereas the SAM9407 datasheet specifies TQFP144. Until the board is inspected, these may refer to different components or the earlier package note may be mistaken. The SAM9407 must remain a candidate, not a chosen emulation target.

The memory-size discrepancy also has a more concrete context now. A contemporaneous 1995 review describes the TechnoX as having six megabytes of sample memory, while the SAM9407 datasheet states a maximum configuration of 64 MB of 8-bit sample address space. These are not necessarily contradictory: one is a reported installed capacity and the other a chip-family maximum. Neither establishes that the TechnoX uses SAM9407, and neither makes the current 128 KiB IC51 dump a complete waveform set.

### Discriminating evidence needed before implementing a SAM9407 model

1. A clear top-side and underside PCB photo showing the large sound IC, all memory ICs, package pin count, and silkscreen designators.
2. The exact IC top marking and package. A 144-lead package would fit the SAM9407 datasheet; an 80-lead part would rule out that exact package/device.
3. Continuity or schematic evidence linking IC4 bus/control lines to the candidate chip's A0–A2, CS, RD, WR, READY and data pins.
4. Correlation of the firmware's `0x0DCE` read/strobe sequence with the candidate's actual handshake pins, followed by one real-hardware note capture.

Do not yet replace the generic backend with a SAM9407 emulator. First establish the chip identity and the electrical mapping.

References:
- Dream SA, *SAM9407 Integrated Sound Studio*, objective specification (February 1996): https://dosdays.co.uk/media/dream/SAM9407.PDF
- Contemporary MX review, *Revolt into style* (April 1995), describing the TechnoX's six-megabyte sample memory: https://www.muzines.co.uk/articles/revolt-into-style/12193?theme=2
- Secondary chip-reference listing (lead only, not confirmation): https://en.wikipedia.org/wiki/List_of_sound_chips
