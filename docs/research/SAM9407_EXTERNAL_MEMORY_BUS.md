# SAM9407 external memory bus model

Status: functional memory-interface model from the SAM9407 objective specification. It is not a P16 CPU, DSP emulator, DRAM timing simulator, or proof that the TechnoX board contains this exact device.

## Datasheet facts encoded

The February 1996 SAM9407 objective specification documents:

- a 25-bit **word** address bus, WA0–WA24, addressing up to 32M words;
- a 16-bit external memory data bus WD0–WD15 used with PCM ROM, SRAM or DRAM;
- WCS0/ for PCM ROM, WCS1/ for SRAM, WWE/ and WOE/ for writes and output enable;
- S1/S0 cycle classes: `00` idle, `01` synthesis access, `10` instruction fetch, `11` P16 processor read/write;
- an MMU shared by the P16 control processor and synthesis/DSP engine, with external ROM/RAM used for program data, samples and delay lines.

Source: [DREAM SAM9407 objective specification, February 1996](https://dosdays.co.uk/media/dream/SAM9407.PDF), sections 2–3 and 7. The pin description is also reproduced in the Programmer Reference.

## Implemented

`Source/SAM9407ExternalMemoryBus.h` is a bounded, allocation-free word-address bus. The plugin's raw waveform-file loader uses the same size validator before publishing the memory view:

- validates the full 25-bit word address range, including the exclusive end address `0x02000000` when a mapping includes final word `0x01FFFFFF`;
- validates candidate raw waveform-image lengths before load: non-empty, even number of bytes and no larger than `0x04000000` bytes (32M 16-bit words);
- maps up to eight non-overlapping memory regions;
- supports immutable PCM ROM views plus writable SRAM/DRAM backing;
- decodes/encodes S1/S0 cycle values;
- performs little-endian 16-bit reads, RAM writes, and bounds checks;
- rejects writes to ROM, idle cycles, instruction-fetch writes, overlapping windows and overflow past `0x01FFFFFF`.

The voice backend now reads its source words through this memory bus using the documented synthesis-access cycle. `MASSWaveformMemoryView` is still the owner-supplied raw image; the view is exposed as one read-only PCM-ROM window beginning at word address zero, matching the existing sample-map convention. A different physical base address must be supplied by a future board-verified map.

## Explicit limits

This model does not simulate multiplexed DRAM RAS/CAS timing, RBS for 8-bit SRAM, chip-select glue logic, bus arbitration, cache behavior, refresh, P16 instruction fetches, DSP instructions or host bootstrap electrical timing. It does not infer that IC4 `0x8000–0x8003` maps to the SAM9407 host interface. Board-level confirmation is still required before binding the IC4 transport to these cycle classes.

The model is a foundation for routing future validated sample reads and P16 memory accesses through the correct word-address/cycle abstraction. It does not supply the missing TechnoX waveform ROM/image or create original instrument audio on its own.
