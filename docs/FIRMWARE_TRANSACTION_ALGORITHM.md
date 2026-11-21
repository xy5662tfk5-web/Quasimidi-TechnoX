# Firmware transaction algorithm — IC4 v1.05

Status: reverse-engineering specification, not yet a complete sound-engine implementation.

Evidence: reachable-code disassembly of the IC4 MCS-51 image. Static analysis may miss indirect dispatch, table-driven branches, dynamic page selection, and runtime state. All unknown semantics remain explicitly unknown.

## 1. Scope and naming

The current task is to model the common transaction loader at `0x1494`, the MIDI-event preparation routine at `0x4256`, the initialization path at `0x367B`, and the special-event path at `0x44BA`.

Important distinction:

- `RAM[0x29]` is the XDATA page selector copied to `A0`.
- `RAM[0x2A]` is the byte offset loaded into `R0`.
- `RAM[0x2F]` and `RAM[0x30]` are internal-RAM byte variables.
- Bit-address instructions such as `JB 0x2F` refer to bit-address space and must not be confused with byte `RAM[0x2F]`.

## 2. Common loader input format (`0x1494`)

At entry, the loader performs:

```asm
14BF  MOV 0xA0,0x29
14C2  MOV R0,0x2A
14C4  MOVX A,@R0
14C5  ANL A,#0x7F
14C7  MOV 0x2F,A
14C9  INC R0
14CA  MOVX A,@R0
14CB  MOV 0x30,A
```

Therefore the source is a 12-byte block in XDATA page `RAM[0x29]`, starting at offset `RAM[0x2A]`. The loader reads the following fields:

| Source offset | Destination / effect | Evidence |
|---:|---|---|
| +0 | `RAM[0x2F] = byte & 0x7F` | 14C4–14C7 |
| +1 | `RAM[0x30] = byte` | 14C9–14CB |
| +2 | `RAM[0x31] = ROM[0x1208 + byte]` | 14CE–14D3 |
| +3 | `RAM[0x33] = byte` | 14D5–14D7 |
| +4 | `RAM[0x34] = byte` | 14D9–14DB |
| +5 | `RAM[0x37] = byte` | 14DD–14DF |
| +6 | `RAM[0x36] = byte` | 14E1–14E3 |
| +7 | skipped | 14E5–14E6 |
| +8 | skipped | 14E6–14E7 |
| +9 | bits 0–2 copied to flags E0–E2 | 14E8–14F3 |
| +10 | `RAM[0x32] = ROM[0x1208 + byte]` | 14F5–14F8 |
| +11 | `RAM[0x35] = byte` | 14FA–14FC |

This is a structured transaction descriptor, not a simple two-byte command/value pair.

## 3. Loader branch condition

The first branch discriminator is exact:

```asm
151E  MOV A,0x30
1520  JNZ 0x1572
```

- If source byte +1 is zero, execution follows the zero-byte path beginning at 0x1522.
- If source byte +1 is nonzero, execution jumps to 0x1572.

This branch is not equivalent to Note On versus Note Off. The initialization path `0x367B` and special-event path `0x44BA` also use this loader, so the condition classifies transaction behavior, not musical event type by itself.

### Zero-byte path (overview)

At 0x1522, bit 0x06 selects between:
- reading the slot-control byte at XDATA `0x28D8 + RAM[0x28]` and jumping to `0x0E8E`; or
- computing a descriptor-dependent address, clearing bit 7 of the selected XDATA byte, and continuing through the resource/status logic.

### Nonzero-byte path (overview)

At 0x1572:
- if `RAM[0x52] == 0`, call `0x17D4`;
- if bit 0x06 is clear, continue at `0x1599`;
- otherwise, compute the selected address, set bit 7 in its current byte, and write `RAM[0x30]` to the adjacent byte;
- bit 0x04 can cause a retry/loop back to 0x1544.

The full routine subsequently inspects slot state and may reach the external-interface path. Do not assume every loader call immediately reaches the same hardware writes.

## 4. External interface path

When execution reaches `0x16C3`, the firmware selects the page dynamically:

```asm
16C3  MOV 0xA0,0x2D
```

The routine then accesses offsets including:

- `FB`: status/control handshake;
- `FC`: mode nibble derived from `RAM[0x28]`;
- `FE–FF`: computed pointer;
- `61–62`: second computed pointer;
- `F8`: `RAM[0x2F]`;
- `60`: `RAM[0x30]`;
- `FD`: status read/modify/write.

These are offsets on the selected XDATA page, not proof of a particular sound-chip register map. The page value is runtime state in `RAM[0x2D]`.

## 5. Source builder A — MIDI event preparation (`0x4256`)

Inputs saved on entry:

- `R7 -> RAM[0x6E]`: part/record index;
- `R5 -> RAM[0x6F]`: note number on the known MIDI call path;
- `R3 -> RAM[0x70]`: velocity on the known MIDI call path.

