# MIDI-to-SoundHost trace: confirmed command-port activity

Status: reproducible firmware-level trace from the Intel 8032 machine regression test. This supersedes the earlier tentative statement in MIDI_EVENT_ANALYSIS.md that the MIDI stimulus emitted no SoundHost transactions. The trace confirms command-port writes after the MIDI stimulus, but does not yet prove which packet starts a voice.

## Test stimulus

The test selects restored RAM Performance/program 5 and schedules these MIDI bytes through the emulated ACIA/INT1 receive path:

```
C0 05       Program Change, program 5
90 3C 64    Note On, note 60, velocity 100, channel 1
```

The test then runs the 8032 for 2,000,000 machine cycles and drains for another 5,000,000 cycles.

## Confirmed firmware execution

The regression assertions establish:

- `0x57A8` is entered for the general MIDI event path.
- `0x5671` is called exactly twice during this stimulus.
- `0x4256` is called exactly twice during this stimulus.
- The 4-byte external command-port packet count grows from 548 to 1101: **553 additional packets**.
- The external command writer at `0x0DB7` is called during the stimulus.
- The test asserts that the CPU does not fault and the scheduled ACIA bytes are consumed.

This confirms the Note On path reaches Part-indexed parameter staging and that external command-port activity occurs during the same execution window.

## Captured packet-burst structure

The test asserts these command packet groups by their recorded firmware return addresses:

| Packet indices | Count | Caller return PC | Verified shape |
|---|---:|---:|---|
| 554–809 | 256 | `0x0E6A` | First index sweep, first payload word 0 |
| 810–1065 | 256 | `0x0E6A` | Second index sweep, payload word `0x8000` |
| 1067–1082 | 16 | `0x0E1D` | First 16-entry group, byte 2 is `0x40` |
| 1083 | 1 | `0x0E0B` | Midpoint control write; packet bytes include `FF` at byte 0 and `C0` at byte 3 |
| 1084–1099 | 16 | `0x0E1D` | Second 16-entry group, byte 3 is `0x80` |

The regression also checks boundary packet values and the control write. The 553-packet delta includes packets outside the groups listed above; do not infer their meaning from the count alone.

## Important interpretation boundary

The packet burst is temporally associated with the MIDI stimulus, and the firmware definitely traverses `0x5671` and `0x4256`. However, the two 256-entry sweeps and the two 16-entry groups have the shape of table/parameter initialization or bulk configuration. They are not yet identified as per-note voice-start commands. The test's current synthetic ready-response fixture also does not reproduce a measured response from the physical hardware.

Therefore:

- Confirmed: MIDI event -> general event dispatcher -> Part-indexed staging routine -> external four-byte command-port writes in the test run.
- Not yet confirmed: which exact packet is the note-on voice trigger, whether the burst is a side effect of program/Part initialization, and the physical peripheral's command semantics.
- Not yet implemented: the original sound processor or sample playback algorithm.

## Next reverse-engineering tasks

1. Add per-instruction call/return tracing around `0x5671`, `0x4256`, `0x1494`, `0x16C3`, `0x189D`, and `0x0DB7`, retaining the exact call nesting and packet timestamps.
2. Run controlled differential tests: Program Change only; Note On only after a stable Performance is loaded; Note Off only; velocity changes; repeated same note; and each of the four active Performance Parts.
3. Compare packet deltas and payload bytes between those runs. A packet that appears only on Note On and varies with note/velocity/Part is a stronger voice-command candidate.
4. Trace the source of the 256-entry sweeps and 16-entry groups to determine whether they configure a wavetable/sample bank, a voice table, or another peripheral subsystem.
5. Only after command fields are correlated with known-note hardware captures, implement a named command model and sound playback backend.

## Reproducibility

The assertions and trace output are in `tests/test_technox_8032_machine.cpp`, in the section beginning with the `programAndNote` stimulus. The test records packet bytes, instruction PC, caller return PC, and machine-cycle timestamp for each command-port packet.

