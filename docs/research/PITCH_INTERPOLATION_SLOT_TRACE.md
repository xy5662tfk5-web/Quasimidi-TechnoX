# Note-pitch interpolation slot trace (IC4 v1.05)

Status: reproducible real-ROM 8032 trace, exercised in the Intel macOS CI regression. Test stimulus uses the already-loaded first factory Performance/program 5, Part 1 path, Note On pitch 65 and velocity 100. The same firmware stage emits 549 four-byte SoundHost transactions; the hardware response remains synthetic.

## Observed calls and source operands

The two visits to the 0x09B9 interpolation handler both have caller return PC 0x1E6A. The bounded trace records two calls to the arithmetic body at 0x09DA, two calls to the packet writer at 0x0A4F, and twenty MOVX accesses in the watched 0x09B9..0x0A3D window.

| Pass | MOVX byte reads | Raw bytes | Effective 24-bit input | Step | Output 24-bit |
|---:|---|---|---:|---:|---:|
| 1 | 0x0081, 0x0082, 0x0083 | 57 01 A2 | 0x020157 | +0x000D | 0x020171 |
| 2 | 0x0091, 0x0092, 0x0093 | 01 00 48 | 0x000001 | +0x000D | 0x000001 |

The effective input masks the top source byte to three bits. The reconstructed firmware arithmetic computes:

increment = (effectiveInput24 * stepMagnitude) >> 16

and then adds or subtracts that increment according to the firmware sign branch, modulo the 24-bit output width. For pass 1 the increment is 0x1A, producing 0x020171; for pass 2 the increment is zero, leaving 0x000001.

The resulting low/middle/high bytes at the two 0x0A4F writer snapshots are respectively 71 01 02 and 01 00 00. The test now asserts each source address, source byte, shared caller, step, increment, result and writer register.

## What the evidence says

- The same-note-processing path consults two distinct 3-byte XDATA slots, 16 bytes apart, within one 0x1E6A caller context.
- These are XDATA operands used by the firmware's interpolation arithmetic. They are not the 24-byte Performance Part record addresses 0x7D35–0x7D94, and they are not accesses to the external command window 0x8000–0x8003.
- The code path is part of the previously traced pitch-handler route through 0x09B9. The exact semantic units of the 24-bit values and their physical destination are not yet proven; do not label them as sample addresses, pitch words, or DSP fields without additional differential evidence.
- The separate velocity-sensitive 0x1848 -> 0x2089..0x208E -> 0x210F chain remains a different traced route; the two investigations should not be merged merely because both feed the same broad Note On processing flow.

## Resource-handler producer: note 72 byte-level trace

The follow-up Note 72 run now captures the resource-handler writes that populate the two interpolation triplets before the 0x09B9 arithmetic. The call returns to 0x1720, within the selected IC4 resource-command processing path. The captured data accesses and instruction snapshots establish the following sequence for this fixture:

1. At 0x1E4B, `MOVX A,@DPTR` reads XDATA 0x2019. At 0x1E4C, opcode `CC` exchanges A with R4; at 0x1E4D, `MOV B,A` puts the caller's prior R4 value into B. The trace shows B=0x01 for the first lane and B=0x00 for the second.
2. The routine decrements DPL at 0x1E4F and reads XDATA 0x2018 at 0x1E51. It copies that byte to R5; at 0x1E53, `ORL A,R4` combines the 0x2018 byte in A with the 0x2019 byte now in R4. The following `JZ` at 0x1E54 takes the alternate path only when both bytes are zero. In the Note 72 fixture the pair is `00 06`, so the OR result is `0x06` and the slot writer is reached.
3. At 0x1E5B, `MOVX A,@R1` reads the existing high byte of the selected triplet. At 0x1E5C, `ANL A,#0xF8` clears its low three bits; at 0x1E5E, `ORL A,B` reinserts the saved low-bit field. The result is written at 0x1E60.
4. The following instructions write R3 at 0x1E63 and R2 at 0x1E66 while decrementing R1 after each write. Thus the firmware writes high, middle, low bytes; the later 0x09B9 consumer reads low, middle, high.

The 0x2018/0x2019 pair is a **zero/nonzero guard in this path**, not a 16-bit number added to the pitch triplet. The byte at 0x2019 is exchanged into R4, while the previous R4 value is saved through A into B. The byte at 0x2018 is then ORed with the 0x2019 byte solely for the zero test. On the nonzero path, B supplies the low three bits merged into the existing high byte; the numerical value 0x06 itself is not copied into the triplet.

