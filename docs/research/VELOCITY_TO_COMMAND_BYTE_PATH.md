# Velocity-to-command byte path: verified IC4 instruction trace

Status: firmware-level mapping confirmed by the successful Intel macOS CI run for commit `384f7cb17bfcb0ec6878d9e301b089f6fd31c889` (workflow run `45327716234`). The experiment holds Part, program and pitch (note 64) constant and changes only MIDI velocity from 100 to 50. The test-only synthetic ready response remains enabled.

## Differential result

The isolated second Note On emits 549 four-byte SoundHost transactions in both cases. The first five transactions are identical except transaction 4:

| Order | Velocity 100 | Velocity 50 |
|---:|---|---|
| 1 | `00 A9 E4 01` | `00 A9 E4 01` |
| 2 | `02 01 00 00` | `02 01 00 00` |
| 3 | `05 FB D0 03` | `05 FB D0 03` |
| 4 | `0A 00 78 00` | `0A 00 98 02` |
| 5 | `0F 00 00 00` | `0F 00 00 00` |

Transaction 4 is emitted from the firmware path returning at `0x2122`; the 8032 writer sends R5, R2, R3, R4 to `0x8000..0x8003`.

## Instruction-level chain

The bounded 8032 execution trace captures 80 instructions from `0x1FC9` through the pre-execution snapshot at `0x210F`, plus eight XDATA accesses for each velocity.

1. The first fixed-point helper returns to `0x1FC9` with A=`0x10` (velocity 100) or A=`0x59` (velocity 50). The caller copies this value into B and stores it at XDATA `0x00D1` at `PC=0x2005`. The same caller reads `0x00D3=0x13` on both runs. This is a separate intermediate result, not the byte ultimately placed in the fourth packet.
2. The second fixed-point helper returns to `0x2027` with A=`0x0F` (velocity 100) or A=`0x53` (velocity 50). The helper inputs differ only in R2: `0x0F` versus `0x53`; A, B, R3 and R4 at entry are identical. The live return values match the reconstructed `MASS1848Arithmetic` model exactly.
3. The following instructions transform that second return value:
   - `0x2089: C4` — SWAP A (exchange high and low nibbles).
   - `0x208A: 03` — RR A (rotate right once).
   - `0x208B: FC` — MOV R4,A.
   - `0x208C: 54 F8` — ANL A,#0xF8.
   - `0x208E: FB` — MOV R3,A.
4. Consequently, A=`0x0F` becomes `0x78` after SWAP/RR; A=`0x53` becomes `0x9A`. R4 therefore holds `0x78` or `0x9A`, while masking with `0xF8` gives R3=`0x78` or `0x98`.
5. The packet builder at `0x210F) uses RAM[`0x2B`]=`0x0A`, R2=`0x00`, R3, and the low three bits of R4. This produces exactly `0A 00 78 00` and `0A 00 98 02`.

The first differing CPU state is at `0x1FC9`; the first differing XDATA access is the write to `0x00D1` at `0x2005`. The later packet-byte derivation is now explained by the second helper's return and the SWAP/RR/mask sequence above.

## Interpretation boundary

This closes the firmware dataflow from the velocity-sensitive `0x1848` result to bytes 2 and 3 of one observed four-byte command. It does **not** establish whether those bytes encode amplitude, envelope, sample position, pitch, or another external-processor parameter. The identity and response protocol of the device at `0x8000..0x8003` remain unknown; the synthetic ready response is not a hardware capture. The 544 later configuration-sweep packets are not reclassified as voice data by this result.

## Next experiment

Repeat the same instruction watch with a second pitch and a second active Part while holding velocity fixed. Assert the intermediate A/R4/R3 values at `0x2089`, `0x208B`, and `0x208E`, then compare which fields change with pitch and Part. Keep this separate from the still-unidentified host-port handshake and sample-memory path.
