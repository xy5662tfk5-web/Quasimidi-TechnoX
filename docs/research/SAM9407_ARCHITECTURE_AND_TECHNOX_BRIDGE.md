# DREAM SAM9407 — candidate architecture and TechnoX bridge constraints

Status: unverified candidate for architectural comparison only; physical board population and the 8032-to-chip bridge are not verified.

## Why SAM9407 is worth evaluating

The SAM9407 objective specification (DREAM, February 1996) describes a single-chip sound system with:
- a 16-bit CISC P16 control processor;
- a separate RISC synthesis/DSP core;
- algorithm RAM and five dedicated synthesis parameter RAM banks;
- 64 frame-process slots, with 16 micro-instructions per algorithm and up to 32 resident algorithms;
- wavetable synthesis with interpolation, alternate looping and a 24 dB resonant filter per voice;
- a memory-management unit that shares external ROM/RAM between P16 code/data and DSP sample/delay-line access;
- an intelligent I/O block, MIDI UART, timers, codec interface, host FIFO and burst interface.

The public application list names Quasimidi TechnoX, and family-generation material links the M.A.S.S. platform to Quasar/Raven. This makes SAM9407 a candidate worth evaluating, but it does not establish that this exact TechnoX board contains the chip. A readable IC marking and a matching pin/bus capture are required before a SAM9407-specific emulator is treated as the target.

## Three domains that must not be collapsed

### 1. IC4 controller domain

IC4 is the Intel 8032 firmware already executing in the project. It performs MIDI handling, resource selection, voice bookkeeping, parameter arithmetic and external-device transactions. Its observed external apertures include:
- four byte addresses at `0x8000–0x8003`;
- the mode-dependent `0xFF10` path;
- the `0xC000–0xFFFF` external memory window, selected through SFR `0x8E` and P3 controls.

The meanings decoded at the IC4 side must remain firmware meanings until a physical bridge is demonstrated.

### 2. SAM9407 host/control domain

The SAM9407 datasheet specifies a 16-bit host data bus `D0–D15`, address lines `A0–A2`, and separate `CS/`, `RD/`, `WR/` and `SBHE/` controls. The eight host register selections are grouped as follows:
- `A2:A1=00`: primary byte-wide MIDI/UART interface;
- `A2:A1=01`: 16-bit burst data port;
- `A2=1`: auxiliary/game-compatible register range.

This is not a direct match for the 8032's currently observed four-byte external aperture. The TechnoX board could include glue logic, latches, bus-width adaptation or another intermediary, but none is currently decoded. Do not equate IC4 address `0x8000` with SAM9407 host register zero, or map `0x8001–0x8003` to named SAM9407 registers, without evidence.

### 3. SAM9407 external sample-memory domain

The SAM9407 exposes a separate memory interface for sample ROM/SRAM/DRAM, including:
- `WA0–WA24` external memory addressing;
- `WD0–WD15` sample-memory data;
- `WCS0/`, `WCS1/`, `WWE/`, `WOE/`, and byte-select/DRAM timing signals;
- `S0–S1` cycle-type outputs distinguishing idle, synthesis access, instruction fetch and P16 processor read/write.

The IC4-visible `0xC000–0xFFFF` window and the 13,647-byte IC51 remapped startup image are not themselves proof of the physical waveform memory layout. They may represent a transfer/buffer, control RAM, mapped external memory, or another intermediate resource. The *The Mix* review of TechnoX (April 1995) explicitly lists six-megabyte sample memory. Later online listings say 64 MB, so the actual installed capacity must remain unverified until board markings or a dump resolve the conflict. No complete byte-exact waveform dump has been recovered.

## Independent software cross-check: Linux SAM9407 driver

