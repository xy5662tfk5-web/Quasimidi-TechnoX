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



## Descriptor branch and 0x0DB7 external-window handoff

The descriptor-tail route is now represented as two distinct packet-building cases:

- At the 0x2092 decision, bit 2 of the current record byte selects the route. Clear takes the short 0x210F path; set continues through the ordinary record-derived sequence.
- The ordinary path emits the two fixed packets `[RAM[0x2B], R5, R6, R7]` and `[RAM[0x2B]-1, R2, R3, R4]`. When internal bit 0x0A is set, it first emits the record-tail packet sourced from offsets `RAM[0x2E]-0x0F`, `-0x0E`, and `-0x0D`.
- The short 0x210F path sets R2 to zero, uses RAM[0x2B] as R5, preserves R3, masks R4 to its low three bits, and conditionally places ACC.7 from internal bit 0x0D.

The code now has separate route, short-packet, and ordinary-sequence models with regression coverage. These models preserve the firmware's raw packet fields and do not call them pitch, amplitude, sample address, or voice number.

The shared 0x0DB7 writer stores `[R5, R2, R3, R4 & 0xBF]` to XDATA `0x8000..0x8003`. After the fourth write it clears internal bit 0x0C and sets it only when the transmitted fourth byte retains bit 7. The new `MASS0DB7PacketBuilder` models that byte mask and software flag. This bit is a firmware-side protocol phase/state mirror; the synthetic ready response in the test fixture is not a hardware acknowledgement.

The resource command decoder is also regression-tested against the captured seven-byte IC51 group-08 record `08 20 18 A0 F6 00 00`, preserving the firmware register order `R2,R3,R4,R7,R6,R5`. This ties the resource record to raw handler inputs, and the pitch-slot guard ties the subsequent register writeback to the two XDATA state bytes. Physical units and the external receiver's interpretation are still unassigned.



## IC51 group-08 record to pitch-slot staging: evidence chain

The focused 1C61 live trace records the selected IC51 bank and seven consecutive bytes at 0x160C..0x1612:

`08 20 18 A0 F6 00 00`

The field decoder preserves these as opcode 0x08 and register inputs `R2=20, R3=18, R4=A0, R7=F6, R6=00, R5=00`. The handler then stages the descriptor fields in the current selected-page record area, including the group byte at offset 0x80 and the descriptor byte at offset 0x83. The later correction/writeback trace shows the actual pitch slot being populated high-to-low at PCs 0x1E60/0x1E63/0x1E66, with final bytes at XDATA 0x0081..0x0083; the 0x09B9 consumer reads those same three bytes low-to-high.

The secondary lookup is independently reproducible from the descrambled IC4 code ROM: the computed MOVC addresses are 0x397E, 0x39FE, and 0x3A7E, containing `05 43 02`. The packed-field decoder reproduces the live register tuple `R2=80, R3=60, R4=48, R5=05, R6=43, R7=02`. The 1C61 accumulator model then reproduces the intermediate `D5:1E:02`; the subsequent correction is a distinct step and yields the final slot vector `D5:1F:A2` in the Note 66 fixture. The later 09B9 consumer's high-byte mask and interpolation remain separate from this descriptor-field reconstruction.

### Executable group-08 projection

The repository now composes the recovered parts in `Source/MASSFirmwarePitchParameterExecution.h`:

1. Decode the seven-byte group-08 entry into the original IC4 register fields (`R2,R3,R4,R7,R6,R5`).
2. Resolve the signed interpolation coefficient from the caller-supplied XDATA `0x2067` snapshot and run the existing 24-bit fixed-point interpolation projection.
3. Resolve the secondary MOVC base from an explicit handler-boundary snapshot (`basePointer`, address-selection `R3/R4`, `RAM[0x2F]`), then add the command's original `R7` as the firmware does at `0x1D95`.
4. Read the three actual code-ROM locations at `DPTR`, `DPTR+0x80`, and `DPTR+0x100`; decode the packed fields and run the `0x1DC8..0x1E04` accumulator transform.
5. Return the raw pre-slot-correction 24-bit result, without assigning Hz, semitone, sample-address or DSP-register meaning.

The snapshot carries explicit validity flags for the interpolation control, secondary-address inputs and the direct-RAM `0x05–0x07` bypass (required when interpolated `R2==0`). The projection refuses to continue when any required boundary snapshot is missing. It does not assume that the address-selection values can be derived from the raw IC51 record.

`MASSOriginalVoiceProgramRuntime::planNextPitchCommand()` now consumes the next group-08 command from the selected voice's IC51 stream and passes it through this projection. The runtime regression locates a real factory group-08 record and checks the cursor handoff using an explicitly synthetic code-ROM boundary fixture. Separately, the CMake test now supplies the actual descrambled IC4 ROM; the real-ROM test checks that addresses `0x397E/0x39FE/0x3A7E` contain `05 43 02`, decodes `R3=60,R4=48,R5=05,R6=43,R7=02`, and reproduces the known pre-slot accumulator vector `D5:1E:02` from explicit test snapshots.

This is an executable composition of recovered firmware stages, not proof that the original group-08 record's pointer chain or every Note-66 handler input is fully resolved. The final `D5:1F:A2` slot correction remains a separate `0x1E4B..0x1E66` stage, and the 09B9 consumer remains separate. The original host-device response and actual SAM9407 register mapping are still unknown.



A separate IC51 Performance 1 / Part 13 resource-stream decode finds a seven-byte group-08 pitch-parameter command at stream offset 0x0179 (`0E 64 CC 05 9E 0C 5C`). It is consistent with the same handler class, but the current evidence does not prove that this exact stream record is the pointer source for the 0x160C..0x1612 bytes in the live Note 66 capture. Keep the two observations linked as a candidate relationship, not a completed pointer chain, until the command-stream cursor is captured through the pointer-table lookup.

