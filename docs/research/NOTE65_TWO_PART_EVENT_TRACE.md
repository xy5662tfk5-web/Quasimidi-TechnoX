# One Note On dispatched to two Part event slots — real IC4 trace

Status: real IC4 v1.05 ROM execution captured by the Intel C++ regression after factory Performance initialization and the test's synthetic queue/DSP-ready setup. The MIDI stimulus is Note On key 65 (`0x41`), velocity 100 (`0x64`). The synthetic ready response is a test overlay, not a captured hardware response.

## 1. One MIDI event enters two runtime Part slots

The real firmware calls `0x4256` twice for the same key/velocity pair. Both calls return to `0x56C8`; the only event-slot field that changes is R7:

| Dispatch | Runtime event ID | MIDI key (R5) | Velocity (R3) | Firmware record index | Stored Performance Part |
|---:|---:|---:|---:|---:|---:|
| 1 | `0x0C` | `0x41` (65) | `0x64` (100) | 12 | Part 13 |
| 2 | `0x0D` | `0x41` (65) | `0x64` (100) | 13 | Part 14 |

The Part numbering is not inferred from the two calls alone. It follows the independent address correlation in `MIDI_EVENT_TO_PERFORMANCE_PART_MAPPING.md`: the Performance loader copies Part selectors 1–4 into runtime records 12–15, and event IDs 12–15 address those same records using `0x7C15 + 24*eventID`. For this experiment the test deliberately installs a synthetic read override at Common selector XDATA `0x7BD5` with value `1` (Layer 2); that value is not being claimed as a read from the factory Performance dump. Firmware routine `0x593D` expands the overridden selector into event IDs `0x0C` and `0x0D`, matching the two observed calls.

## 2. Two separate note-state maps and branch offsets

For key 65, the firmware's `0xAC00` Note On helper uses a 14-entry computed dispatch and page `P2=0x35`. The two event IDs produce byte offsets `0xC8` and `0xD8`. The bit within each byte is `65 % 8 = 1`; the computed Note On branch offset is `1 * 0x0E = 0x0E`.

The matching Note Off probe reaches `0xAC88`, then `0xACA0` and `0xACB2` twice. It addresses the same per-event byte offsets `0xC8` and `0xD8` on page `0x35`, but selects the separate 17-entry branch at offset `1 * 0x11 = 0x11`.

These are firmware byte/bit-address calculations. Page `0x35` is not identified as a physical DSP register page.

## 3. Separate resource-selection records are prepared for both event IDs

The two calls to `0x1494` both return to `0x446A`. They use the 12-byte per-Part resource-selection records in page `0x29`:

| Event ID | Record address | Descriptor address bytes (+5/+6) | 12-byte record (hex) |
|---:|---:|---:|---|
| `0x0C` | `0x2990` | `54 02` | `00 00 20 00 80 54 02 00 00 06 7F 00` |
| `0x0D` | `0x299C` | `58 FA` | `00 00 00 00 80 58 FA 00 00 06 7F 0F` |

The record bases are exactly 12 bytes apart, consistent with the 12-byte record structure read by `0x1494`. The firmware stores distinct descriptor-pointer bytes and otherwise retains several common fields. This is a per-Part resource-selection structure, not the 24-byte Performance Part parameter block itself.

## 4. Resource interpreter contexts also differ

The same Note 65 stimulus produces two `0x189D` entries (return PC `0x1720`):

| Event ID | R5 at `0x189D` entry | A | DPTR snapshot |
|---:|---:|---:|---:|
| `0x0C` | `0x7A` | `0x16` | `0x26E2` |
| `0x0D` | `0x63` | `0x03` | `0x274B` |

Those are observed architectural values at interpreter entry, not decoded DSP command semantics. The separate entry contexts are now asserted in the regression alongside the two resource-selection records.

## 5. Pitch interpolation is also visited twice

The same Note 65 stimulus visits the `0x09B9` interpolation route twice, both from return PC `0x1E6A`. The exact source triplets and arithmetic results are pinned in `PITCH_INTERPOLATION_SLOT_TRACE.md`: XDATA `0x0081–0x0083` (`57 01 A2`) produces `0x020171`; XDATA `0x0091–0x0093` (`01 00 48`) produces `0x000001`, both with step `+0x000D`.