The open-source [libmikmod SAM9407 backend](https://github.com/sezero/mikmod/blob/d27e9c85aabce9a8e3cb8762913cf39b0fc1a0b8/libmikmod/drivers/drv_sam9407.c) drives the generic Linux SAM9407 driver through a structured event API. Its event sequence includes voice open/close, sample-memory assignment, output volume, main stereo send, pitch, start and stop. The memory event carries a sample handle, 16-bit sample type, forward/reverse loop mode, start position, loop position and end position. It also demonstrates independent pitch, volume and panning updates.

This is useful as a **generic semantic cross-check**: a SAM9407 voice model should be able to represent sample source/loop boundaries, pitch, stereo level and lifecycle separately. It does **not** provide the TechnoX M.A.S.S. command mapping, does not prove the TechnoX board uses the same Linux-facing firmware, and does not identify any of the IC4 bytes at `0x8000–0x8003` with a SAM9407 register or event. Do not copy the Linux event numbers or API structures into the TechnoX bridge.

The EWS64 reference driver is also from another SAM9407-based product. Its firmware and `.94B` sound banks can be useful for studying generic SAM9407 memory loading and software architecture, but they are not TechnoX's reported waveform bank and cannot substitute for TechnoX's original IC51/DSP resources. See the [EWS64 hardware/software notes](https://vogonswiki.com/index.php/EWS64).

## Architecture-level implementation boundary

The implementation should be separated into independent components:

1. `TechnoX8032Machine`: the original IC4 machine, including its 8-bit bus and firmware-executed protocol.
2. `TechnoXSoundHostBridge`: only the behavior actually established from IC4 write/read sequences, including selector+payload framing and phase transitions. Keep external responses open-bus or explicitly supplied by a test fixture until measured.
3. `SAM9407HostInterface`: a separate datasheet-conformant 16-bit host-side component. Its eight register selection and byte/burst semantics can be modeled independently, but the TechnoX bridge mapping must be injected only after it is proven.
4. `SAM9407P16Runtime` and `SAM9407SynthesisDSP`: separate control and synthesis domains. Datasheet architecture is known, but P16 firmware, algorithm images and parameter-address maps for TechnoX have not yet been recovered.
5. `SAM9407ExternalWaveMemory`: a separate address-space model for actual waveform/sample data. Do not substitute IC51 program command streams for PCM.
6. `ADSP2105FX`: keep in a separate effects boundary. It remains a candidate FX1/FX2 processor in this specific instrument until chip marking or hardware trace confirms it.

## Evidence needed before connecting IC4 to SAM9407

- clear photograph of the TechnoX mainboard with all large IC markings legible;
- schematic or continuity measurements from IC4 P2/P3 and XDATA address/data/control to the suspected SAM9407 pins/glue logic;
- read/write capture for host data/address/strobes, including data width and byte lanes;
- identification of sample ROM/DRAM/SRAM parts and their address/data connections;
- power-up/reset timing and the relationship of SAM9407 `RUN`, reset and audio serial signals to the rest of the board;
- at least one known command/response captured both at IC4's aperture and at the SAM9407 host pins.

## Current project state

- IC4 control firmware executes and the observed host traffic is retained with PC/caller, P2/P3, SFR `0x8E`, machine cycle and a machine-wide event sequence.
- An offline timeline merges IC51, host aperture, FF10, external-memory, P3 and mode-register observations.
- IC51 resource catalogs and per-voice program bindings are connected to the backend.
- The original sample playback, any fitted-chip-specific P16/DSP execution, physical host-bridge mapping and ADSP-2105 FX rendering are **not implemented**.
- SAM9407 remains an unverified candidate, not a confirmed TechnoX chip identification.
- `BasicMASSVoiceBackend` remains a diagnostic oscillator and is not an authentic TechnoX sound source.

## Primary source

DREAM SAM9407, *Integrated Sound Studio*, objective specification, February 1996: [datasheet PDF](https://dosdays.co.uk/media/dream/SAM9407.PDF). Architecture, P16/DSP division, process slots and RAM banks are described in section 2; host bus and pin groups are described in sections 3 and 7.

## Evidence update — 27 November 2026

The SAM9407 data sheet provides a more specific target model than a generic sample player:

- The objective specification lists 16-bit samples at 44.1 kHz, alternate looping and a 24 dB digital filter for each voice.
- Its external-memory pins expose a 25-bit word address and a 16-bit data bus; the data sheet describes support up to 32M 16-bit words / 64M 8-bit samples, depending on the memory configuration.
- The chip contains a P16 control processor and a separate programmable RISC synthesis/DSP engine. The P16 updates DSP parameter RAM; the DSP executes resident algorithms and accesses sample/delay-line data through the MMU.
- This means the often-repeated 64 MB figure is at least a documented *maximum capability* of the SAM9407 memory interface. It must not be treated as proof that the TechnoX contains a 64 MB waveform image. The contemporary April 1995 *The Mix* TechnoX review reports 6 MB of sample memory. Until the actual board/memory devices are identified, retain 6 MB as the contemporary product report and 64 MB as the chip/interface capability or later secondary listing—not as interchangeable measurements.

A separately available Linux SAM9407 driver package (version 1.0.4, 2003) and the open-source libmikmod SAM9407 backend provide a useful generic software cross-check. The libmikmod backend loads signed 16-bit samples into sample handles and submits separate voice events for sample assignment, start/loop/end positions, forward/reverse loop mode, pitch, stereo send, output volume, start and stop. This confirms the *kind* of state a generic SAM9407 voice needs. It does not expose the TechnoX M.A.S.S. P16 firmware, the TechnoX waveform bank, or the bridge from IC4's 8-bit ports; those card-driver event codes must not be copied into the TechnoX protocol.

The TechnoX association with SAM9407 is supported by public chip-application references, and the architecture is a strong working target. However, the SAM9407 data sheet itself does not name TechnoX, and no readable marking/pin trace from this particular board has yet been recorded. Physical confirmation remains open.

### Revised immediate sequence

1. Keep the IC4/MIDI/IC51 work frozen as the verified controller-side foundation.
2. Use the SAM9407 data sheet and generic driver only to model the *separate* P16/DSP/sample-memory domains. Do not map any IC4 packet byte to a SAM register by resemblance.
3. Resolve the board bridge: correlate IC4 writes at `0x8000–0x8003`, `0xFF10`, P2/P3 and SFR `0x8E` against the visible DSP/glue-logic and sample-memory pinout. A board IC marking or measured bus capture is the decisive discriminator.
4. Acquire a repeated, hash-verified dump of the actual sample memory (expected size must come from the fitted memory topology), then decode one known waveform and connect it to one factory program.
5. Only after that golden sample renders correctly should the project expand to other programs, voice modulation and FX.

Sources: [DREAM SAM9407 objective specification](https://dosdays.co.uk/media/dream/SAM9407.PDF); [Linux SAM9407 driver 1.0.4 package index](https://www.ibiblio.org/pub/micro/pc-stuff/Linux/kernel/sound/); [libmikmod SAM9407 backend](https://github.com/sezero/mikmod/blob/d27e9c85aabce9a8e3cb8762913cf39b0fc1a0b8/libmikmod/drivers/drv_sam9407.c); [April 1995 *The Mix* TechnoX review](https://www.muzines.co.uk/articles/revolt-into-style/12193?theme=2).