## Additional source-level qualification

Inspection of the regression assertions narrows the interpretation further:

- The stimulus is a single five-byte sequence, `C0 05 90 3C 64`; Program Change and Note On are not isolated. The observed 553-packet delta therefore cannot be assigned to Note On specifically.
- The test asserts calls to `0x57A8`, `0x5671`, and `0x4256`, plus at least one call to the four-byte writer `0x0DB7`. It does **not** assert that `0x1494` is entered by this MIDI stimulus, and explicitly notes that the IC51 helper `0x1494` is not deterministically entered.
- The test comments identify the 32-entry section as a loop through `0x0DF7`, calling `0x0E1A`; its two groups of 16 are separated by a control-port write from `0x0DE4`. This is stronger call-site evidence than merely grouping packets by return PC, but still does not reveal what the external device does with them.
- The fixture asserts packet count and selected packet boundaries/shapes, not the complete semantic mapping of all 553 packets and not a voice-start/voice-stop event. Its own comment says full MIDI-to-voice dispatch is not asserted until the SoundHost path is modeled.

The immediate experiment must therefore use four separately booted, identically initialized machines (or a deterministic snapshot facility): (a) Program Change only, (b) Note On only after the same program is already selected, (c) Note Off only after a note-on baseline, and (d) velocity/note substitutions. Compare full packet streams, not only counts. Keep the synthetic ready response labelled as a test fixture in every result.


## Latest differential probe: a second Note On after setup

A follow-up probe now sends a second, distinct event (`90 40 64`, note 64, velocity 100) after the first Note On has passed the test-only ready/queue-drain handshake. The existing synthetic read override remains active; this second stage does not inject a new status value.

Observed second-stage deltas:

- 549 complete four-byte SoundHost transactions and 549 burst words.
- 2 additional calls each to firmware routines `0x4256`, `0x5671`, `0x1494`, `0xAC00`, and `0x189D`.
- 549 calls to the packet writer `0x0DB7`.
- 14,522 additional IC51 reads.
- The stage completes at PC `0x32DB`; no CPU fault is reported by the regression.

The first five packets of this second stage are:

| Order | Packet bytes (`R5 R2 R3 R4`) | Recorded return PC | Writer call |
|---:|---|---|---|
| 1 | `00 A9 E4 01` | `0x0A52` | `0x0A4F` |
| 2 | `02 01 00 00` | `0x0A52` | `0x0A4F` |
| 3 | `05 FB D0 03` | `0x22FB` | `0x22F8` |
| 4 | `0A 00 78 00` | `0x2122` | `0x211F` |
| 5 | `0F 00 00 00` | `0x236E` | `0x236B` |

The firmware helper at `0x0DB7` writes these four bytes in the order R5, R2, R3, R4 to offsets `0x8000–0x8003`. This byte order and the five observed packet values are directly traceable. The meanings of the three payload bytes and the first-byte selector are still unknown.

The remaining 544 packets form a repeated configuration sweep: 512 transactions return through `0x0E6A`, followed by 32 transactions returning through `0x0E1D`. This is a firmware-observed reconfiguration burst after the five initial note-associated transactions. It is not proof that all 544 writes are voice data or that the five preceding packets alone start audible playback.

The regression now asserts the 549-packet delta and the exact five leading packet byte arrays and return PCs. This turns the observation into a repeatable guard against accidental changes in the emulated firmware bus path. It does not validate an external chip response, sample decoder, or audible output.


## Latest differential probe: velocity-only change

The same runtime then receives a second event with the same Part, selected program and pitch (note 64), but velocity is reduced from 100 to 50:

```
Reference:   90 40 64
Comparison:  90 40 32
```

Both events produce 549 SoundHost transactions and 549 burst words. Comparing the first five packets gives:

