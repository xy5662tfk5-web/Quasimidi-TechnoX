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

## Next differential probe

Capture the same two slot addresses for note 64 and note 65 under identical Part/program/velocity. Then vary only the active Performance Part / event ID and verify whether the selected slot pair changes. This will show whether the 16-byte slot stride corresponds to event/voice records, a pair of interpolation lanes, or another firmware structure. Continue to keep the synthetic ready response labelled as a test fixture.

## Layer-mode differential: both pitch lanes remain active in Single

The same IC4 Note 65 stimulus was repeated with the Common selector read-override changed from Layer 2 (`0x7BD5=1`) to Single (`0x7BD5=0`). The MIDI dispatch/resource path collapsed from two Part events to one (`0x4256=1`, `0x1494=1`, `0x189D=1`, `0xAC00=1`; sole event ID `0x0C`).

Despite that reduction, the interpolation trace still records two entries at `0x09B9`, two arithmetic bodies at `0x09DA`, two packet-writer entries at `0x0A4F`, and 20 watched MOVX events. It reads the same two source triplets:

- `0x0081–0x0083`: `57 01 A2`;
- `0x0091–0x0093`: `01 00 48`.

This is decisive negative evidence against assigning one interpolation triplet to each enabled Performance Part. The pitch slots are independent from the number of candidate Part events selected by the Common layer mode. They may represent two interpolation lanes or another internal pair of parameter states, but their exact ownership/semantics remain unidentified. The physical sound device and sample format are still unknown.
