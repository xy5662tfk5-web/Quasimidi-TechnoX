# Sine-derived slot-1 path into the SoundHost writer

Status: reproduced by the real IC4 v1.05 8032 machine test and passing Intel macOS CI run 1464 (commit 1c202e42ca774b901cd14a37c8b3305f6923c09e). This is a firmware execution trace with a seeded synthetic active-record fixture; it is not a capture from the physical sound device.

## 1. Slot-1 selection and coefficient inputs

The test first executes the actual IC4 initialization and periodic sine update, then enters the alternate consumer path with these XDATA inputs:

- `0x00FB = 0x08`: ACC.3 selects the alternate coefficient-table page.
- `0x00FC = 0x10`: high nibble selects slot index 1.
- `0x28D9 = 0x24`, `0x2889 = 0x35`, `0x2899 = 0x46`: nonzero operands for the alternate page.
- `0x2021 = 0x05`: slot-1 multiplier input.
- `0x2002–0x2003 = 0x0000`: seeded slot state.

The firmware runs from `0x044C` through the fixed-point scratch stage to `0x0550`. Assertions verify the selected slot marker in direct RAM (`RAM[0x30]=0x01)), the coefficient bytes copied to `RAM[0x45]`, `RAM[0x41]`, and `RAM[0x05]`, and that this stage itself emits no SoundHost reads or writes.

The selected P2 page is then seeded with one synthetic active record so the outer record scan reaches the real downstream writer. This setup deliberately isolates the consumer path; the seeded record is not claimed to be a factory preset or a hardware-populated voice.

## 2. Sine-step differential and record output

Two runs differ only in the periodic sine step count:

| Sine step count | Firmware-updated sine sample | Record byte at page +0x96 | Record byte at page +0x97 |
|---:|---:|---:|---:|
| 1 | Same one-step sample as the slot-0 probe | `0x14` | `0x00` |
| 2 | Same two-step sample as the slot-0 probe | `0x1F` | `0x00` |

Both runs reach the four-register writer at `0x0DB7`, return to `0x0A52`, execute the writer path for exactly 16 instructions, and finish without a CPU fault. The test asserts the selected page is `0x00`, the record bytes above, four individual SoundHost writes, and one assembled packet.

## 3. Exact four-byte write observed

The two slot-1 packets are:

| Sine step count | Bytes written to 0x8000–0x8003 | Writer return PC | Captured device index |
|---:|---|---:|---:|
| 1 | `01 03 05 00` | `0x0A52` | `0x01` |
| 2 | `01 04 05 00` | `0x0A52` | `0x01` |

The four byte writes are separately asserted:

| Register offset | Value, one step | Value, two steps | Writer PC |
|---:|---:|---:|---:|
| 0 | `0x01` | `0x01` | `0x0DBA` |
| 1 | `0x03` | `0x04` | `0x0DBD` |
| 2 | `0x05` | `0x05` | `0x0DC0` |
| 3 | `0x00` | `0x00` | `0x0DC5` |

Every event has caller return PC `0x0A52`, captured device index `0x01`, and P2 latch `0x80`. The assembled packet order is the firmware helper's R5, R2, R3, R4 order.

## 4. What the differential establishes

- The alternate coefficient-page path selects slot index 1 and reaches the same outer record-list consumer and four-byte writer used by the slot-0 probe.
- With all other test inputs held fixed, changing the sine-step count changes the record field at page offset `+0x96` from `0x14` to `0x1F`.
- The same differential changes only byte 1 of the observed four-byte packet, from `0x03` to `0x04`; bytes 0, 2 and 3 remain fixed.
- The selected slot's arithmetic/record update is therefore causally upstream of a reproducible SoundHost packet change in this controlled firmware fixture.

This does **not** identify the semantic units of record offset `+0x96` or packet byte 1. It does not prove that the byte is pitch, amplitude, envelope, sample address, voice number, or any other named DSP parameter. The captured `deviceIndex` is machine-model context derived from the bus state; it is not an independently identified physical chip address. The host response remains synthetic/unmodeled.

## 5. Next differential work

1. Keep the active-record seed identical and vary only the selected slot (slot 0 versus slot 1), then compare every source operand, intermediate, record write, and packet byte.
2. Trace all reads and writes to the selected record's `+0x88–+0x97` region to find the producer and consumer of `+0x96/+0x97`; retain P2, PC, return PC and cycle for each access.
3. Change one coefficient input at a time (`0x2020+slot`, the `0x28xx` operands, and the slot-indexed `0x20xx` state) and establish which value controls the observed packet byte.
4. Repeat with a second MIDI note and an independently initialized active Part before attempting to name the field.
5. Keep the synthetic active-record fixture and synthetic ready/status inputs explicitly labelled. Do not implement a named external DSP command until the same mapping is supported by a real-hardware capture or authoritative device documentation.

## 6. Regression anchor

The slot-1 test is in `tests/test_technox_8032_machine.cpp`, in the `runAlternateSlotOne` lambda following the slot-0 sine consumer. The passing CI run is [macOS Intel build #1464](https://github.com/xy5662tfk5-web/Quasimidi-TechnoX/actions/runs/45429015580), commit `1c202e42ca774b901cd14a37c8b3305f6923c09e`. Both build/package and unit-test jobs completed successfully.