| Order | Velocity 100 | Velocity 50 | Changed bytes | Return PC |
|---:|---|---|---|---|
| 1 | `00 A9 E4 01` | `00 A9 E4 01` | none | `0x0A52` |
| 2 | `02 01 00 00` | `02 01 00 00` | none | `0x0A52` |
| 3 | `05 FB D0 03` | `05 FB D0 03` | none | `0x22FB` |
| 4 | `0A 00 78 00` | `0A 00 98 02` | bytes 2 and 3 | `0x2122` |
| 5 | `0F 00 00 00` | `0F 00 00 00` | none | `0x236E` |

This is a controlled firmware-level correlation: with Part, program and note held constant, changing only MIDI velocity changes the third and fourth bytes of the packet emitted from the `0x2122` call site. It identifies a promising velocity-sensitive command field, but the hardware meaning, scaling, and whether it directly controls amplitude remain unverified.

The probe is asserted/reported by the regression test in `tests/test_technox_8032_machine.cpp`. As before, the DSP-ready response is synthetic; this proves how IC4 constructs the packet, not how the external sound processor reacts to it.

Static disassembly around the caller return PC confirms the packet's register assembly:

```asm
210F  MOV R2, #0x00
2111  MOV R5, 0x2B
2113  MOV A, R4
2114  ANL A, #0x07
211B  MOV R4, A
211C  MOV P2, #0x80
211F  LCALL 0x0DB7
2122  MOV P2, 0x2D
```

Thus this call site emits `[XDATA/RAM[0x2B], 0x00, R3, (R4 & 0x07)]`; `R3` is inherited from the preceding firmware calculation. For the velocity-only probe, the observed packet is `[0x0A, 0x00, 0x78, 0x00]` at velocity 100 and `[0x0A, 0x00, 0x98, 0x02]` at velocity 50. The next reverse-engineering target is now the producer of `R3` and `R4` before address `0x210F`, not the generic packet writer. This still does not establish whether those fields represent amplitude, envelope, a sample address, or another voice parameter.



## Fixed-point return values confirmed for the velocity differential

The regression now compares the live IC4 return state at both callers with the instruction-level `MASS1848Arithmetic` model. This confirms the arithmetic result at the two call boundaries, not merely the input snapshots:

| Caller return PC | Velocity 100: input R2 -> A | Velocity 50: input R2 -> A | Live/model agreement |
|---|---|---|---|
| `0x1FC9` | `0x11 -> 0x10` | `0x63 -> 0x59` | exact for both |
| `0x2027` | `0x0F -> 0x0F` | `0x53 -> 0x53` | exact for both |

At `0x1FC9`, B also changes from `0xEA` (velocity 100) to `0xE2` (velocity 50), while the carry/overflow flags match the C++ arithmetic model. At `0x2027`, B is `0x00` in both runs. These are register-level arithmetic facts; the semantic units of the values remain unknown.

The second helper result is therefore not itself the velocity-sensitive packet byte: its output A is `0x0F` / `0x53`, while the later packet builder at `0x210F` sees R3/R4 = `0x78/0x78` or `0x98/0x9A`. The remaining task is to capture the instruction stream and register state between each helper return and the next consumer, including writes to R3, R4, RAM[0x2B], and any indirect/XDATA operands. Do not infer a direct A-to-packet mapping.

### Instrumentation implementation: bounded 8032 execution trace

The next trace slice is now implemented in `Source/TechnoX8032Machine.h` and exercised by the same-note velocity differential in `tests/test_technox_8032_machine.cpp`.

- Capture is explicitly armed by the test and starts when the CPU reaches either `0x1FC9` or `0x2027`; it records every instruction up to and including the pre-execution snapshot at `0x210F`.
- Each instruction record includes the three code bytes, PC, stack return PC, machine cycle, A/B/PSW, active register-bank base and R0–R7, DPTR, SP, P2/P3, sound-bus mode, direct RAM `0x2B`, and the consecutive RAM bytes `0x30–0x37`.
- A separate bounded event stream records each MOVX/XDATA read and write during the same active window: instruction PC, stack return PC, address, value, direction, machine cycle, P2/P3, bus mode and IC51-select state.
- The test captures the same note/Part/program at velocity 100 and 50, then reports the first instruction-path divergence, first CPU-state divergence, and first XDATA access divergence. It deliberately does not assign semantic names to the changed values.