Crucially, the follow-up Single-mode test still executes both `0x09B9` passes and reads both triplets even though only event ID `0x0C` enters `0x4256`. The interpolation passes therefore do **not** correspond one-to-one with the two active Part events. The two 24-bit slots remain an independent firmware structure whose semantic units are unknown; they are not declared sample addresses or DSP pitch words.

## 6. Common layer selector explains the two event calls

Routine `0x593D` reads the Common selector at XDATA `0x7BD5`, then sets internal event-enable bits before calling `0x5671`:

| `0x7BD5` | Performance mode | Event IDs enabled by `0x593D` |
|---:|---|---|
| `0` | Single | `0x0C` |
| `1` | Layer 2 | `0x0C`, `0x0D` |
| `2` | Layer 3 | `0x0C`, `0x0D`, `0x0E` |
| `3` | Layer 4 | `0x0C`, `0x0D`, `0x0E`, `0x0F` |

The current real-ROM fixture reads `0x7BD5=1`, and the firmware test now asserts that value next to the two Note 65 dispatches. This explains the two event calls in this run: the Common selector requests Layer 2. Per-Part enable/Track Mode checks still apply, so the table describes the candidate event IDs selected by the layer mode, not an unconditional promise that every candidate always reaches `0x4256`.

## 7. Layer 2 to Single differential

The follow-up regression changes only the same Common selector read-override from `1` to `0`, then repeats the same Note On (key `0x41`, velocity `0x64`) through the real ACIA/IC4 path. It asserts exactly one call each to `0x4256`, `0x1494`, `0x189D` and `0xAC00`, and pins the sole event ID to `0x0C`. Event `0x0D`, its `0x299C` selection record and its second Note On processing pass are no longer entered. This directly verifies the selector-driven expansion from two candidate Part events to one.

Both selector values are test overrides. The experiment verifies IC4's behavior when Common mode is set to Layer 2 or Single; it does not establish which mode is stored in any factory Performance's actual Common data.

## 8. Interpolation lanes are not one-to-one with Part events

The follow-up Single-mode run changes only the Common selector read-override from Layer 2 (`0x7BD5=1`) to Single (`0x7BD5=0`). The Part event path contracts to one event (`0x0C`) and one resource-selection/stream entry. However, the interpolation trace still records two calls at `0x09B9`, two arithmetic bodies at `0x09DA`, two packet writers at `0x0A4F`, and 20 watched MOVX accesses. It reads both triplets:

- `0x0081–0x0083`: `57 01 A2`;
- `0x0091–0x0093`: `01 00 48`.

The two interpolation passes therefore cannot be assigned one-to-one to Parts 13 and 14. They remain an independent internal pair of parameter/interpolation lanes with unresolved ownership and units.

## 9. What this establishes—and what remains open

Established by real-ROM execution and passing regressions:
- One key 65 / velocity 100 MIDI Note On reaches event IDs `0x0C` and `0x0D` separately.
- Those event IDs map to the first two stored Performance Part records, Parts 13 and 14.
- Note On and Note Off use matching event-specific byte offsets on page `0x35`, with separate computed-jump strides.
- The two event paths build separate 12-byte resource-selection records and enter `0x189D` with different register/pointer contexts.
- The interpolation handler reads two different 24-bit XDATA source triplets and produces distinct results.

Still open:
- Whether the same selector-to-event expansion is identical in all Performance and Sequencer Multi states, and how Track Mode/Part enable bits suppress individual event IDs in each operating mode.
- The complete descriptor/command-stream memory ownership and the meaning of the `0x189D` entry registers.
- The physical device behind the `0x8000–0x8003` port, its actual response/handshake, and the semantics of its command bytes.
- The sample/waveform memory format and the original M.A.S.S. oscillator/voice algorithm.

Next probe: repeat the differential with selector values `2` and `3` to validate the three- and four-layer event sets while keeping the same Note 65 stimulus. Do not turn the number of firmware Part events into a claim about the external processor's voice count.
