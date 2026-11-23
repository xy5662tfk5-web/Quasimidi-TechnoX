# IC51 loader and external SoundHost protocol findings

Status: instruction-level firmware trace and deterministic machine-test evidence. The external audio device identity and command semantics remain unknown.

## 1. IC51 is actively read by the original firmware

The 8032 machine test boots the descrambled IC4 v1.05 firmware with the raw IC51 v1.05 image attached. The firmware reaches the IC51 bulk-loader path and performs these reads:

- 13,651 reads from IC51 bank 0 for the bulk block (4-byte block header plus payload).
- 64 reads from IC51 bank 1 for descriptor-table data.
- The bulk transfer contains exactly 13,647 payload bytes.
- The test derives the source pointer and length from the IC51 descriptor at byte offset `0x90` (table entry `0x48`), then verifies every byte written to external sound-memory XDATA against the corresponding source byte in IC51.

This is stronger than a raw byte scan: the byte-for-byte comparison is performed after the original IC4 firmware executes the loader.

## 2. External sound-memory transfer

The firmware routine at IC4 `0xAF1A` reads the block header and streams payload bytes through `0xAF05`. The write helper at `0xAEDD` maps the logical low address through the 256-byte table at IC4 `0xADDD`. The mapping is a permutation of all 256 low-byte values; the project tests both the forward map and its inverse for every value.

Observed transfer facts:

- Destination XDATA window: `0xC000–0xFFFF` (16 KiB in the current machine model).
- Payload length: 13,647 bytes.
- Final written address: `0xF5B2`.
- Final byte: `0xD2`.
- Every transferred byte matches IC51 source data after applying the firmware's address mapping.

The 16 KiB artifact `technox-external-sound-memory` is a snapshot of this firmware-loaded XDATA window after the transfer. It is not a PCM/WAV extraction and does not establish sample encoding, sample boundaries, sample rate, or loop metadata.

## 3. Four-byte external command window

Reachable firmware uses external XDATA addresses `0x8000–0x8003`. The helper at `0x0DB7` writes four sequential bytes through `MOVX @R0,A`:

| Register offset | Firmware source at helper entry |
|---|---|
| `0x8000` | R5 |
| `0x8001` | R2 |
| `0x8002` | R3 |
| `0x8003` | R4 |

The related helper at `0x0DCE` conditionally writes `0x40` or `0xC0` to offset 3 as a control/handshake phase, then reads the other register bytes back. This demonstrates a structured host/peripheral protocol, but does not identify the chip or the semantic meaning of the four fields.

With an explicitly injected synthetic ready response in the test fixture, firmware startup emits 544 complete four-byte transactions:

- 512 transactions from caller `0x0E6A`: indices descend through `0x00–0xFF` twice, with payload words `0x0000` and `0x8000`.
- 32 transactions from caller `0x0E1D`: indices `0x0F, 0x1F, ... 0xFF`, with observed payload words `0x07FF` and `0x87FF`.

These values are recorded as observed initialization traffic only. Register names and meanings must remain opaque until corroborated by hardware documentation, chip identification, or real device captures.

## 4. Other bus evidence and limitations

- SFR `0x8E` selects external sound-bus modes in the current machine model. Firmware's `AF05` helper selects mode 2 for the XDATA write and restores mode 1 afterward.
- Firmware toggles P3.5 while reading the IC51 image; the test verifies the bank-0/bank-1 selection behavior against the actual ROM bytes.
- The `0xFF10` external port is accessed as a sound-device data/status port in the machine model. The value `0xAC` used to pass the startup wait is an explicit test injection, not an observed response from real hardware.
- The firmware's 544 initialization transactions occur only after the synthetic ready response is supplied. The current model does not claim to reproduce the real peripheral's ready/status behavior.
- A subsequent MIDI Program Change + Note On probe receives all bytes through the emulated ACIA, but the current test run still produces no additional SoundHost command transactions or sound-memory reads. The MIDI-to-voice-command path remains unresolved.

## 5. Next reverse-engineering tasks

1. Identify the physical device connected to the `0x8000–0x8003`, `0xFF10`, and mode-switched `0xC000–0xFFFF` buses from board photos, chip markings, and wiring.
2. Trace the firmware wait/status loop and reset/ready handshake without injecting `0xAC`.
3. Follow the MIDI receive queue from the ACIA interrupt through the Program Change and Note On handlers, especially the current call paths around `0x57A8`, `0x5671`, and `0x4256`, until the code that should submit a sound command is found.
4. Decode the 13,647-byte IC51 payload and its descriptor references only after the physical memory map and codec are established.
5. Obtain real-hardware captures to validate the register protocol and sample playback. Do not substitute the generic sine oscillator for this path or label it authentic.

## 6. Reproducibility

The evidence above is exercised by `tests/test_technox_8032_machine.cpp` and the `macOS Intel build` workflow's unit-test job. The workflow uploads:

- `technox-ic4-command-handlers` — reachable firmware disassembly and MOVX context.
- `technox-external-sound-memory` — 16 KiB snapshot after the IC51 bulk transfer.
- `technox-ic51-raw-research` — raw IC51 dump used by the test.

The machine test uses a synthetic SoundHost ready response for the post-loader startup path; that fixture must not be confused with a hardware-confirmed response.
