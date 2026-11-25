# External sound-device bus and control-line audit

Branch: `research/original-technox-audio-engine`

This document records the next evidence checkpoint for Section B of `research/SOUND_CORE_MASTER_CHECKLIST.md`. It deliberately keeps all unverified chip, pin and register semantics neutral.

## Scope and method

The IC4 v1.05 binary is validated by the existing ROM toolchain and descrambled with the MAME-derived address transform. A fresh reachable-code pass from reset and interrupt vectors decoded 15,474 instruction locations. This is a static reachability result, not a full hardware trace: indirect jumps, dynamic tables, banked paths and runtime conditions still limit completeness.

The audit separates three external interfaces that have previously been conflated:

1. XDATA `0x8000–0x8003`: the four-byte command/status window captured by the SoundHost trace.
2. XDATA `0xFF10`: a mode-dependent data/status aperture used by resource streaming and ready-marker polling.
3. XDATA `0xC000–0xFFFF`: the external-memory window populated by the IC51 bulk transfer. The verified 16-KiB image is not a decoded PCM ROM.

## 1. SFR 0x8E and firmware bit 0x1B

### Bit 0x1B provenance

In the currently reachable descrambled IC4 image, the only instruction found that writes IRAM bit `0x1B` is at `0x3829`:

```asm
0x381F  AF 8E       MOV  R7,0x8E
0x3821  EF          MOV  A,R7
0x3822  B4 01 03    CJNE A,#0x01,0x3828
0x3825  D3          SETB C
0x3826  80 01       SJMP 0x3829
0x3828  C3          CLR C
0x3829  92 1B       MOV  0x1B,C
```

At this check, bit `0x1B` is set exactly when SFR `0x8E == 0x01`; otherwise it is cleared. Other reachable references to bit `0x1B` are tests, notably in external-memory access helpers. The flag is therefore a firmware cache of one specific bus-mode condition at the time the test runs, not a DSP-ready flag and not a general hardware acknowledgement.

### Conditional mode switch around MOVX

The helper at `0xAF05` has this exact shape:

```asm
0xAF05  C0 00       PUSH 0x00
0xAF07  12 AE DD    LCALL 0xAEDD
0xAF0A  30 1B 03    JNB  0x1B,0xAF10
0xAF0D  75 8E 02    MOV  0x8E,#0x02
0xAF10  F2          MOVX @R0,A
0xAF11  30 1B 03    JNB  0x1B,0xAF17
0xAF14  75 8E 01    MOV  0x8E,#0x01
0xAF17  D0 00       POP  0x00
0xAF19  22          RET
```

When bit `0x1B` is set, IC4 temporarily changes SFR `0x8E` from 1 to 2 for the `MOVX @R0,A` operation, then restores it to 1. When the flag is clear, both mode writes are skipped. This is direct evidence of a guarded external-memory bus-mode transition. It does not identify the recipient chip, the physical bus pins, or the meaning of either mode value.

A second confirmed use of mode 2 occurs in the IC51 sound-memory upload path. The C++ machine model currently treats `(SFR 0x8E & 3) == 2` as the RAM path for `0xFF10` and external sound-memory operations; keep that as a documented emulation mapping, not a decoded hardware register name.

## 2. P3.2 is a control-line candidate, not a confirmed reset pin

The 8051 bit address `0xB2` maps to P3.2. Reachable IC4 code contains these meaningful operations:

- `0x34EB: C2 B2` — clear P3.2 during hardware setup.
- `0xAF1A: C2 B2` — clear P3.2 at entry to the IC51 bulk-loader.
- `0xAF73: D2 B2` — set P3.2 after the bulk payload transfer.
- `0xB34D: C2 B2` — clear P3.2 before the external `0xC000` memory test.

The bulk-loader sequence at `0xAF1A` clears P3.2, performs the IC51-to-external-memory transfer, sets P3.2 at `0xAF73`, then continues into a delay and a poll of `0xFF10` for byte `0xAC`. This makes P3.2 a real control signal around the transfer. However, the separate `0xB34D` memory-test path also clears P3.2 and does not set it again before its local return at `0xB366`. The existing MIDI/SoundHost trace also contains transactions with P3 snapshot `0xFA`, where bit 2 is low.

Therefore, the current C++ name `soundDeviceResetReleased` is a **working hypothesis/compatibility label only**. The observed firmware proves P3.2 transitions; it does not yet prove active-low DSP reset. Do not use this boolean as evidence that the sound processor is reset or running. Before implementing any reset sequence, resolve why P3.2 is low in the memory test and in some SoundHost transaction contexts.

## 3. P3.1, P3.4 and P3.5 are actively sequenced