The disassembly also identifies a generic writer for this pair. IC4 routine 0x02D1 performs its arithmetic using one of two four-byte code tables at 0x0098/0x00C8 and funnels the two-byte result through 0x0392; 0x039E and 0x03A2 are the final MOVX writes to the indexed 0x2000 XDATA region. In the Note 72 trace, writes to 0x2018/0x2019 from those PCs occur later in the drain (cycles 107,980,804 onward), after the resource handler's reads at cycles 105,107,166/171. This pins a writer path but does not yet prove which earlier operation supplied the `00 06` pair consumed by this Note 72 event.

The real-ROM trace asserts these six writes for the Note 72 fixture:

| Slot | High byte (0x1E60) | Middle byte (0x1E63) | Low byte (0x1E66) | 24-bit bytes, low-to-high |
|---|---:|---:|---:|---|
| A, XDATA 0x0081–0x0083 | 0xA1 | 0x80 | 0xBE | `BE 80 A1` |
| B, XDATA 0x0091–0x0093 | 0x48 | 0x00 | 0x01 | `01 00 48` |

The same run then reads those exact triplets in the two 0x09B9 interpolation passes. With step +0x000D, the results are `0x0180D1` and `0x000001`, respectively. The high byte is masked to its low three bits by the interpolation arithmetic; no semantic unit or sample-address interpretation is assigned to these 24-bit values.

The note-72 packet prefix also provides a useful separation check. Its first interpolation packet is `00 D1 80 01`, while the velocity-sensitive packet emitted at return PC 0x2122 remains `0A 00 78 00`, identical to the Note 65 / velocity 100 reference. The second run changes pitch while holding velocity at 100; this is a controlled cross-pitch comparison, not proof of the external receiver's byte semantics. The test fixture still uses synthetic ready/status inputs.

## Next differential probe

Capture the same two slot addresses for note 64 and note 65 under identical Part/program/velocity. Then vary only the active Performance Part / event ID and verify whether the selected slot pair changes. This will show whether the 16-byte slot stride corresponds to event/voice records, a pair of interpolation lanes, or another firmware structure. Continue to keep the synthetic ready response labelled as a test fixture.

## Layer-mode differential: both pitch lanes remain active in Single

The same IC4 Note 65 stimulus was repeated with the Common selector read-override changed from Layer 2 (`0x7BD5=1`) to Single (`0x7BD5=0`). The MIDI dispatch/resource path collapsed from two Part events to one (`0x4256=1`, `0x1494=1`, `0x189D=1`, `0xAC00=1`; sole event ID `0x0C`).

Despite that reduction, the interpolation trace still records two entries at `0x09B9`, two arithmetic bodies at `0x09DA`, two packet-writer entries at `0x0A4F`, and 20 watched MOVX events. It reads the same two source triplets:

- `0x0081–0x0083`: `57 01 A2`;
- `0x0091–0x0093`: `01 00 48`.

This is decisive negative evidence against assigning one interpolation triplet to each enabled Performance Part. The pitch slots are independent from the number of candidate Part events selected by the Common layer mode. They may represent two interpolation lanes or another internal pair of parameter states, but their exact ownership/semantics remain unidentified. The physical sound device and sample format are still unknown.


## 0x2018/0x2019 guard and exact slot writeback model

The later Note 72 instruction trace narrows the two-byte state pair's role in the pitch handler:

1. At 0x1E4B, the firmware reads XDATA 0x2019.
2. At 0x1E4C, XCH A,R4 exchanges that byte with the incoming R4 value; at 0x1E4D, MOV B,A preserves the incoming R4 in B.
3. At 0x1E51, it reads XDATA 0x2018. The OR at 0x1E53 combines the two state bytes only to decide whether the following JZ at 0x1E54 skips the slot update.
4. On the nonzero route, the existing high slot byte is read, masked with 0xF8, and ORed with B. The handler then writes R3 and R2 as the middle and low bytes. Thus the saved incoming R4 contributes only its low three bits to the existing high byte.

The repository now models this as `MASSPitchSlotGuardProjection` and tests both zero-guard and nonzero-write cases. This is a byte/register-level model of the observed handler, not a semantic naming of the state pair.

### Provenance boundary for the state pair

A separate static/cycle trace identifies the generic writer chain: 0x02D1 performs table-driven arithmetic using code tables at 0x0098 and 0x00C8, routes the two-byte result through 0x0392, and reaches the final XDATA stores at 0x039E and 0x03A2 in the indexed 0x2000 region. In the Note 72 fixture these writes occur after the handler's reads of 0x2019/0x2018. The writer is therefore identified, but the upstream caller and the initial source operands that seed this calculation are not yet proven. Do not claim that the 0x02D1 table inputs or outputs are pitch units until that producer chain is captured.

