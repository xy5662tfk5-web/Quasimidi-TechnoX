# SAM9407 power-up bootstrap boundary model

Status: datasheet-level subsystem model. It is not evidence that the TechnoX board's IC4 port is wired directly to a SAM9407, and it is not a P16 instruction-set emulator.

## Recovered from the SAM9407 objective specification

The DREAM SAM9407 specification (February 1996, section 8 “Reset and Power Down”) describes two reset-time paths:

- With **BOOT low**, P16 program execution starts from external ROM word address `0x0100`.
- With **BOOT high**, P16 starts in its internal bootstrap ROM. The bootstrap expects exactly **256 16-bit words** from the 16-bit burst-transfer port, stores them in external DRAM word addresses `0x0100–0x01FF`, and then resumes execution at `0x0100`.

The same specification describes the P16 control processor and the synthesis/DSP core as distinct execution domains that share external memory. This model covers only the bootstrap word-transfer boundary; it does not emulate P16 instructions, DSP algorithms, synthesis frame slots, or external DRAM arbitration.

Primary source: [DREAM SAM9407 objective specification (PDF)](https://dosdays.co.uk/media/dream/SAM9407.PDF), sections 2, 3, 7 and 8.

## Implemented component

`Source/SAM9407BootstrapReceiver.h` provides a small deterministic state model:

- `reset(bootPinHigh)` samples the BOOT state.
- `writeBurstWord(uint16_t)` accepts one already-decoded 16-bit word only in bootstrap mode.
- The first received word is stored at word address `0x0100`; the 256th is stored at `0x01FF`.
- The execution state switches to external-memory execution only after all 256 words arrive.
- `getEntryProgramCounter()` reports the external entry point `0x0100` only after that hand-off (or immediately when BOOT is low).
- `readBootstrapWord()` exposes only words actually received through this bootstrap path.

The unit test covers BOOT-low startup, the 255/256-word boundary, first/last addresses, rejection of extra words, reset and a new partial transfer.

## Explicitly not connected to TechnoX IC4 yet

The TechnoX firmware trace has a neutral four-byte write/read protocol at XDATA `0x8000–0x8003`, a separate mode-dependent `0xFF10` upload/status aperture, and a separate `0xC000–0xFFFF` paged memory image. None is yet mapped pin-for-pin to SAM9407 host address pins `A0–A2`, `CS/`, `RD/`, `WR/`, `SBHE/`, or its 16-bit data bus.

There is also a specification-level caution: the host-interface prose identifies the 16-bit burst port in the A2/A1/A0 register map, while timing notes describe upper-byte/SBHE conditions using A2/A1. The implementation intentionally receives a complete 16-bit word after host-port decoding rather than guessing this electrical/address selection.

Consequently this component is useful for the next verified stage—testing a real captured 256-word bootstrap stream—but **does not** initialize the TechnoX plugin, explain the existing 544 IC4 transactions, or make the emulator audible. The IC4-to-DSP bridge, actual sample memory dump/address map, P16 program and original synthesis DSP remain separate work items.
