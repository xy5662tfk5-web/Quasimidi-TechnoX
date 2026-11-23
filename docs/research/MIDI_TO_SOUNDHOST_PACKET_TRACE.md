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