This is instrumentation and a regression comparison, not yet a decoded trace result. The newly added test has not yet been confirmed by a completed CI run in this checkpoint. Once it runs, the first divergent state/write can be inspected to identify the firmware transformation between the arithmetic helpers and the packet builder. The test-only synthetic ready response remains in use; no external processor behavior or audio output is inferred.

### Pitch descriptor pointer source: XDATA indirection confirmed

The pitch-source probe now starts at the descriptor-pointer load at IC4 PC `0x1607` and follows execution through the `0x1C61` interpolation/parameter path to `0x1E67`.

The failed assertion in the first run exposed a mistaken interpretation: register `R7=0x0C` at PC `0x1607` is not the low byte returned by the pointer read. The instruction bytes at `0x1607` and `0x160B` are both `MOVX A,@DPTR`. The captured execution states show:
- At PC `0x1607`, DPTR is `0x54E5`; after the MOVX, the following instruction at `0x1608` sees A=`0x00`.
- PC `0x160A` increments DPTR to `0x54E6`; after the second MOVX at `0x160B`, the following instruction at `0x160C` sees A=`0x16`.
- The resulting little-endian pointer is therefore `0x1600`. The seven-byte record subsequently read by the pitch path is at `0x160C–0x1612`; the test already checks those seven IC51 read bytes and their matching firmware read PCs.

The test was corrected to assert the actual pointer bytes `00 16`, the two adjacent XDATA addresses `54E5/54E6`, and the combined pointer `0x1600`. It no longer confuses the input selector in R7 with the value loaded by MOVX. This correction is based on the captured 8032 instruction stream; the corrected PR CI run is still required before treating the new assertion set as green.

### Remaining reverse-engineering work


1. Add a bounded instruction trace for the two dynamic return addresses captured at `0x1848`, starting at each return PC (`0x1FC9` and `0x2027`) and ending at the next relevant consumer / `0x210F` packet builder.
2. For each instruction record PC, opcode bytes, A/B/PSW, active R0-R7 bank, DPTR, P2/P3, current stack return PC, and the values of RAM[0x2B], RAM[0x30..0x37]. Record XDATA reads/writes in the same cycle window.
3. Run the same-note velocity 100/50 pair and compare the two traces instruction-by-instruction. The first divergent input or write should identify where the velocity-dependent path is introduced or transformed.
4. Repeat with a second pitch and a second Part before assigning a field name. Then classify the first five Note On commands separately from the 544-command configuration sweep.

The current fixture still uses a synthetic ready response. These traces establish firmware behavior only; they do not identify the external processor or prove audible sample playback.
5. Obtain and inspect the CI execution trace for the two velocity runs; use the first divergent PC/state/XDATA operand to trace the producer of R3/R4.
6. Repeat with another pitch and a second Part, then classify all five Note On-associated commands independently from the 544-command configuration sweep.


## Confirmed pitch-handler output: 1C61 path through 09B9

A dedicated Note On probe now captures two complete 100-instruction windows from the caller at 0x1E67 through the shared packet writer at 0x0A4F. The two windows each contain ten MOVX/XDATA accesses. The active call chain is:

```
0x1E67  LCALL 0x09B9
0x09B9  compare/update current slot bytes at base + 0x0E/+0x0F
0x09DE  read the three coefficient bytes at base + 0x01..+0x03
0x0A31  add the three-byte slot vector back into R4:R3:R2
0x0A42  mask the high result byte to three bits
0x0A4A  R5 = direct RAM[0x2B]
0x0A4F  LCALL 0x0DB7
```

For the Note 65 / velocity 100 probe, the two captured active records were:

