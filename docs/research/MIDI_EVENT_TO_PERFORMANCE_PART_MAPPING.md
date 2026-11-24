# Mapping MIDI event IDs to the four stored Performance Parts

Status: strong firmware-level mapping from independent address arithmetic and SysEx slot numbering. This identifies the four active event slots with Performance Parts 13–16; it does not identify the physical audio processor or the final voice-start command.

## 1. Three independent numbering systems converge

### Performance SysEx block selector

The supplied factory bank contains four Part frames per Performance. In each 33-byte frame:

- byte 5 is the Performance address high byte;
- byte 6 is the Part block selector, 1 through 4;
- bytes 8–31 are the 24 Part parameter bytes.

The existing parser maps selectors 1–4 to user-facing Part numbers 13–16.

### Performance loader destinations

Firmware routine `0xB475` copies those four 24-byte blocks to:

| SysEx selector | Payload destination | 24-byte runtime record |
|---:|---:|---:|
| 1 | `0x7D35–0x7D4C` | index 12 |
| 2 | `0x7D4D–0x7D64` | index 13 |
| 3 | `0x7D65–0x7D7C` | index 14 |
| 4 | `0x7D7D–0x7D94` | index 15 |

The 16-record table begins at `0x7C15`, with 24 bytes per record:

```
record_base(i) = 0x7C15 + 24*i
```

Thus the four copied Performance blocks occupy records 12–15 exactly.

### MIDI event IDs

The general MIDI event dispatcher creates four event IDs: `0x0C`, `0x0D`, `0x0E`, and `0x0F`. Routine `0x5671` uses each event ID in the same record-base arithmetic:

```
event_record_base(id) = 0x7C15 + 24*id
```

Since hexadecimal `0x0C–0x0F` equals decimal 12–15, the event dispatcher addresses precisely the same four runtime records that the Performance loader fills.

## 2. Consequence: the four Performance Parts are the four active melodic event slots

The following evidence now converges:

1. The SysEx block selectors 1–4 represent Parts 13–16.
2. The loader copies their parameter bytes to runtime record indices 12–15.
3. The general MIDI dispatcher emits event IDs 12–15.
4. The MIDI Note On path in `0x5671` passes that event ID to `0x4256`.
5. `0x4256` uses `0x7C15 + 24*eventID` to access the matching record.

This is a strong, internally consistent one-to-one mapping:

| Runtime event ID | Runtime record index | User-facing Performance Part |
|---:|---:|---:|
| `0x0C` (12) | 12 | Part 13 |
| `0x0D` (13) | 13 | Part 14 |
| `0x0E` (14) | 14 | Part 15 |
| `0x0F` (15) | 15 | Part 16 |

The apparent off-by-one is intentional in the data model: runtime record index 12 is displayed as Part 13, because the firmware/event-slot index is zero-based while the manual's Part number is one-based.

## 3. Parameter reads confirm that Note On consumes the loaded Part record

Within the `0x4256` Note On path, the firmware reads fields using the event ID as an index:

- `0x7C00 + 24*id + 0x18` → record base + 3: Level.
- `0x7C00 + 24*id + 0x19` → record base + 4: Panorama.
- `0x7C00 + 24*id + 0x1C` → record base + 7: Transpose.
- `0x7C00 + 24*id + 0x26` → record base + 17: Velocity curve.

These offsets independently agree with the official 24-byte Part parameter map. This ties the Note On event ID to the actual Part parameter record, rather than merely showing coincidental reuse of a 4-slot counter.

## 4. What this establishes

Confirmed to a high level from firmware and SysEx structure:

- The four stored Performance Part blocks are the runtime records at indices 12–15.
- MIDI event IDs 12–15 index those same records.
- The general Note On path uses the event ID to read the matching Part's parameters.
- The plugin's existing mapping of SysEx selectors 1–4 to Parts 13–16 and its 169-byte raw Performance image layout are consistent with the firmware.

Still unresolved:

- Whether the 24-byte records reside in ordinary RAM, dual-port RAM, or a memory window belonging to a coprocessor.
- How the descriptor built by `0x4256` is consumed by `0x1494`, `0x16C3`, and the page-selected mailbox.
- The physical identity of the 80-pin QFP and the peripheral behind `0x8000–0x8003`.
- The voice allocator's actual hardware command and the sample/oscillator playback engine.

## 5. Next reverse-engineering step

The next useful work is not more speculative Part numbering. It is to trace the 13-byte descriptor fields produced by `0x4256` through `0x1494` and `0x16C3`, then classify the command tuples written to `0x8000–0x8003` by event ID. That is the remaining path from a confirmed MIDI Part event to the unidentified hardware boundary.

## 5. Live Note 65 dispatch confirms the first two mappings

The real-ROM regression sends one Note On (key `0x41`, velocity `0x64`) and captures two separate calls to `0x4256` from return PC `0x56C8`:

- event `0x0C`, key `0x41`, velocity `0x64` → runtime record index 12 / Performance Part 13;
- event `0x0D`, key `0x41`, velocity `0x64` → runtime record index 13 / Performance Part 14.

The loaded Common selector at XDATA `0x7BD5` is `1` (Layer 2). Routine `0x593D` maps selector values 0, 1, 2 and 3 to respectively one, two, three and four candidate event IDs beginning at `0x0C` (`0x0C`; `0x0C–0x0D`; `0x0C–0x0E`; `0x0C–0x0F`). This directly explains why the Layer 2 trace makes two calls. Per-Part enable and Track Mode conditions remain additional gates.

The same capture observes distinct 12-byte resource-selection records at `0x2990` and `0x299C`, and two downstream `0x189D` entry contexts. Full register values and regression details: `NOTE65_TWO_PART_EVENT_TRACE.md`.