The routine writes a 12-byte record at:

`0x2900 + 12 * RAM[0x6E]`

Confirmed writes:
- +0: pitch after applying per-part tuning/transpose and clamping to the firmware's observed range 2…127;
- +1: velocity transformed by `0xABBF`; the mapper returns zero unchanged when input is zero;
- +3: mapped value produced through `0x695A` from the source part record.

The routine also writes/updates auxiliary state outside this 12-byte record, including bitmap pages handled by `0xAC00` and `0xAC88`. Do not fill unknown record bytes with guessed meanings.

It stores the record address as a big-endian XDATA pointer in `0x2B98–0x2B99`, then calls `0x1494` with mode values derived from the part index and the part record's field +0x17. Observed mode forms include:
- `index`;
- `index + 0x10`;
- `index + 0xC0` when source field +0x17 equals 2;
- `index + 0xE0` when source field +0x17 equals 3.

These are mode/slot selectors in the loader path; their final semantic names are not yet established.

## 6. Source builder B — initialization/setup (`0x3603–0x367B`)

The routine prepares a record in the `0x28E8 + 12 * index` region, then sets:
- `RAM[0x28]` from saved input `R7`;
- `RAM[0x29]` from input `R6`;
- `RAM[0x2A]` from input `R7`;

and calls `0x1494`.

This is a distinct source region from the MIDI event record at `0x2900 + 12 * index`. It is used during initialization as well as other setup paths. It must not be classified as Note On.

## 7. Source builder C — special event (`0x44BA`)

The routine stores its inputs `R7` and `R5`, prepares auxiliary state, and writes:
- `0x296C = input R7`;
- `0x296D = mapped input R5` (through `0xABBF`, using `0x7CFE` as the mapping source).

It then invokes `0x1494` with:
- page `0x29`;
- offset `0x6C`;
- mode `0x09` or `0x49`, depending on the event branch.

The second byte at `0x296D` is nonzero for the ordinary mapped cases, but this alone does not imply voice start. The routine also updates `0x2971–0x2975` and `0x2B80`.

## 8. Executable model — conservative pseudocode

```text
load_transaction(page, offset, mode, xdata, state):
    state.mode = mode & 0x0F, with E4..E7 folded into the high mode nibble
    p = page
    o = offset

    d[0]  = xdata[p][o] & 0x7F
    d[1]  = xdata[p][o + 1]
    d[2]  = rom[0x1208 + xdata[p][o + 2]]
    d[3]  = xdata[p][o + 3]
    d[4]  = xdata[p][o + 4]
    d[5]  = xdata[p][o + 5]
    d[6]  = xdata[p][o + 6]
    d[9]  = xdata[p][o + 9]
    d[10] = rom[0x1208 + xdata[p][o + 10]]
    d[11] = xdata[p][o + 11]

    flags.E0..E2 = d[9].bits(0..2)

    slot_base = 0x2068 + 0x80 * state.mode
    slot_base += d[0] only on the branch where firmware bit 0x06 is clear

    if d[1] == 0:
        follow_zero_byte_path(slot_base, flags, state)
    else:
        follow_nonzero_byte_path(slot_base, d[0], d[1], flags, state)

    if firmware state reaches external_interface_dispatch:
        dispatch using page RAM[0x2D] and the observed FB/FC/FE/FF/61/62/F8/60/FD sequence
```

This pseudocode deliberately leaves slot arbitration and hardware completion as state-machine calls. Those routines still need exact control-flow and status-bit modelling before the model can claim hardware equivalence.

## 9. Immediate implementation milestones

1. Implement a pure 12-byte descriptor decoder with explicit page and offset.
2. Implement the zero/nonzero byte branch as a deterministic state machine with injected XDATA and flags.
3. Add trace events for every read/write and branch decision.
4. Add fixtures for the three known source builders: `0x4256`, `0x367B`, `0x44BA`.
5. Only then implement the external page-`0x2D` handshake.
6. Verify against captured hardware traces or further firmware-derived test vectors before labelling any event as Note On/voice start.

## 10. Open questions

- Exact semantics of the `0x1208` lookup table.
- Full meaning of mode values passed in `RAM[0x28]`.
- Exact relationship between `0x0E8E`, `0x1456`, `0x1742`, `0x1009`, and resource arbitration.
- Which status transition at `0x2DFB/0x2DFD` confirms completion or starts a voice.
- Whether the IC4 external interface is directly connected to the synthesis device or an intermediary controller.
- Exact field meanings of unknown bytes in the `0x2900` and `0x28E8` records.

## 11. Confidence policy

Confirmed means directly visible in the disassembly and control/data flow described above. Hypothesis means a semantic interpretation not established by the current evidence. No unknown byte or register should be assigned a musical meaning solely from its value or address.
