# Slot record accumulator and fixed-point SoundHost path

Status: reproduced on the descrambled IC4 v1.05 ROM by the Intel CI unit-test job for commit `2a3ce0a3344605039e40b75820471a663d250217`. The test runs the real firmware in the 8032 machine model, but the active record is deliberately seeded synthetic state; this is not a physical-device capture.

## 1. Matched Slot 0 / Slot 1 differential

The Slot 0 and Slot 1 consumer probes now use matching coefficient bytes:

| Operand role (neutral) | Slot 0 address | Slot 1 address | Value |
|---|---:|---:|---:|
| coefficient A | `0x28C8` | `0x28D9` | `0x22` |
| coefficient B | `0x2888` | `0x2889` | `0x33` |
| coefficient C | `0x2898` | `0x2899` | `0x44` |
| slot-indexed input | `0x2020` | `0x2021` | `0x05` |
| two-byte slot state | `0x2000–0x2001` | `0x2002–0x2003` | `0x0000` |

Slot 0 selects the `0x28C8` page with `0x00FB=0x00`, `0x00FC=0x00`; Slot 1 selects the alternate page with `0x00FB=0x08`, `0x00FC=0x10`. The firmware snapshots are asserted to load the matched `0x22/0x33/0x44` operands in both cases.

For both slots, one sine step produces scratch bytes `RAM[0x36/0x37]` equal to the other slot's one-step result; the same cross-slot equality holds for two steps. Both slot paths reach the same record consumer and emit the same packets:

| Sine steps | Record `+0x96/+0x97` | SoundHost packet |
|---:|---:|---|
| 1 | `14 00` | `01 03 05 00` |
| 2 | `1F 00` | `01 04 05 00` |

This shows that the two slot-indexed paths can produce identical arithmetic when their test inputs are matched. It does not prove that factory slot coefficients are equal or assign a musical meaning to the values.

## 2. Exact record field access

For the test record page `0x00`, the firmware uses internal cursor `RAM[0x2E]=0x88`. Therefore the two-byte field at cursor offset `+0x0E` maps to XDATA `0x0096–0x0097`.

The successful CI trace records this sequence for both slots and both sine-step values:

| XDATA | Operation | IC4 PC | Stack return PC | Meaning established by disassembly |
|---:|---|---:|---:|---|
| `0x0096` | read | `0x09BF` | `0x09A7` | low byte of stored 16-bit value |
| `0x0097` | read | `0x09C3` | `0x09A7` | high byte of stored 16-bit value |
| `0x0097` | write `0x00` | `0x09CA` | `0x09A7` | high byte update |
| `0x0096` | write `0x14` or `0x1F` | `0x09CD` | `0x09A7` | low byte update |

The trace reports exactly one read and one write for each of `+0x96` and `+0x97` in each isolated invocation. The old value is zero because the fixture seeds both bytes to zero.

## 3. Caller and update logic recovered from ROM

The reachable disassembly establishes the following flow:

1. At `0x08A2–0x08A4`, firmware copies internal scratch `RAM[0x36]` into `R4` and `RAM[0x37]` into `R5`.
2. `0x08A6` jumps to `0x09A4`; `0x09A4` calls `0x09B9`. The stacked return address during the record accesses is consequently `0x09A7`.
3. `0x09B9–0x09C5` constructs the XDATA pointer from `RAM[0x2E]+0x0E), reads the low byte, subtracts `R4), reads the high byte, subtracts `R5) with borrow, and combines the differences.
4. If both bytes already match, `0x09C8` returns without rewriting the field. Otherwise `0x09C9–0x09CD` stores `R5` to the high byte first and `R4` to the low byte second.
5. The bytes `R4/R5` then feed the following fixed-point arithmetic. The routine reads the three record bytes at cursor offsets `+1,+2,+3` (observed XDATA `0x0089, 0x008A, 0x008B`) at PCs `0x09DF`, `0x09E6`, `0x0A00`, then reads them again at `0x0A35`, `0x0A39`, `0x0A3D` while combining the intermediate.
6. At `0x0A4A–0x0A4F`, the firmware stages `RAM[0x2B]`, selects P2=`0x80`, and calls the real four-register writer at `0x0DB7). It restores P2 from `RAM[0x2D]` at `0x0A52`.

The disassembly makes the two-byte field structurally clear: `+0x0E` is the low byte, `+0x0F` is the high byte, and the routine compares the stored pair with the new `R4:R5` value before updating it. This is a cached/previous 16-bit state value used by the fixed-point output path. Its user-facing parameter name, scale, and physical DSP meaning remain unidentified.

## 4. What the trace does and does not prove

Established:
- the incoming two-byte value is sourced from internal scratch bytes `0x36/0x37`;
- the `+0x0E/+0x0F` record pair is compared as a 16-bit value and conditionally updated;
- the following calculation reads three adjacent record bytes twice and then reaches the four-byte SoundHost writer;
- matched Slot 0 / Slot 1 test inputs produce equal intermediate results and equal packet bytes;
- each isolated test emits four writes at `0x0DBA/0x0DBD/0x0DC0/0x0DC5`, returning to `0x0A52).

Not established:
- the musical identity of the 16-bit cached value or the three adjacent record bytes;
- the physical meanings of the four SoundHost bytes;
- the sound processor's identity or ready/busy/ack protocol;
- whether the seeded active record exactly matches a factory-loaded or hardware-populated voice.

Do not label this record pair as pitch, volume, envelope, sample offset, or LFO amount without a separate controlled mapping. Keep the generic oscillator backend labelled as a test backend.

## 5. Regression and source anchors

- Test: `tests/test_technox_8032_machine.cpp`, Slot 0 `runSineSlotConsumer` and Slot 1 `runAlternateSlotOne`.
- Firmware: `0x08A2–0x08A6`, `0x09A4`, `0x09B9–0x09CD`, `0x09DA–0x0A55`.
- Disassembly artifact was generated by successful unit-test job in workflow run [#1468](https://github.com/xy5662tfk5-web/Quasimidi-TechnoX/actions/runs/45435677117), commit `2a3ce0a3344605039e40b75820471a663d250217`.
