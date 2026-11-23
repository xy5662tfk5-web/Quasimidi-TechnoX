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