This now connects a real IC51 record to the firmware register tuple, the 1C61 transform, the three-byte XDATA pitch slot, the 09B9 consumer, and the 0x0A4F packet writer. It still does not map the raw values to Hz, semitones, sample addresses, amplitude, or named external-device registers.


### 0x0DCE phase/readback companion

The external-window exchange continues through companion helper 0x0DCE. It compares requested R4.7 with internal bit 0x0C. A mismatch causes a control write to XDATA offset 3: 0xC0 when the requested phase is set, or 0x40 when it is clear. If the phase already matches, that control write is skipped. The helper then reads offset 4, followed by offsets 0, 1, 2, and 3 into R5, R2, R3, and R4.

The repository now models this ordering with `MASS0DCEExchangeDecoder`, taking the five response bytes as external inputs. Tests cover both phase transitions and the no-write case. Offset 4 remains an opaque status byte: the model does not call it an acknowledgement and does not invent a peripheral response.


## Indexed two-byte curve producer: IC4 routine 0x02D1

The Note 72 trace now captures the full indexed-pair calculation that was previously only identified as a generic writer. The routine starts with `RAM[0x28]` as a slot index, reads the slot's scale/guard byte from `XDATA[0x2054 + RAM[0x28]]`, computes a two-byte result, and writes it at:

- `XDATA[0x2000 + 2*RAM[0x28]]` = R5 (high byte)
- the next XDATA byte = R4 (low byte)

Thus index `0x0C` maps to `0x2018/0x2019`. A zero guard bypasses the curve/table math and writes `00 00`.

### Exact ROM coefficient tables

The routine selects one of two 48-byte tables from IC4 v1.05 code ROM:

- `0x0098` when the signed input takes the negative branch.
- `0x00C8` when the signed input takes the nonnegative branch.

Each table contains twelve four-byte records: a 16-bit cumulative base followed by a 16-bit segment slope. The positive table is:

| Record | Base | Slope |
|---:|---:|---:|
| 0 | `0000` | `079C` |
| 1 | `079C` | `0811` |
| 2 | `0FAD` | `088B` |
| 3 | `1838` | `090D` |
| 4 | `2145` | `0997` |
| 5 | `2ADC` | `0A29` |
| 6 | `3505` | `0AC4` |
| 7 | `3FC9` | `0B67` |
| 8 | `4B30` | `0C15` |
| 9 | `5745` | `0CCD` |
| 10 | `6412` | `0D90` |
| 11 | `71A2` | `0E5E` |

The negative table uses the same segment ordering in reverse slope progression:

| Record | Base | Slope |
|---:|---:|---:|
| 0 | `0000` | `0E5E` |
| 1 | `0E5E` | `0D90` |
| 2 | `1BEE` | `0CCD` |
| 3 | `28BB` | `0C15` |
| 4 | `34D0` | `0B67` |
| 5 | `4037` | `0AC4` |
| 6 | `4AFB` | `0A29` |
| 7 | `5524` | `0997` |
| 8 | `5EBB` | `090D` |
| 9 | `67C8` | `088B` |
| 10 | `7053` | `0810` |
| 11 | `7863` | `079D` |

In both tables each record's base plus slope reaches the next record's base, and the final base plus final slope equals `0x8000`. The positive slope sequence's geometric step is approximately `1.0594805`, about 16.5 ppm above the equal-temperament semitone ratio. This is strong evidence that the tables encode a twelve-segment exponential or pitch-related conversion curve. It is not yet proof of the physical unit or the external sound device's interpretation.

### Firmware arithmetic and executable projection

The input scale is the low nibble of the guard byte shifted into the high nibble (`(guard & 0x0F) << 4`). The two input bytes at `RAM[0x29]` and `RAM[0x2A]` are rotated, sign-tested around `0x80`, normalized on the negative branch, and converted into a segment index plus a 16-bit fractional value. The selected record's base is then combined with the interpolated segment offset.

The multiplication at `0x035E..0x037E` is a firmware-specific three-product fixed-point approximation, not a normal full 16×16 multiply. It uses `slopeHigh*fractionHigh`, `slopeLow*fractionHigh`, and `slopeHigh*fractionLow`; the low×low product is not computed. The implementation `MASSIndexedPairMathProjection` mirrors the 8-bit rotates, carry propagation, table selection, table fetch, three partial products, base addition and final two's-complement step. Its tests use the ROM's original 96 coefficient bytes rather than synthetic approximations.

The Note 72 live trace verifies slot index `0x0C` with scale `0xF0` and positive record 0 (`base=0x0000, slope=0x079C`). Both the `0x3F1B` and `0x4C8E` caller paths write `00 06` to `0x2018/0x2019` on the later curve-update passes. The `0x3F1B` loop is a 16-slot loop; its index is `RAM[0x6B]`. The `0x4C8E` path selects `RAM[0x6C]` and uses the normalized value in `RAM[0x70]`.

The resource handler reads `00 06` at `0x2018/0x2019` earlier in the Note 72 pass (cycles about `105,107,166` and `105,107,171`); the later curve writes happen around cycle `107,980,804` onward. These are separate time points. The later matching output corroborates the curve writer and target slot but does not prove that those later writes created the earlier consumed pair.

The projection is raw firmware math only. `0x2018/0x2019` remains an indexed state/output pair; the project does not yet identify it as a DSP pitch register, frequency in hertz, sample address, or modulation amount.