| Window | Slot base | Compared / stored pair | Three bytes read | Packet emitted |
|---:|---:|---:|---:|---|
| 1 | `0x80` | `00 00 -> 06 00` at `008E/008F` | `57 01 A2` from `0081..0083` | `00 71 01 02` |
| 2 | `0x90` | `00 00 -> 06 00` at `009E/009F` | `01 00 48` from `0091..0093` | `02 01 00 00` |

The packet arrays are now asserted exactly in `tests/test_technox_8032_machine.cpp`, as well as being checked against the live R5/R2/R3/R4 snapshots before the call to `0x0DB7`. In the first window, the intermediate 24-bit register result before the final vector addition is `00:00:1A`; after adding the three bytes and applying the firmware's three-bit high-byte mask, the emitted payload registers are `R2:R3:R4 = 71:01:02`. In the second window they become `01:00:00`. This describes the observed arithmetic and register order only; the physical units or external-device meaning of these values are not known.

### Complete Performance 1 / Part 13 resource stream

The actual IC51 resource lookup for Performance 1 Part 13 (bank 2 / sound 10) resolves to pointer bank 1, pointer index `0x0A`, descriptor `0x464D`, command stream `0x0153`. The descriptor has no Table A groups (terminator `0x75`) and no Table B entries (terminator `0x38`). Its stream consumes 58 bytes as 16 slots; it reaches the 16-slot limit without an explicit `0xFF` terminator or terminal control command.

The entire command sequence is now pinned byte-for-byte by `tests/test_mass_resource_rom.cpp`:

| Slot | IC51 offset | Kind | Bytes |
|---:|---:|---|---|
| 0 | `0153` | single-byte advance | `76` |
| 1 | `0154` | single-byte advance | `6F` |
| 2 | `0155` | conditional record | `C1 72 8A 60` |
| 3 | `0159` | multi-parameter | `D6 63 94 02 66 09 24` |
| 4 | `0160` | multi-parameter | `52 A2 55 EC 66 BE 69` |
| 5 | `0167` | marker | `7C` |
| 6 | `0168` | multi-parameter | `57 C2 5A 80 6B D2 6E` |
| 7 | `016F` | four-byte command | `9C 5D F0 5F` |
| 8 | `0173` | single-byte advance | `AE` |
| 9 | `0174` | single-byte advance | `70` |
| 10 | `0175` | conditional record | `C5 73 8E 61` |
| 11 | `0179` | pitch parameter | `0E 64 CC 05 9E 0C 5C` |
| 12 | `0180` | multi-parameter | `54 02 52 54 68 92 66` |
| 13 | `0187` | single-byte advance | `70` |
| 14 | `0188` | four-byte command | `5A 96 57 5E` |
| 15 | `018C` | single-byte advance | `6D` |

The stream contains one group-08 pitch-parameter record at offset `0x0179`, which is consistent with the live `0x1C61 -> 0x1E67 -> 0x09B9` pitch-handler path. The current trace starts at `0x1E67`, however; it does not yet prove that this particular ROM record supplied the two captured slot bases and coefficient triplets. That byte-to-register provenance is the next trace slice: capture the executing `0x1C61` record load through its `0x1E67` caller and compare all seven source bytes to the live R2/R3/R4/R5/R6/R7 inputs.

The unit-test job for the full instruction dump passed in CI run `45257503996`; the new full-stream asserts and exact pitch packet asserts are being tested in the subsequent run. The fixture still injects a synthetic DSP-ready state. No external sound processor response, PCM decode, or audible output is established by these traces.


## End-to-end 1C61 pitch-record reconstruction (live trace + ROM model)

The next trace starts at the group-08 handler entry `0x1C61` and ends at `0x1E67`. It captures 226 instructions and 23 XDATA accesses while processing the live Note 66 / velocity 100 probe. This bridges the previously isolated arithmetic tail to its actual IC51 input.

### IC51 input and Part-slot staging

With the IC51 bank selected (`P3=EC`), IC4 reads seven consecutive bytes:

