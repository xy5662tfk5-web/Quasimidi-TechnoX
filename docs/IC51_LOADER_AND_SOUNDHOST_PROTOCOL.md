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
- The combined MIDI probe (Program Change 5 followed by Note On, note 60, velocity 100) is delivered through the emulated ACIA. In the synthetic-ready fixture, the test observes 553 additional four-byte command-port packets and confirms calls through firmware addresses 0x57A8, 0x5671, 0x4256 and 0x0DB7. This supersedes the earlier "no additional transactions" observation.
- The 553-packet burst is not yet attributable to the Note On alone: Program Change and Note On are sent in one batch, and the test does not yet run isolated differential cases. The packet trace identifies two 256-write sweeps called from 0x0E67 (return PC 0x0E6A), plus a 32-iteration path through 0x0DF7 / 0x0E1A, split into two 16-packet groups around a control write from 0x0DE4 (return PC 0x0E0B). These are confirmed caller/shape relationships, not decoded peripheral semantics.
- The MIDI regression does not establish original voice triggering, sample reads, or audible playback. The test explicitly avoids asserting a completed MIDI-to-voice dispatch until the external SoundHost path is modeled.

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


## 7. SFR 0x8E qualification — direct-byte and bit-address forms are different

The earlier phrase "SFR 0x8E selects sound-bus modes" describes the current machine-test latch, not a generic Intel 8032 register definition.

The descrambled IC4 v1.05 image contains both direct-byte operations (`MOV 0x8E,#0x02`, `MOV 0x8E,#0x01`, `MOV R7,0x8E`, and `MOV 0x8E,R4`) and a bit-address operation `SETB bit 0x8E` at PC `0x3510`. In the standard MCS-51 bit map, bit address `0x8E` is `TCON.6/TR1` (Timer 1 run control); it is not bit 0 of the direct SFR byte at address `0x8E`.

A strong derivative match is Atmel AT89C51RD2/ED2: direct byte SFR `AUXR` is at `0x8E`; bit 1 EXTRAM selects on-chip XRAM versus the external MOVX bus, and bit 0 AO suppresses free-running ALE while retaining ALE during MOVX/MOVC. On that family, firmware values `0x02` and `0x01` mean EXTRAM=1/AO=0 and EXTRAM=0/AO=1. This explains why IC4 brackets external MOVX writes with those values, but it does **not** identify the fitted MCU.

The Dallas DS5001/DS5002 SFR map has no direct-byte SFR at `0x8E`; therefore an earlier DS5002FP reference is not proof of the IC4 CPU model. See the dedicated investigation in the repository's hardware notes and the Atmel/Dallas data sheets:
- Atmel AT89C51RD2/ED2: https://media.digikey.com/pdf/Data%20Sheets/Atmel%20PDFs/AT89C51ED2%2CRD2_Rev06.pdf
- Dallas DS5001/DS5002 guide: https://www.keil.com/dd/docs/datashts/dallas/secure_ug.pdf

### Confirmed XDATA address distinction

- `P2=0xFF, R0=0x10` constructs XDATA address `0xFF10`; it is used by the IC51 loader/status path and by the FX stream upload routines.
- `P2=0x80, R0=0..3` constructs `0x8000..0x8003`; helper `0x0DB7` writes `[R5,R2,R3,R4]`, while `0x0DCE` writes `0x40/0xC0` handshake values to offset 3 and reads response bytes.

These are distinct 16-bit XDATA addresses and different firmware call paths, not aliases. `0xFF10` is used for both reads and writes. The current generic 8032 test run has firmware bit `0x1B=0`, so its conditional direct-`0x8E` transitions are skipped; the synthetic `0xAC` response proves only the loader's expected code path, not physical ready behavior or an AUXR-enabled bus waveform.

Do not hard-code an Atmel CPU identity. The next hardware-model test is to add a selectable AUXR-compatible bus profile, keep the generic 8032 behavior unchanged, and compare actual IC4 execution across both. Capture P2, R0/DPTR, P0, P3.6/P3.7, direct SFR `0x8E`, PC, and cycle at each MOVX so logical XDATA transactions are not mistaken for pin-level ALE/RD/WR proof.


### AUXR-compatible XRAM aperture and the firmware's 0x1B mode test

The official AT89C51RD2/ED2 data sheet narrows the possible XRAM overlay: its on-chip XRAM is configurable from 256 to 1792 bytes (768 bytes by default) and occupies only the *first bytes* of the external data address space. With EXTRAM=0, MOVX accesses inside that configured low-address window are intercepted internally; accesses above the configured XRAM size still reach the external bus. With EXTRAM=1, MOVX uses the standard external bus. See the official expanded-RAM section and AUXR table: https://ww1.microchip.com/downloads/en/DeviceDoc/doc4235.pdf.

This means that, even on this Atmel candidate, `0x8000–0x8003`, `0xFF10`, and `0xC000–0xFFFF` are outside the internal XRAM window and remain external addresses in both EXTRAM states. The compatibility uncertainty is instead the low IC51 pointer/resource windows (e.g. `0x0000–0x02FF`) which may overlap internal XRAM when EXTRAM=0. The present generic 8032 model routes these low addresses according to its external IC51 select logic and does not model that derivative-specific overlay.

A second instruction-level discriminator is the firmware's initialization at `0x381C–0x3829`:
- `0x381C` calls board/peripheral setup `0x34D8`.
- `0x381F` reads direct SFR byte `0x8E` into R7.
- `0x3822` compares the byte with `0x01`.
- `0x3825` sets carry only on equality; `0x3828` clears carry on the other path.
- `0x3829` writes carry into firmware bit `0x1B`.

Thus the special `0x1B` wrapper mode is enabled only when the initial direct-byte value at `0x8E` equals `0x01`. The `AF05` and FX upload helpers then switch `0x8E` to `0x02` for each external MOVX write and restore `0x01` afterward. This supports a two-state external-bus mode interpretation, but the initial reset value and exact fitted CPU must still be tied to the PCB marking. It is not sufficient to assume AT89C51RD2/ED2 solely because its AUXR layout resembles the firmware sequence.
