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


## First Note On after synthetic ready/queue-drain setup

The first Note On stage starts from the IC4 test fixture's initial startup state and uses an explicitly injected ready/queue-drain handshake. Its trace contains **546** complete four-byte transactions:

- 544 repeated configuration transactions (512 via `0x0E6A`, 32 via `0x0E1D`);
- two additional four-byte writes whose caller return PC is `0x1048`.

The two startup packets are now asserted byte-for-byte by the regression:

| Order | Packet bytes (`R5 R2 R3 R4`) | Recorded return PC | Writer |
|---:|---|---|---|
| 1 | `F0 80 01 80` | `0x1048` | `0x0DB7` |
| 2 | `F1 80 01 80` | `0x1048` | `0x0DB7` |

The follow-up warm-state Note On below emits a different five-packet prefix. Keep these two startup packets separate from those five note-associated packets: they occur only in the first synthetic-ready stage of this fixture. The test does not identify the external device or the meaning of `F0/F1`, and this startup sequence is not evidence of real hardware acknowledgement.

## Paired Note Off after the ready-state Note On

After the synthetic-ready Part 1 setup and two-layer Note 65 dispatch, the regression sends `80 41 40` (Note Off, key 65, release velocity 64). The firmware:

- enters the general event path and dispatches the two active layer IDs;
- calls the Note Off helper `0xAC88` twice and executes the Part handler `0x4256` twice;
- records the expected Note Off branch at `0xAC A0` and page-`0x35` transition at `0xACB2` for both layer event IDs;
- emits **zero** writes to `0x8000–0x8003` and makes no call to `0x0DB7` in this test window.

The regression now asserts the packet-count and packet-writer deltas remain unchanged for this Note Off event. This is a useful separation: the tested Note Off path changes IC4's internal note/event state rather than replaying the four-byte command-window burst. It does **not** prove that the physical sound device receives no release indication through another, still-unidentified interface, nor does it establish the DSP's actual release behavior. The test still uses the explicitly synthetic ready-response fixture.

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

The second helper result is therefore not itself the velocity-sensitive packet byte: its output A is `0x0F` / `0x53`, while the later packet builder at `0x210F` sees R3/R4 = `0x78/0x78` or `0x98/0x9A`. The bounded instruction trace now closes this gap for the exercised branch. At `0x2089` the firmware executes `SWAP A`; at `0x208A` it executes `RR A`; `0x208B` stores that byte in R4; `0x208C` masks A with `0xF8`; and `0x208E` stores the masked value in R3. Thus:
- `A=0x0F` becomes swapped `0xF0`, rotated `0x78`, so `R3=0x78`, `R4=0x78`.
- `A=0x53` becomes swapped `0x35`, rotated `0x9A`, so `R3=0x98`, `R4=0x9A`.

The subsequent `0x211F` writer receives the four-byte form `[RAM[0x2B], 0, R3, R4 & 7]`, yielding the observed packets `0A 00 78 00` and `0A 00 98 02`. The new `MASS1848CommandFieldProjection` helper models only this verified register projection, and the live-machine regression compares its output against the original 8032 snapshots. It does not assign meanings such as amplitude, sample address, or envelope to those bytes.

### Instrumentation implementation: bounded 8032 execution trace

The next trace slice is now implemented in `Source/TechnoX8032Machine.h` and exercised by the same-note velocity differential in `tests/test_technox_8032_machine.cpp`.

- Capture is explicitly armed by the test and starts when the CPU reaches either `0x1FC9` or `0x2027`; it records every instruction up to and including the pre-execution snapshot at `0x210F`.
- Each instruction record includes the three code bytes, PC, stack return PC, machine cycle, A/B/PSW, active register-bank base and R0–R7, DPTR, SP, P2/P3, sound-bus mode, direct RAM `0x2B`, and the consecutive RAM bytes `0x30–0x37`.
- A separate bounded event stream records each MOVX/XDATA read and write during the same active window: instruction PC, stack return PC, address, value, direction, machine cycle, P2/P3, bus mode and IC51-select state.
- The test captures the same note/Part/program at velocity 100 and 50, then reports the first instruction-path divergence, first CPU-state divergence, and first XDATA access divergence. It deliberately does not assign semantic names to the changed values.

The bounded trace has now run successfully in macOS Intel CI (run #1476). It captured 80 instructions from `0x1FC9` through the pre-execution snapshot at `0x210F`, with identical PC/opcode flow for velocity 100 and 50. The first CPU-state difference is the return snapshot at `0x1FC9`: A/B/R2 are `10/EA/11` versus `59/E2/63`. The first MOVX difference is the write at PC `0x2005` to neutral XDATA address `0x00D1`: `0x10` versus `0x59`. The other seven observed MOVX accesses in this slice match between the two runs. The later `0x2027` return and `0x2089..0x208E` projection produce the R3/R4 values documented above. A follow-up regression now asserts the first CPU-state and XDATA divergence explicitly; its CI run is tracked separately. The test fixture's synthetic DSP-ready response is not a hardware capture, and no physical processor response or audible output is inferred.

### Remaining reverse-engineering work

1. Repeat the same-note velocity differential with another pitch, another active Part, and additional velocity points. This tests whether the `0x2089..0x208E` projection is shared across resource/Part contexts.
2. Trace the earlier producers of the `0x1848` inputs and selected resource bytes back through `0x189D`, `0x1494`, and the IC51 descriptor stream. Preserve raw values until their units are proven.
3. Compare isolated Program Change, Note On, Note Off and controller events from identical machine snapshots; classify the five note-associated commands separately from the 544-command configuration sweep.
4. Identify the physical receiver for `0x8000–0x8003` and validate its ready/ack behavior against a real hardware capture.

The current fixture still uses an explicitly synthetic ready-response path for the warm-state differential. These results establish the 8032's command construction, not the external processor's response or audible sample playback.

## Shared caller confirmed for the two 0x0A4F pitch packets

The live 8032 instruction-watch trace now records the outer stack return address at the two executions of the `0x0A4F` writer callsite. Both snapshots report `0x1E6A`. The hardware packet trace independently records `0x0A52` as the return from the immediate `LCALL 0x0DB7` writer routine; these are two different stack levels and must not be conflated.

The regression now asserts that both `0x0A4F` snapshots have the same outer return PC `0x1E6A`. This rejects the earlier unverified theory that the two packets came from separate callers. The evidence instead places both writes under the same `0x1E67` / `0x09B9` pitch-handler call path, with the same static callsite being reached twice during the note processing. The two packet bytes remain individually asserted, and their payload meanings remain unassigned.