| IC51 address | Byte | Destination in the handler's register/field sequence |
|---:|---:|---|
| `160C` | `08` | `F0` / group-08 opcode |
| `160D` | `20` | `R2` |
| `160E` | `18` | `R3` |
| `160F` | `A0` | `R4` |
| `1610` | `F6` | `R7` |
| `1611` | `00` | `R6` |
| `1612` | `00` | `R5` |

The handler writes the record into the current Part's XDATA slot. It stages `08` at `0080`, `A0` at `0083`, and the remaining record bytes into `0084–0086`. The live slot is based at `0x80`; the subsequent arithmetic path uses the neighbouring `0x90` slot on the next invocation.

### Secondary-table lookup, now reproduced from the actual IC4 ROM

The live register/IRAM state entering the secondary-table lookup is:

- base pointer assembled from `IRAM[0x9F:0x9E]` = `0x3903`;
- `R3=0x18`, `RAM[0x2F]=0x7B`, `R4=0xA0`;
- the firmware's signed wrap loop converts `0x18 + 0x7B = 0x93` to the 7-bit index `0x7B`;
- `R4.4` is clear, so no `0x180` secondary-table offset is added;
- adding the original `R7=0x7B` gives the MOVC base `0x397E`.

The actual descrambled IC4 ROM bytes at `0x397E`, `0x39FE`, and `0x3A7E` are `05 43 02`. The packed-field decoder produces the live values observed at `0x1DC8`:

| Register | Value |
|---|---:|
| `R2` | `80` |
| `R3` | `60` |
| `R4` | `48` |
| `R5` | `05` |
| `R6` | `43` |
| `R7` | `02` |

The C++ regression now resolves this same address with `MASS1C61SecondaryTableAddressBuilder`, reads all three bytes with `MASS1C61SecondaryTableReader`, and decodes them with `MASS1C61PackedLookupDecoder` against the real firmware image. The instruction-level model `MASS1C61AccumulatorTransform` is then run with those exact live registers and the captured direct-RAM operands. It reproduces the first arithmetic result `R2:R3:R4 = D5:1E:02`, which is what the CPU trace reaches at the next stage boundary (`0x1E05`).

### Final correction and slot writeback

The firmware then performs a distinct correction step from `0x1E05` through `0x1E2B`. In this probe, the correction changes the middle byte from `0x1E` to `0x1F`; it is not part of the preceding secondary-table multiply/accumulate result. IC4 clears the slot pair at `008E/008F`, consults the slot state at `2019/2018`, and writes back the final three-byte vector:

| Instruction | XDATA write | Result |
|---:|---|---:|
| `1E60` | `0083` | `A2` |
| `1E63` | `0082` | `1F` |
| `1E66` | `0081` | `D5` |

The `0x1E67` entry snapshot is therefore `R2=D5, R3=1F, R4=06, R5=00, R6=43, R7=02`. The following `0x09B9` handler consumes the staged record and produces the two initial packets through `0x0A4F`.

The regression now asserts the seven IC51 input bytes and addresses, the decoded register tuple at `0x1DC8`, the arithmetic tuple at `0x1E05`, the final tuple at `0x1E67`, and the exact XDATA writeback bytes `D5 1F A2`. The resource-model test independently verifies the real IC4 ROM table lookup and first accumulator result. This closes the byte-to-table-to-arithmetic-to-slot-writeback path for this tested event; it does not yet assign a musical unit or physical sound-engine meaning to the resulting three-byte vector.

**Packet-byte correction:** the second pitch-handler packet observed for the Note 65 probe is `02 01 00 00` (not `02 00 00 00`). The regression assertion and the earlier CI writer trace both use `02 01 00 00`.

The current CI checks are running for the new live-trace assertions and the real-ROM model test. The DSP-ready response in the broader Note On fixture is still synthetic, so these results confirm IC4's computation and bus writes, not an external DSP response or audible playback.