The reachable routine around `0xB385–0xB3BB` tests bit addresses `0xE0`, `0xE1` and `0xE2` (ACC.0–ACC.2), then conditionally clears P3.5, P3.4 and P3.1, performs external reads, and restores those output bits. The resource/IC51 code also repeatedly toggles P3.5, P3.4 and P3.1 around byte reads.

This confirms that P3 is not merely a static reset word: several individual lines participate in address/select/transfer sequencing. Existing code already uses P3 patterns to decide when IC51 is selected. Keep signal names neutral (P3.1, P3.2, P3.4, P3.5) until the electrical mapping is checked against a board schematic or pin capture.

Only two reachable whole-port P3 writes were found in this pass: `0x34DF: MOV P3,#0xFF` and reset-vector `0xEB35: MOV P3,#0xFF`. Most later changes are bit-address writes. The MCS-51 core's bit-write path updates the whole port latch through `writeInternal`, so the machine trace can capture both whole-port writes and bit transitions if it stores the instruction PC and previous/new latch values.

## 4. What the present traces do and do not prove

### Four-byte host window: 0x8000–0x8003

The firmware writes sequentially to offsets 0, 1, 2 and 3; the emulator records each byte and the bus context independently. `0x0DB7` is a four-byte writer. The `0x0DCE`/related control path can write `0x40` or `0xC0` to offset 3 and then read offset 2. Those values are observed phase/control writes, not yet decoded as ACK, BUSY, reset, or a command opcode.

The present emulator's read response defaults to `0xFF`; any alternate response used to force firmware progress is a test fixture. A captured four-byte transaction is not yet a processor command specification.

### FF10 aperture

Firmware uses `0xFF10` for resource uploads and also reads it in a status/ready-poll context. The observed `0xAC` is a marker value checked by IC4, but the current test-ready response is synthetic. It is not a hardware capture and cannot validate the actual ready/busy timing.

### External-memory image

IC51 bank 0 source `0x3C64`, length `0x354F` (13,647 bytes), is transferred through the verified address map at IC4 `0xADDD` into the `0xC000–0xFFFF` window. The byte-exact memory reconstruction is established. The memory device, physical topology and contents' interpretation as PCM are not established.

## 5. SAM9407 compatibility check

The Dream SAM9407 datasheet describes a 16-bit host interface with three address inputs, chip-select/read/write controls and a 16-bit data bus, plus separate sample-memory address/data signals and an active-low reset. This is a plausible general architecture for a sample-based synth DSP. The TechnoX firmware evidence presently available is a four-byte 8-bit XDATA aperture plus a separate `0xFF10` transfer/status path and a mapped external-memory window.

Those facts are not yet reconciled into a SAM9407 pin-level interface. External glue logic could translate the 8032 interface, but no verified TechnoX PCB marking, schematic or bus capture has yet established that. Keep SAM9407 as a research lead, not the emulated chip identity. Reference: [Dream SAM9407 datasheet](https://dosdays.co.uk/media/dream/SAM9407.PDF).

## 6. Next experiments — ordered, no duplicate MIDI work

1. **P3 write timeline:** extend the existing port-write trace with instruction PC, previous latch and new latch. Capture both direct P3 writes and bit-address writes. Run cold boot through the complete IC51 transfer, the `0xB34D` memory test and the first 544 initialization SoundHost transactions. Report every P3.2 transition and the P3 value at each host packet.
2. **Mode/port correlation:** emit one chronological merged timeline for SFR `0x8E` writes, P3 transitions, P2 changes, `0xFF10` reads/writes, IC51 reads and `0x8000–0x8003` transactions. Retain instruction PC, caller PC, cycle and reset-candidate bit state.
3. **Control response differential:** vary only the external read response for the actual `0x8002` and `0xFF10` reads in isolated fixtures. Record which firmware branches change and which are purely timing/polling. Clearly label all such values synthetic.
4. **Device identity:** compare the inferred signals with a legible TechnoX PCB photo, chip top markings and any schematic/service documentation. A Quasar/Raven family resemblance or a SAM9407 datasheet match is insufficient by itself.
5. **Only after protocol evidence:** implement a named host-port device model. Until then preserve neutral register indices, raw byte order, exact cycle timing, and configurable external responses; do not build sample playback or DSP register meanings on guesses.

## Acceptance criteria for Section B

Section B is complete only when:
- the P3 control-line timeline is repeatable from cold boot through load and note events;
- the role and polarity of relevant P3 lines are tied to board evidence or captured hardware behavior;
- `0x8000–0x8003` register order, read/write semantics, control phases and wait/ready behavior are documented and regression-tested;
- `0xFF10` status versus resource-transfer behavior is separated and the real `0xAC`/ready response source is established;
- the physical processor and external memory topology are confirmed, or the implementation is explicitly framed as a generic but evidence-based external-device model rather than a named-chip emulation.
