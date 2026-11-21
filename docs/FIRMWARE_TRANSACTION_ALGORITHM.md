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
| +9 | bits 0–2 copied into internal bit addresses 0x00–0x02 | 14E8–14F3 |
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


## 12. Executable reference model added

The dependency-free header `Source/TechnoXFirmwareTransaction.h` now implements:

- a 256-page × 256-byte XDATA image with explicit page presence;
- a 12-byte descriptor decoder;
- injected lookup-table contents for the ROM table at 0x1208 (the table is not fabricated);
- the exact first branch on descriptor byte +1;
- a conservative branch planner for the observed 0x1522–0x1599 paths.

The planner models these observed actions:
- zero second byte + flag 0x06 clear: read `0x28D8 + mode`, then branch to `0x0E8E`;
- zero second byte + flag 0x06 set: clear the selected slot byte's high bit, scan status, then either load replacement bytes and continue at `0x1599`, or retry at `0x1544`;
- nonzero second byte: call `0x17D4` when `RAM[0x52]` is zero; if flag 0x06 is set, set bit 7 at the selected slot byte and write byte +1; flag 0x04 selects the retry edge.

The planner reports intended actions and addresses; it does not perform the firmware's asynchronous resource wait, emulate the external controller, or claim a Note On semantic.

Unit tests are in `tests/test_firmware_transaction.cpp`. They cover descriptor decoding, byte masking, lookup mapping, page/range errors, zero/nonzero branching, slot address calculation, and selected action sequences. A standalone GitHub Actions workflow compiles the test with C++17 and warnings-as-errors.


## 13. Slot scan and resource arbitration — newly traced

### 0x0E8E: select an available slot in the selected mode

Entry is reached from the zero-second-byte path of 0x1494 after reading the mode-control byte at XDATA 0x28D8 + mode.

Observed behavior:

1. Initialize RAM[0x31] = 0xFF, then read the two-byte slot-map pointer previously stored in RAM[0x3C:0x3D].
2. Cache the current map/status byte in RAM[0x3E]. If bit 0x20 is set, skip the initial free-slot test and enter the status-handshake path at 0x0EE6.
3. Otherwise increment the slot candidate counter RAM[0x31]. Candidate zero is checked against the bit mask RAM[0x2F] through indexed helper 0x1408. If the masked bit is clear, return.
4. Candidates are scanned up to 0x1F; each candidate selects a pair of bytes in XDATA page 0x2B, starting at offset 2 * candidate. The pair is compared against RAM[0x2F] and RAM[0x28]. A match identifies the candidate and loads its associated status byte at offset 0x41 + candidate.
5. The selected status byte is masked with 0x1F and used as the dynamic XDATA page (RAM[0x2D] and A0). The firmware then polls offset 0xFB and tests selected bits of the just-read accumulator value (ACC.3 through ACC.7, depending on the branch). These are accumulator-bit tests, not independent persistent flags. The tests control retries, writes to offset 0xFD, and progression into the lower-level slot descriptor processing beginning at 0x0F33.

The scan is bounded by 32 candidates. The code establishes a slot-selection/status protocol, but does not identify the selected slot as a musical voice number without further evidence.

### 0x1456: reserve/prepare selected slot, then dispatch

The routine begins with LCALL 0x1742. If the returned accumulator is 0xFF, it returns immediately. Otherwise the returned value is copied to RAM[0x2D] and used as the selected XDATA page.

It then writes RAM[0x2F] and RAM[0x28] to offsets 0x00 and 0x01 of XDATA page 0x2B; calls 0x1408 with the descriptor mask and selected page to compute a bitmap address; ORs the computed mask into that bitmap byte; increments XDATA counter 0x2B40; stores a per-page pointer derived from page + 0x41 in RAM[0x3C:0x3D]; writes page | 0x60 to the selected page's offset 0x41; and tail-jumps to 0x16C3.

This is evidence for a reservation/allocation step rather than direct voice start: the routine reserves a bitmap bit, updates a counter and page metadata, and only then enters the external-interface transaction.

### 0x1742: resource token / page queue manager

0x1742 reads RAM[0x52] and subtracts one.

- If the result underflows (original value was zero), it calls 0x1780.
- If 0x1780 returns zero, it retries the 0x1742 loop.
- If the result is nonzero, it decrements RAM[0x52], updates the linked page queue using RAM[0x50], RAM[0x51], RAM[0x4E], and RAM[0x4F], then returns the previous queue head in A.

The queue links are stored in internal RAM at 0x7E + page_id. The routine maintains a head/tail-like pair and increments/decrements RAM[0x52]; this is consistent with a page/resource availability queue. The exact semantic label of the resource remains unproven.

### 0x1780, 0x17AC, 0x17D4: page arbitration and retry

These routines select a candidate page from RAM[0x50], load its page into RAM[0x2D]/A0, and inspect status offset 0xFB.

- 0x1780 reads offset 0xFB into A and accepts the current candidate only when ACC.3 is set and ACC.5 and ACC.6 are clear. It then calls 0x1009, delays, calls 0x018D, and returns zero. If the candidate is not ready, it follows the page link at 0x7E + page_id and retries; if it reaches RAM[0x51], it transitions to the alternate candidate path at 0x17AC.
- 0x17AC performs a related check without the ACC.3 requirement: it requires ACC.5 and ACC.6 both clear before calling 0x1009, delaying, calling 0x018D, and returning zero. Otherwise it follows the linked page list; reaching RAM[0x51] returns without acquiring a page.
- 0x17D4 is a third retry path. It selects the same candidate chain and tests bits of the status byte in A. Its exact branch predicate must be read from the individual instruction path; do not treat E3/E5/E6 as persistent global flags or infer the condition from their names. This routine is invoked by the nonzero descriptor path when RAM[0x52] == 0.

The delay is a short software loop (R0=0, NOP, DJNZ); no time unit is inferred.

### 0x1009: discover a usable endpoint on a page

0x1009 selects page RAM[0x2D], derives a high-nibble group from the page ID, and scans offsets 0x80, 0x88, ... 0xF8.

A candidate byte is accepted when (value & 0x7E) == 0x16. The routine then switches to XDATA page 0x80, prepares arguments (R5 = group, R4 = 0, R3 = 1, R2 = 0x80), calls 0x0DB7, restores the original page, and continues scanning.

This is endpoint discovery/initialization on a selected page. The signature is observed in firmware; the endpoint's hardware identity is not established.

### 0x018D and 0x0211: validate and initialize selected page

0x018D rejects any selected page with upper three bits set (page & 0xE0 != 0) by jumping to address zero. For a valid page it selects that XDATA page and calls 0x0211, then updates the linked page queue and increments RAM[0x52].

0x0211 reads status offset 0xFB; if bit 5 of the returned byte (ACC.5) is set it raises internal bit address 0x0A. It calls 0x0DF7 with the selected page, clears offset 0xFB, writes 0x40 to offset 0xFA, then scans endpoint bytes beginning at offset 0x30. It initializes endpoint fields and checks associated flags. This is page/endpoint initialization, not proof of a synth voice register protocol.

### Updated interpretation and limits

The combined path is now clearer:

0x1494 → 0x0E8E → slot scan/status → 0x1456 → 0x1742 → page arbitration → 0x1009/0x018D → 0x16C3

The exact ordering varies by branch: 0x1456 calls 0x1742 before writing reservation metadata and jumping to 0x16C3; 0x0E8E can also return early or enter the status path without reaching 0x1456.

Confirmed:
- candidate slots are scanned and tested using bitmap masks;
- page IDs and linked lists are maintained;
- a selected page's bitmap, counter, metadata, and endpoint table are updated;
- status bits gate retry/acquisition;
- 0x1456 enters the external-interface transaction only after the reservation work.

Not yet confirmed:
- the external controller's chip identity;
- whether a page corresponds to a physical voice, channel, DSP, or transport endpoint;
- which final status transition constitutes successful synthesis-command completion;
- whether the 0x2B page records represent voices, commands, or another resource abstraction.

No function in this chain is labelled Note On or voice-start at this stage.

## 14. External-interface transaction: write sequence and status return (0x15D1, 0x16C3, 0x172F, 0x1740)

This section records the instruction-level evidence recovered by re-downloading the IC4 v1.05 ROM from the research branch, applying the repository's IC4 address descrambler, and regenerating the reachable MCS-51 listing (16,349 decoded instructions). The addresses below are instruction addresses in the descrambled 64 KiB code image.

### 14.1 Entry context and branch into the interface path

The code at 0x16C3 is not an independent subroutine entry. It is reached from the common 0x1494 loader:

- At 0x169E the firmware reads RAM[0x3E] and transfers bit E5 into internal bit flag 0x08.
- At 0x16A4, if E5 is set, execution branches to 0x16C3.
- If E5 is clear, execution calls 0x1742 to acquire a page/resource, clears bit 0x06, and proceeds through the page-selection and transaction setup path.
- The allocator routine 0x1456 also tail-jumps to 0x16C3 after reserving a bitmap bit and updating page metadata.

Thus, the same external-interface setup is reached from more than one loader/resource state. Do not infer a musical event type from the entry alone.

### 14.2 Exact setup and write ordering at 0x16C3–0x172C

The selected page is dynamic: A0 = RAM[0x2D]. All following MOVX @R0,A accesses use this selected page and the low-byte offset in R0.

| PC | Instruction / operation | Effect |
|---|---|---|
| 16C3 | MOV A0, RAM[2D] | Select external XDATA page |
| 16C6–16C8 | R0=FB; A=RAM[3F] | Load control value; ACC.7 is tested |
| 16CA | JNB ACC.7, 16E4 | If bit 7 is clear, skip the read/active-control write |
| 16CD–16CE | Read FB; save old byte in R6 | Preserve previous status/control byte |
| 16CF–16D7 | Build 0x80, optionally OR 0x40 when internal bit-address flag 0x05 is set; write to FB | Active control write is 0x80 or 0xC0 |
| 16D8–16DB | Test bit 3 of the previously read FB | Tests old status, not the just-written value |
| 16DD–16DF | If old FB.3 is set: clear internal bit flag 0x06; set RAM[0x3F]=0x80 | Updates loader state for subsequent branch |
| 16E4–16E5 | Alternative: write 0x00 to FB | Used when the initial ACC.7 test fails |
| 16E6–16EC | Increment R0 to FC; write (RAM[0x28] << 4) & 0xF0 | Mode selector in upper nibble |
| 16ED–16F5 | Add 2 to RAM[0x36:0x37] | Advances the second pointer by two bytes |
| 16F7–16FF | Write RAM[0x3C] and RAM[0x3D] to FE and FF | First pointer/address pair |
| 1700–170C | Derive upper page group from RAM[0x2D]; update internal flag 0x0D; save group in RAM[0x2B] | Page/group metadata |
| 170E–1712 | Set RAM[0x44]=0x70; clear RAM[0x2E] | Prepare helper state |
| 1714–171C | Write RAM[0x36] and RAM[0x37] to offsets 61 and 62 | Second pointer/address pair |
| 171D | Call 0x189D | Builds/loads further transaction data |
| 1720–172C | If internal bit 0x06 is clear, write RAM[0x2F] to F8 and RAM[0x30] to 60 | Two payload bytes are written after helper 0x189D; when bit 0x06 is set these two writes are skipped |

The write order is therefore not simply “payload then trigger”: the firmware first selects the page and writes control/mode and pointer fields, invokes helper 0x189D, and only then conditionally writes the two payload bytes.

0x189D reads mode-indexed configuration bytes beginning at XDATA page 0x28, offset 0x68 + RAM[0x28] (and subsequent offsets), then reads descriptor bytes through the pointer in RAM[0x36:0x37]. It updates internal state and descriptor flags. Its role is a data-preparation helper; the available evidence does not identify a physical sound-chip register map.

### 14.3 Status read/modify/write at 0x172F–0x1741

After the optional payload writes, execution always reaches:

| PC | Instruction | Meaning |
|---|---|---|
| 172D | R0=FD | Select status/handshake offset |
| 172F | MOVX A,@R0 | Read current FD byte |
| 1730–173A | Copy internal bit flags 0x00, 0x01, 0x02 into ACC.0, ACC.1, ACC.2 | Replaces the low three bits with descriptor-derived flags saved earlier by 0x14E8–0x14F3 |
| 173C | CLR ACC.7 | Clear bit 7 in the value to be written back |
| 173E | CLR ACC.6 | Clear bit 6 in the value to be written back |
| 1740 | MOVX @R0,A | Write modified byte back to FD |
| 1741 | RET | Return to caller |

Important 8051 addressing detail: bit addresses 0xE0–0xE7 are bits 0–7 of the accumulator. The operations at 0x1730–0x173E modify the accumulator value read from FD; they are not writes to XDATA addresses 0xE0–0xE7.

This is a read/modify/write acknowledgement or state-normalization step: it preserves FD bits 3–5, replaces bits 0–2 from the descriptor flags, and clears bits 6–7 before writing back. The firmware clearly clears those two high bits in the returned control/status byte. Their hardware names and exact meaning (busy, request, acknowledgement, interrupt, etc.) remain unassigned until corroborated by controller documentation or captured bus traces.

### 14.4 Conditional readback at 0x15D1–0x15D8

This is an earlier branch in the same loader flow, not the continuation immediately after 0x1740:

1. The branch requires internal flag 0x06 to be set.
2. It requires bit E5 of RAM[0x3E] to be set.
3. It selects page RAM[0x3E] & 0x1F.
4. It reads FB; if ACC.3 is set, it skips the readback.
5. Otherwise it reads F8 into R5 and 60 into R6.

This establishes a conditional two-byte readback from the external page. No comparison against expected values occurs at 0x15D1–0x15D8, so this is evidence of data retrieval, not by itself proof of successful command completion.

### 14.5 Reconstructed transaction phases

The evidence supports this conservative sequence:

1. Page selection: A0 <- RAM[2D].
2. Control/status sampling: read FB; branch on its previous bit 3 and internal loader state.
3. Control write: write 0x80 or 0xC0 to FB, or write 0x00 on the alternate branch.
4. Mode and pointer setup: write FC, FE–FF, and 61–62.
5. Data preparation: call 0x189D.
6. Conditional payload transfer: if bit 0x06 is clear, write descriptor bytes to F8 and 60.
7. Status normalization/acknowledgement: read FD, replace low three bits with descriptor flags, clear bits 6 and 7, and write it back.
8. Separate conditional readback path: under the 0x15BF guards, read F8 and 60 only when FB.3 is clear.

The sequence is firmware-confirmed at the instruction level. The semantic interpretation of the external controller's bits and whether the final FD write is the command-acceptance acknowledgement or a general status reset are still hypotheses. No claim is made that this is specifically a synth voice-start command.


## 15. Corrected accumulator-bit interpretation and deeper trace of 0x189D / 0x104F

### 15.1 Critical MCS-51 addressing correction

In the 8051 architecture, direct bit addresses 0xE0–0xE7 are the eight bit-addressable bits of the accumulator SFR A (direct address 0xE0). Therefore:

- JB 0xE5 means test ACC.5 of the current A value.
- JNB 0xE7 means test ACC.7 of the current A value.
- CLR 0xE7 clears ACC.7 in the current accumulator.
- MOV C,0xE5 copies ACC.5 into carry.
- MOV 0xE1,C writes carry into bit-address 0xE1, which is an accumulator bit, not a general RAM flag.

These conditions are data-dependent. They do not denote persistent firmware flags by themselves. Any interpretation of E3–E7 must identify the immediately preceding load/operation that established A.

By contrast, bit addresses 0x00–0x7F map to the bit-addressable internal RAM area 0x20–0x2F. The instructions at 0x14E8–0x14F3 transfer descriptor byte +9 bits 0–2 into internal bit addresses 0x00, 0x01, and 0x02. The later checks JNB 0x00, JNB 0x01, JNB 0x02 in 0x189D therefore refer to persistent internal bit flags.

### 15.2 Confirmed writers of RAM[0x3E] and RAM[0x3F]

The reachable-code cross-reference pass finds these direct writes:

| Address | Destination | Operation |
|---|---|---|
| 0x0E98 | RAM[0x3E] | MOV RAM[3E],A |
| 0x0EE1 | RAM[0x3E] | MOV RAM[3E],A |
| 0x148E | RAM[0x3E] | MOV RAM[3E],A |
| 0x15AC | RAM[0x3E] | MOV RAM[3E],A |
| 0x1699 | RAM[0x3E] | MOV RAM[3E],A |
| 0x16C1 | RAM[0x3E] | MOV RAM[3E],A |
| 0x1498 | RAM[0x3F] | MOV RAM[3F],#0x00 |
| 0x14BD | RAM[0x3F] | MOV RAM[3F],A |
| 0x16DF | RAM[0x3F] | MOV RAM[3F],#0x80 |

This list is from reachable, instruction-aligned code in the current 64 KiB descrambled map. It is a direct-write inventory, not a semantic classification of every value.

RAM[0x3F] is initialized to zero at 0x1498. The conditional path at 0x14B3–0x14BD can set it from a mode-indexed byte at XDATA page 0x20, ORed with 0x80. The interface routine at 0x16C8 tests bit 7 of this value; when the old FB byte has bit 3 set, the code writes 0x80 back into RAM[0x3F] at 0x16DF. Thus RAM[0x3F] is a transaction-control byte whose high bit directly gates the FB control-write branch. Its full semantic name remains unknown.

RAM[0x3E] is a state byte written by the slot/resource paths and the loader. The code at 0x1694–0x1699 clears bit 5 in the value loaded from RAM[0x3E], then stores it back. Later branches test bits of this byte after loading it into A. It is therefore unsafe to describe E5 as a standalone global flag: in these locations it is simply bit 5 of RAM[0x3E]'s current value.

### 15.3 What 0x104F actually does

The reachable instructions show that 0x104F is not the voice-command writer. It is part of page/endpoint discovery and initialization:

1. Load selected page RAM[0x2D] into A0.
2. Derive a page-group value from the high nibble and store it in RAM[0x2B]; internal bit address 0x0D records one mode condition.
3. Read status offset 0xFB.
4. If ACC.5 is clear, read pointer bytes at offsets 0xFE and 0xFF, load DPTR, read/modify/write the pointed byte with mask 0xDF.
5. Scan the page's low-offset control area and clear/initialize entries. The loop is bounded by the firmware's 0x30 guard.
6. Scan endpoint descriptors at offsets 0x80, 0x88, ... 0xF8. A byte is a candidate when (value & 0x7E) == 0x16.
7. On a candidate, switch A0 to page 0x80, prepare group/endpoint arguments, and call 0x0DCE and 0x0DB7; restore the original selected page and continue the scan.
8. At completion, set bits 3 and 4 in status offset 0xFB, write 0xFF to offset 0x70, and return.

The endpoint signature and the page-0x80 helper calls are direct observations. The endpoint's physical identity is still unknown. This routine should be modelled as discovery/initialization, not as proof of a sound-generation command.

### 15.4 Deeper trace of 0x189D

0x189D is called from 0x171D in the interface transaction. Its initial operations build a mode-indexed DPTR in XDATA page 0x28:

- First read: offset 0x68 + RAM[0x28] → RAM[0x41].
- Second read: offset 0x78 + RAM[0x28] → RAM[0x43].
- Third read: offset 0x88 + RAM[0x28] → R3.
- Fourth read: offset 0xA8 + RAM[0x28] → R4.

It then replaces DPTR with the pointer in RAM[0x36:0x37]. Internal bit addresses 0x00–0x02 control three bit-addressable SFR bits:

- if internal bit 0 is set, clear bit address 0xB5;
- if internal bit 1 is set, clear bit address 0xB4;
- if internal bit 2 is set, clear bit address 0xB1.

The routine reads descriptor bytes through the pointer into RAM[0x38], RAM[0x39], RAM[0x3A], and RAM[0x3B], with additional branches that advance the pointer and alter RAM[0x2E]. It also sets the same SFR bits 0xB4, 0xB5, and 0xB1 again at several later points. These are direct writes to bit-addressable SFR space; whether the corresponding P3 pins are being used as GPIO control lines or alternate-function signals requires board-level corroboration.

At 0x19BD, the routine loads DPTR with 0x1BC3 and executes JMP @A+DPTR. This is computed control flow. The current reachability disassembler intentionally stops at indirect jumps, so it cannot yet enumerate the complete mode-dispatch targets or claim that every branch of 0x189D has been decoded. This is now the next focused target: recover the table at 0x1BC3, determine its entry widths and target addresses, then decode each target independently.

### 15.5 Consequences for the emulator

- Represent ACC bits E0–E7 as aliases of the current accumulator, never as persistent global flags.
- Represent bit addresses 0x00–0x02 as persistent internal RAM bits.
- Represent bit addresses 0xB1, 0xB4, and 0xB5 as bit-addressable SFR bits, not ordinary RAM flags.
- Model RAM[0x3E] and RAM[0x3F] as separate state bytes with the writers listed above.
- Keep 0x104F in the page/endpoint initialization subsystem.
- Do not complete the 0x189D model until the computed jump table at 0x1BC3 and its targets are decoded.


## 16. Computed mode dispatch inside 0x189D — table recovered

The computed jump at 0x19BD is now partially resolved from the descrambled ROM bytes.

Control flow immediately before the jump:

- At 0x19A2, A is masked with 0x38.
- If the result equals 0x38, execution takes the separate special path beginning at 0x19A7.
- Otherwise, the value is rotated right once at 0x19BD. For the aligned values 0x00, 0x08, 0x10, 0x18, 0x20, 0x28, and 0x30, this produces offsets 0, 4, 8, 12, 16, 20, and 24.
- DPTR is loaded with 0x1BC3 and `JMP @A+DPTR` dispatches into a table of four-byte entries.

Recovered table:

| Masked input A | Table address | Bytes | Destination |
|---:|---:|---|---:|
| 0x00 | 0x1BC3 | 02 1C 2B 00 | 0x1C2B |
| 0x08 | 0x1BC7 | 02 1C 61 00 | 0x1C61 |
| 0x10 | 0x1BCB | 02 1E DD 00 | 0x1EDD |
| 0x18 | 0x1BCF | 02 22 4C 00 | 0x224C |
| 0x20 | 0x1BD3 | 02 23 01 00 | 0x2301 |
| 0x28 | 0x1BD7 | 02 23 74 00 | 0x2374 |
| 0x30 | 0x1BDB | 02 23 74 00 | 0x2374 |

Each entry begins with an 8051 LJMP (opcode 0x02) followed by a 16-bit destination and a trailing zero byte. The computed jump therefore lands on the LJMP instruction, which transfers control to the corresponding handler. Inputs whose masked value is 0x38 do not use this table; they take the separate 0x19A7 path.

This resolves the dispatch destinations but not the full semantics of each handler. The next reverse-engineering pass must disassemble each target with instruction-aligned control flow, trace its pointer accesses and P3-bit writes, and compare the common data-path operations. The target list is now grounded in the actual ROM bytes rather than inferred from nearby code.


## 17. First instruction-aligned decode of the 0x1BC3 handlers

The seven table destinations were decoded independently from the descrambled IC4 bytes. This section records control/data-flow facts only; handler names are descriptive working labels, not claimed hardware semantics.

### 17.1 0x1C2B — descriptor gate and dispatch

At entry, bit address 0x0A selects an alternate path. On the ordinary path, the routine derives DPTR = RAM[0x36:0x37] + 3, applies the descriptor bit flags 0x00–0x02 to SFR bits 0xB5/0xB4/0xB1, reads one XDATA byte, restores those SFR bits, and tests bit 3 of the byte.

- If bit 3 is clear, it advances the pointer, saves it to RAM[0x36:0x37], and jumps to 0x19C2.
- If bit 3 is set, it jumps to 0x224C.

The branch is therefore based on a byte read through the descriptor pointer; it is not a persistent global status flag.

### 17.2 0x1C61 — descriptor-field transfer and table-driven transformation

The handler saves RAM[0x36:0x37] as DPTR, applies the three descriptor-controlled SFR strobes, then reads a run of bytes through MOVX A,@DPTR into A/R2/R3/R4/R7/R6/R5. It saves the advanced pointer back to RAM[0x36:0x37].

It then uses R1 = RAM[0x2E] as an indirect XDATA destination and writes the fields into a record/buffer. The record layout is not a simple byte-for-byte copy: the fourth field is conditionally merged with existing low three bits, one field is masked with 0x7F, and later fields are copied separately. The path consults XDATA 0x2067 and ROM tables beginning at 0x11F0, 0x1288, and 0x1308, then reaches a shared continuation at 0x1EDD through the branch at 0x1E37.

The table-derived values and the complete loop/exit conditions remain to be modelled. In particular, do not infer musical parameter names from the table indices.

### 17.3 0x1EDD — record construction and external-window output

This handler begins by clearing RAM[0x3E], copies the descriptor pointer into DPTR, applies the three descriptor-controlled SFR strobes, and reads a sequence of descriptor bytes into R2/R3/R5/R6/F0/R4/R7. It writes a record through MOVX @R1,A, with R1 initially loaded from RAM[0x2E]. Several fields are transformed using current state (including RAM[0x30], RAM[0x3F], RAM[0x40], RAM[0x41], RAM[0x43], and RAM[0x3E]) and ROM lookup tables at 0x1288 and 0x1308. The code also calls helpers 0x2381 and 0x1848.

A confirmed external write sequence occurs at 0x20CC–0x210B. It uses MOV DPTR,#0x8000 and MOVX writes to four consecutive XDATA addresses, 0x8000–0x8003:

- Optional block, emitted only when internal bit address 0x0A is set: byte 0 = RAM[0x2B] - 2; bytes 1–3 are read from the record area relative to RAM[0x2E] (the third is masked to three bits, with a conditional ACC.7 adjustment).
- Second block: [RAM[0x2B], R5, R6, R7].
- Third block: [RAM[0x2B] - 1, R2, R3, R4].

The second and third blocks are emitted on the ordinary path regardless of the optional-block condition. The code then jumps to 0x19C2. If the alternate branch at 0x2092 is taken, it instead prepares R2=0 and calls 0x0DB7 with A0=0x80 before restoring A0 from RAM[0x2D].

This is the strongest current evidence that the table-dispatched descriptor machinery feeds a separate external XDATA device/window. The firmware proves writes to 0x8000–0x8003; it does not identify the receiving IC or the semantic meaning of the four bytes. Existing MAME no-op mappings for this range are emulator mapping evidence, not proof that the physical hardware ignores the writes.

### 17.4 0x224C — page-0x80 endpoint operation

This handler can divert immediately to 0x1BE2 when internal bit address 0x08 is set. Otherwise it reads three bytes from the descriptor pointer, stores one field in the record area at RAM[0x2E], applies the descriptor SFR strobes, and prepares the page-0x80 endpoint operation.

It selects A0=0x80, derives a group value from RAM[0x2B], and under the relevant mode conditions writes a short control/argument block through MOVX @R0. It then calls 0x0DB7, restores A0 from RAM[0x2D], and jumps to 0x19C2. The call and page selection establish an endpoint operation, but do not establish the endpoint's physical identity.

### 17.5 0x2301 — record initialization plus endpoint dispatch

When internal bit address 0x0A is clear, the handler walks the record area in eight-byte steps and fills selected entries with 0x38; otherwise it sets RAM[0x2E] to 0xF8. It then reads descriptor bytes through RAM[0x36:0x37], stores fields beginning at offset 0xF9 of the indirect XDATA area, and merges the low three bits of an existing field with the descriptor's low three bits.

The routine calls 0x2128 and 0x21B7, conditionally sets ACC.7 based on internal bit 0x0D, derives an endpoint/group argument from RAM[0x2B], selects A0=0x80, calls 0x0DB7, restores A0 from RAM[0x2D], and jumps to 0x19CD. This is a second distinct page-0x80 endpoint path; the exact payload semantics are not yet known.

### 17.6 0x2374 — pointer advance

This short handler increments the 16-bit pointer in RAM[0x36:0x37] by one, including carry into the high byte, then jumps to 0x19C2. It is a descriptor-iteration helper, not an independent peripheral-write sequence.

### 17.7 0x19A7 — special descriptor-record path

The special branch for masked mode value 0x38 writes the current accumulator byte to XDATA through R1 = RAM[0x2E], increments DPTR, saves DPTR back to RAM[0x36:0x37], advances RAM[0x2E] by eight, increments RAM[0x2B], and decrements RAM[0x2C]. If the loop counter remains nonzero it jumps back to 0x1986; otherwise it returns.

This path advances a repeated eight-byte record/descriptor sequence. It is not one of the seven four-byte LJMP table entries.

### 17.8 Current interpretation boundary

The handler family now contains three visibly different operation classes:

1. Descriptor reads and transformed record construction in the indirect XDATA area addressed from RAM[0x2E].
2. Explicit external-window writes to XDATA 0x8000–0x8003 in the 0x1EDD path.
3. Page-0x80 endpoint/control operations through A0=0x80 and helper 0x0DB7 in the 0x224C and 0x2301 paths.

The next required step is to trace the caller-provided values of RAM[0x2E], RAM[0x2B], RAM[0x2C], RAM[0x36:0x37], and internal bits 0x08/0x0A/0x0D across real entry paths. Then compare these writes against the board-level bus wiring or captured hardware traffic. Until that is done, 0x8000–0x8003 is an identified external XDATA write window, not a named sound-chip register bank.


## 18. Shared four-byte external write helper and eight-byte record iteration

### 18.1 0x0DB7 writes the same 0x8000–0x8003 window

The helper at 0x0DB7 is short and instruction-aligned:

```asm
0DB7  MOV R0,#0x00
0DB9  MOV A,R5
0DBA  MOVX @R0,A
0DBB  MOV A,R2
0DBC  INC R0
0DBD  MOVX @R0,A
0DBE  MOV A,R3
0DBF  INC R0
0DC0  MOVX @R0,A
0DC1  MOV A,R4
0DC2  CLR 0xE6
0DC4  INC R0
0DC5  MOVX @R0,A
0DC6  CLR 0x0C
0DC8  JNB 0xE7,0x0DCD
0DCB  SETB 0x0C
0DCD  RET
```

Callers select A0=0x80 before entering this helper. Since R0 starts at zero, the four MOVX writes target XDATA 0x8000, 0x8001, 0x8002, and 0x8003 in order. The packet is:

- 0x8000 = R5
- 0x8001 = R2
- 0x8002 = R3
- 0x8003 = R4 with ACC.6 cleared immediately before the write

After the write, internal bit address 0x0C is cleared and then set only if ACC.7 of the modified R4 value is set. This is a software copy of the outgoing fourth byte's bit 7. The companion helper 0x0DCE uses it as protocol phase state; it is not established as a hardware acknowledgement.

This independently confirms that the 0x8000–0x8003 range used directly by 0x1EDD is also the four-byte write window used by the page-0x80 helper. The repeated four-byte width is strong evidence of a shared external protocol, but not enough to name the receiving IC or decode the packet fields.

### 18.2 0x1BEF and 0x1BE2 — two distinct special branches

The bit-0x0A alternate edge from 0x1C2B reaches 0x1BEF. That routine reads four bytes through the descriptor pointer, advances and saves the pointer, then writes a four-byte record through R1=RAM[0x2E]:

`[0x38, R2, R3, R4]`

It then jumps to 0x19C2. This is a fixed-type record construction branch, distinct from the ordinary 0x1C2B test of descriptor byte +3 bit 3.

The bit-0x08 edge from 0x224C reaches 0x1BE2. That path advances RAM[0x36:0x37] by four and jumps to 0x19C2; it skips the four-byte descriptor item rather than dispatching it through the ordinary endpoint path.

### 18.3 0x19C2 is the common eight-byte record-stride loop

The common continuation at 0x19C2 performs:

1. RAM[0x2E] += 8;
2. RAM[0x2B] += 1;
3. DJNZ RAM[0x2C], 0x1986;
4. RET when the loop counter reaches zero.

This establishes an eight-byte stride in the indirect record/buffer area and a parallel incrementing index in RAM[0x2B]. The descriptor pointer RAM[0x36:0x37] is advanced separately by each handler (for example +1, +3, or +4); it is not automatically advanced by 0x19C2.

### 18.4 Updated hardware-interface boundary

Two code paths now independently emit four-byte writes to the same external XDATA window:

- 0x1EDD uses direct DPTR writes to 0x8000–0x8003, emitting two ordinary blocks and one conditional block.
- 0x0DB7 uses A0=0x80 plus R0=0..3 to write [R5, R2, R3, R4 & 0xBF].

The 0x224C and 0x2301 handlers prepare arguments and call 0x0DB7. This unifies the previously separate observations into one external four-byte transport window. The device identity, command/address interpretation, timing, and acknowledgement semantics remain open.


## 19. Decoding the 0x1EDD packet fields and loop context

### 19.1 Descriptor input fields at 0x1EDD entry

The handler reads five consecutive bytes from the descriptor pointer (after the three P3 strobes have been applied):

- first byte -> R2
- second byte -> R5
- third byte -> R6
- fourth byte -> F0
- fifth byte -> R4

The descriptor pointer is then saved back to RAM[0x36:0x37]. The fifth input byte is subsequently transformed together with F0; these incoming register assignments should not be confused with the final packet fields, because R3/R4/R5/R6/R7 are modified later.

### 19.2 Final field preparation before the writes

The instruction sequence around 0x2085–0x20C4 performs these operations:

1. Copies direct IRAM byte 0x04 into R2 (address 0x04 is register-bank-0 R4; this is an absolute direct address, not a claim about the currently selected register bank), then derives R4 from F0 using SWAP A followed by RR A. R3 is the high portion of this transformed value: R3 = R4 & 0xF8.
2. Reads the byte at XDATA address RAM[0x2E] through R1. If bit 2 of that byte is clear, it takes the alternate path at 0x210F. If bit 2 is set, R2 is transformed by SWAP/RL and mask 0x1F, and R4 is masked to its low three bits.
3. If RAM[0x3E] is nonzero, it becomes R5; R6 is derived from R3 using RLC and mask 0xFC; R7 is derived from R4 using RLC; R3 and R4 are then cleared. If RAM[0x3E] is zero, R5/R6/R7 are all cleared.
4. Internal bit 0x0D controls whether internal bit 0x0C is set and whether ACC.7 is set in the outgoing R4/R7 fields. ACC.7 here is a transient bit of the accumulator value being prepared, not a persistent flag.

The alternate branch at 0x210F instead sets R2=0, masks R4 to three bits, conditionally sets its ACC.7 bit based on internal bit 0x0D, selects A0=0x80, and calls 0x0DB7.

### 19.3 Exact three-packet sequence at 0x8000–0x8003

When internal bit 0x0A is set, the handler emits an additional packet:

- 0x8000 = RAM[0x2B] - 2
- 0x8001 = XDATA[RAM[0x2E] - 0x0F] from the currently selected A0 page
- 0x8002 = XDATA[RAM[0x2E] - 0x0E] from the currently selected A0 page
- 0x8003 = (XDATA[RAM[0x2E] - 0x0D] & 0x07), with ACC.7 conditionally set when internal bit 0x0C is set

The source offsets follow from: A=RAM[0x2E], CLR C, SUBB A,#0x10, INC A, then R0 is used for three consecutive MOVX reads. This is a read from the selected page, not from absolute XDATA page zero unless the caller selected that page.

The handler then always emits the second packet:

- 0x8000 = RAM[0x2B]
- 0x8001 = R5
- 0x8002 = R6
- 0x8003 = R7

And the third packet:

- 0x8000 = RAM[0x2B] - 1
- 0x8001 = R2
- 0x8002 = R3
- 0x8003 = R4

The second and third packets are written as direct DPTR MOVX stores to four consecutive addresses. This exact mapping supersedes the earlier abbreviated description that did not state the source offsets of the optional packet.

### 19.4 Caller initialization and iteration variables

At the visible setup path beginning at 0x1700, the firmware derives RAM[0x2B] from the high nibble of RAM[0x2D], clears RAM[0x2E], sets RAM[0x44]=0x70, and reads the initial descriptor-pointer bytes from the selected XDATA page at offsets 0x61 and 0x60 into RAM[0x36:0x37]. It then calls 0x189D, which performs the descriptor/mode dispatch.

Thus, in this entry path:

- RAM[0x2D] is the selected XDATA page/bank selector.
- RAM[0x2B] starts as (RAM[0x2D] & 0x0F) shifted into the high nibble, and increments once per eight-byte record.
- RAM[0x2E] is the output-record offset and starts at zero.
- RAM[0x36:0x37] is the descriptor-stream pointer loaded from page offsets 0x61/0x60.
- RAM[0x2C] is the remaining-record loop counter; its exact initializer is outside this 0x1700 setup slice and must be traced at the caller above it.

The 0x19C2 continuation advances RAM[0x2E] by eight and increments RAM[0x2B], then decrements RAM[0x2C]. The descriptor pointer is advanced separately by each handler according to the number of bytes consumed.

### 19.5 Remaining uncertainty

This gives a byte-accurate software-level packet layout and the immediate source fields. It still does not identify whether the external receiver interprets the first byte as a voice/channel/address, whether the remaining bytes are data/flags, or whether the write window is memory-mapped to a dedicated IC, FPGA/CPLD, or glue logic. That requires the board schematic/chip-pin trace or captured bus activity.


## 20. Descriptor-loop cardinality and sentinel handling

### 20.1 The pass is explicitly bounded to sixteen records

The instruction-aligned setup at 0x196B–0x197B establishes:

- A = RAM[0x2B] & 0x0F
- SWAP A; RR A; ORL A,#0x80
- RAM[0x2E] = 0x80 | ((RAM[0x2B] & 0x0F) << 3)
- RAM[0x2C] = 0x10
- RAM[0x36:0x37] = the current descriptor pointer from DPTR

The common continuation at 0x19C2 advances RAM[0x2E] by eight, increments RAM[0x2B], and executes DJNZ RAM[0x2C],0x1986. Therefore this pass has a hard count of sixteen iterations, with one eight-byte record stride per completed iteration. This is a firmware constant, not an estimate based on a table's apparent size.

The loop body begins at 0x1986 by restoring DPTR from RAM[0x36:0x37], then reads the next descriptor/control byte. The handler-specific path advances RAM[0x36:0x37] by the number of descriptor bytes it consumes; the common loop does not impose a fixed descriptor-byte stride.

### 20.2 End markers and special 0x38 records

At 0x199C the fetched byte is compared with 0xFF. The sentinel takes the separate 0x19E1 path rather than entering the 0x1BC3 dispatch table.

For non-0xFF values, the firmware masks A with 0x38 and compares it to 0x38:

- A masked value of 0x38 takes the special 0x19A7 record path.
- Other values are rotated right once and used as the computed offset into the LJMP table at 0x1BC3.

This makes 0xFF and the 0x38 class structurally distinct control values: 0xFF is an end/sentinel branch, while 0x38 is a valid special record class that is written into the record area and advances the eight-byte output slot.

### 20.3 Interpretation of the loop counter

RAM[0x2C] is initialized to exactly 16 at 0x1975 and decremented once at 0x19C2 after each completed record. The routine can also exit early through the 0xFF sentinel path. Consequently, the normal maximum is sixteen record slots, but a sentinel can terminate processing before all sixteen have been visited.

The current code establishes record count and stride, but does not yet prove that all sixteen slots correspond one-to-one with physical voices. That mapping still depends on the meaning of the group/index byte RAM[0x2B] and the receiver's interpretation of the emitted packets.


## 21. Selected page and the sixteen output-slot addresses

The caller-side path at 0x1614–0x1618 sets RAM[0x2D] = RAM[0x3E] & 0x1F. At 0x16C3 the firmware writes RAM[0x2D] to SFR A0, selecting the XDATA page used by subsequent MOVX @R0/@R1 accesses.

At 0x1700, the group/index initialization is exactly:

- A = RAM[0x2D]
- SWAP A
- clear internal bit 0x0D; set it if original A.0 (RAM[0x2D].0) was set
- ANL A,#0xF0
- RAM[0x2B] = A

Therefore RAM[0x2B] is not the high nibble of RAM[0x2D]. It is the low nibble of RAM[0x2D], shifted into the high nibble: RAM[0x2B] = (RAM[0x2D] & 0x0F) << 4.

The slot-base calculation at 0x196B–0x1973 then gives:

RAM[0x2E] = 0x80 | ((RAM[0x2B] & 0x0F) << 3)

Because RAM[0x2B] begins with a zero low nibble and increments once per record, the sixteen consecutive output record offsets are:

0x80, 0x88, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8,
0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8.

Each slot is eight bytes wide. This is an exact address-space result from the arithmetic and loop, not a guess about the semantic identity of the slots. The selected XDATA page is RAM[0x2D], so the effective record locations are page RAM[0x2D], offsets 0x80 through 0xF8.

This also clarifies why the four-byte packet's first byte changes as RAM[0x2B], RAM[0x2B]-1, or RAM[0x2B]-2: it is a group/index value derived from the selected page's low nibble and advanced with the slot iteration. Whether the external receiver calls this a voice number, channel, part, or address remains unproven.


## 22. 0x0DB7 is a shared packet-output primitive, not a descriptor-only routine

A fresh reachable-code cross-reference from the reset and interrupt entries found twelve direct call sites to 0x0DB7:

0x02C0, 0x0A4F, 0x0B53, 0x0D07, 0x0E1A, 0x0E67, 0x1045, 0x110A, 0x26C9, 0x26DE, 0x2767, and 0x277E.

This changes the interpretation of the helper: 0x0DB7 is a shared four-byte packet-output primitive used by multiple firmware subsystems. The descriptor handlers at 0x224C/0x2301 are only two of its callers. The exact four stores remain [R5, R2, R3, R4 & 0xBF], followed by the internal bit-0x0C update derived from ACC.7.

### 22.1 A confirmed sixteen-packet initialization loop at 0x02A9–0x02C6

This caller constructs a simple packet and emits it sixteen times:

- It derives R5 = (RAM[0x2D] & 0x0F) << 4.
- It chooses R4 = 0x00 or 0x80 according to bit 0 of RAM[0x2D].
- It clears R2 and R3.
- It sets R6 = 0x0F and selects A0=0x80.
- It calls 0x0DB7, increments R5, and executes DJNZ R6 back to the call.

Because DJNZ tests after decrement, the body executes sixteen times, with R5 taking the consecutive values base+0 through base+15. Each iteration writes [R5, 0, 0, R4] to XDATA 0x8000–0x8003. It restores A0 from RAM[0x2D] after the loop.

This independently corroborates a sixteen-entry indexed external command space. It also matches the same group-base arithmetic used by the descriptor pass, but it does not prove that the entries are sixteen audio voices; they may be channels, slots, endpoints, or another indexed hardware resource.

### 22.2 Endpoint-discovery caller at 0x1045

The endpoint-discovery path prepares:

- R5 = RAM[0x2B]
- R2 = 0x80
- R3 = 0x01
- R4 = 0x00, with ACC.7 conditionally set from internal bit 0x0D

It selects A0=0x80, calls 0x0DB7, then restores A0=RAM[0x2D]. This is a different packet class from the bulk zeroing loop and confirms that the helper's four fields are caller-defined arguments, not a fixed descriptor format.

### 22.3 Other callers show distinct packet sources

The remaining callers include:
- 0x0E67, which prepares a mostly zero packet and conditionally changes the high bit of the fourth field.
- 0x0E1A and 0x110A, which prepare control values after calls to 0x0DCE.
- 0x0A4F, 0x0B53, and 0x0D07, which assemble fields from XDATA records and state before calling the helper.
- 0x26C9/0x26DE and 0x2767/0x277E, which load packet fields from code-memory tables and emit pairs of packets; the latter pair complements the table bytes before transmission.

The call-site survey is limited to the 16,349 instruction-aligned instructions reachable by the current static disassembler from reset/interrupt roots. Computed-jump destinations and indirect control flow may reveal additional callers after full control-flow recovery.


## 23. Companion helper 0x0DCE exposes the packet handshake/phase mechanism

The helper at 0x0DCE does not simply send another packet. It compares ACC.7 of incoming R4 with internal bit address 0x0C:

- If R4.7=0 while bit 0x0C=1, it clears bit 0x0C and writes 0x40 to XDATA offset 3.
- If R4.7=1 while bit 0x0C=0, it sets bit 0x0C and writes 0xC0 to XDATA offset 3.
- If the two states already agree, it skips this control write.

After the optional offset-3 write, it reads XDATA offset 4, then reads offsets 0, 1, 2, and 3 into R5, R2, R3, and R4 respectively, and returns.

The companion sequence therefore uses the fourth packet byte's bit 7 as a phase/state selector. 0x0DB7 writes the packet with bit 6 cleared and mirrors bit 7 into internal bit 0x0C; 0x0DCE conditionally changes the offset-3 control value between 0x40 and 0xC0 when the requested phase differs, then reads the four packet bytes back.

This is substantially stronger evidence for a stateful external protocol than a bare four-byte memory write. It suggests a toggle/phase-controlled interface with a control strobe at offset 3 and a status read at offset 4. It still does not prove whether the receiving device is a sound generator, a register bridge, or another controller; nor does it establish that the offset-4 read is an acknowledgement without board/bus evidence.

The direct reachable call sites to 0x0DCE are 0x0E08, 0x10F0, 0x26FD, 0x271C, 0x279D, and 0x27BF. The exact complete caller set should be regenerated after the next CFG pass; this list is not treated as exhaustive.


## 24. The 0x26AA–0x27xx path is a 256-index, six-byte-per-index write/readback test

The code around 0x26AA provides the clearest explanation yet for the 0x8000 protocol. It selects A0=0x80, calls 0x0E21 to establish the control state, sets DPTR=0 (code-memory source), and initializes R5=0 as the external index.

### 24.1 Normal-pattern write pass

For each R5 value, the routine reads six consecutive bytes from code memory with MOVC and emits two packets through 0x0DB7:

- phase 0 packet: [R5, code[DPTR+0], code[DPTR+1], code[DPTR+2] & 0x07]
- phase 1 packet: [R5, code[DPTR+3], code[DPTR+4], 0x80 | (code[DPTR+5] & 0x07)]

DPTR advances by six bytes per R5 index. R5 is decremented after each pair, starting at 0x00 and wrapping through 0xFF, 0xFE, ... 0x01. DJNZ therefore visits all 256 possible index values exactly once. The source span is 256 * 6 = 1536 bytes (code addresses 0x0000–0x05FF).

### 24.2 Normal-pattern readback verification

The next pass resets DPTR=0 and reads the same two phases through 0x0DCE for each R5 index. Returned R2/R3/R4 values are compared against the corresponding three code-memory bytes; the third byte is compared only in its low three bits because the high bit is used as the phase selector.

Each mismatch branches to a JNZ instruction that jumps to itself, permanently trapping the firmware at that check. The routine therefore treats a readback mismatch as a fatal test failure, not as a retry/poll loop.

### 24.3 Complement-pattern write and readback

The following pass repeats the same 256-index / six-byte pattern with every source byte complemented before transmission. It then performs the matching readback comparison against the complemented code-memory bytes.

For the first six source bytes, the normal pattern is:
[02, EB, 32, FF, FF, FF]

The complement pattern is:
[FD, 14, CD, 00, 00, 00]

The low three bits of each third byte are the data portion; bit 7 distinguishes the two phases. The routine's repeated structure applies this to the full 1536-byte span, not just the first six bytes.

### 24.4 What this establishes

The firmware implements an indexed external data path with:

- 256 distinct index values in R5;
- six data bytes per index, transferred as two three-byte phases;
- phase selection carried in bit 7 of the third byte;
- a control/strobe interaction at XDATA offset 3 using 0x40/0xC0;
- readback of offsets 0–4 through 0x0DCE;
- full write/readback tests using both normal and complemented patterns.

This is strong evidence for a 256 x 6-byte external storage/register space or a hardware interface that emulates one. It is not yet proof that the space is audio-parameter RAM: the code is executing a diagnostic pattern test, and the actual receiver IC and its role still require board-level evidence.


## 25. The 256 x 6-byte test is called during system initialization

The direct call-site scan found one reachable call to the test routine at 0x26A2: address 0x374E executes LCALL 0x26A2.

The caller at 0x3700 is part of the system initialization sequence: it initializes MIDI/serial-related state, performs setup calls, conditionally updates interrupt-control state, calls 0x26A2, then continues with initialization routines at 0x00F8, 0xB56C, 0x5EF9, 0xB5A9, and 0x6433.

At 0x26A2 the routine:
- pushes the current interrupt-enable SFR (0xA8);
- writes zero to SFR 0xA8, disabling interrupts during the test;
- calls 0x0E3A to prepare the external interface;
- selects A0=0x80 and performs the normal-pattern write/readback pass;
- performs the complemented-pattern write/readback pass;
- restores the saved interrupt-enable SFR with POP 0xA8 and returns.

This context strongly classifies the 256 x 6-byte operation as a boot/system-initialization hardware test, rather than a normal per-note or per-voice update. It validates an external indexed memory/register interface before the rest of initialization proceeds. The exact physical device remains unidentified.


## 26. A second 0x8000 transaction path performs indexed read/modify/write

The path at 0x0B3F–0x0BCD branches on bit 2 of the current record-control byte XDATA[RAM[0x2E]]:

- If bit 2 is clear, it emits a normal four-byte packet through 0x0DB7 with R5=RAM[0x2B] and the caller-prepared R2/R3/R4 values.
- If bit 2 is set, it enters 0x0B5C and performs a direct read/modify/write transaction against the 0x8000 window.

The 0x0B5C path is:

1. Select A0=0x80.
2. Compare internal bit 0x0D (requested phase) with bit 0x0C (remembered phase). If they differ, update bit 0x0C and write 0x40 or 0xC0 to XDATA offset 3. It then reads offset 4; the returned value is not retained by this routine.
3. Write RAM[0x2B]-1 to XDATA offset 0, then read offsets 2 and 3.
4. Form the two's-complement of the existing offset-2 / low-three-bits-of-offset-3 value, add the caller's R3/R4 value, and constrain the result. This is an arithmetic read/modify step, not a blind packet copy.
5. Write RAM[0x2B] to offset 0, then write a newly encoded value to offset 1, transformed R3 to offset 2, and transformed R4 to offset 3. R4.7 is conditionally set from internal bit 0x0D; ACC.6 is cleared before the final write.
6. Restore A0=RAM[0x2D].

This establishes a second, stateful transaction style on the same external window: the firmware can select one index, read existing data, compute a delta, and write a transformed value under the next index. The use of RAM[0x2B]-1 for the read phase and RAM[0x2B] for the write phase is explicit in the instruction stream.

The relationship between this indexed delta update and the 256 x 6-byte boot diagnostic is now a key target. Both use the same XDATA offsets 0–4 and phase-control bits, but the semantic meaning of the indexed values (voice, Part, parameter, or another hardware resource) remains unassigned until this path is connected to its callers and the 0x7Cxx record fields.


## 27. The eight-byte slot records feed a three-byte update/interpolation path

The scanner path around 0x0849–0x0899 derives an eight-byte slot base in RAM[0x2E] from an index in RAM[0x2B]. It checks slot status fields and dispatches work according to flags in the record. One confirmed branch at 0x08A9 reads slot offset +4; if ACC.7 is set, it calls 0x0C59.

At entry to 0x0C59, R1 still points to slot offset +4. The routine then:

1. Reads the current slot's three-byte value from offsets +1, +2, and +3 into R5, R6, and R7 (R7 is masked to three bits).
2. Reads a second three-byte value from offsets +9, +10, and +11 into RAM[0x3A], RAM[0x3B], and RAM[0x3C]. These are the corresponding +1..+3 fields of the next eight-byte slot.
3. Subtracts the current triple from the next triple with borrow propagation, producing a three-byte delta.
4. Reads a scale/control byte at slot offset +12 and uses MUL AB operations to scale the delta. Internal bit 0x03 selects a sign/negation path.
5. Adds the scaled delta to the current triple, updates the slot's +1..+3 fields, and conditionally clears bit 7 of slot offset +4 based on a flag at offset +13.
6. Selects A0=0x80, sets R5=RAM[0x2B], prepares R2/R3/R4 from the updated triple (with the low three bits of R4 kept separate from the other flags), and calls 0x0DB7 to emit the indexed packet. It then restores A0=RAM[0x2D].

This is consistent with a fixed-point interpolation/slew operation between adjacent eight-byte records, but the exact semantic label of the scale byte and the record pair is not yet proven. The key confirmed relationship is that the packet emitted to the external interface is derived from the current slot's three-byte state, the next slot's three-byte state, and a scale/control byte at +12.

This gives a concrete path from the 16-slot record buffer to the external 0x8000 packet writer. The next step is to trace who populates slot offsets +1..+4 and +9..+13, then compare those writes with the 24-byte records at 0x7C15 + 0x18*index and with MIDI note/controller events.


## 28. Current neutral field map for the eight-byte slot array

The currently observed accesses support this provisional layout for each slot at base S = RAM[0x2E] + 8*n. These are access roles, not semantic parameter names:

| Offset | Confirmed use |
|---|---|
| +0 | Status/class byte; scanner tests masks 0x38 and 0x10 |
| +1 | First byte of the three-byte value sent to the external packet interface |
| +2 | Second byte of that value |
| +3 | Third byte; low three bits are data, while high bits participate in flags/phase handling |
| +4 | Control/flag byte; bit 7 gates the 0x0C59 update path |
| +5, +6 | Not yet assigned from the current trace |
| +7 | Compared with a value read from page 0x20 and passed through helper 0x1848 |
| +8 | Next slot base (because the stride is eight bytes) |
| +9..+11 | Next slot's +1..+3 triple, used as the second endpoint of the delta calculation |
| +12 | Scale/control byte used by the MUL AB path |
| +13 | Additional flag/compare byte used when deciding whether to clear bit 7 at current slot +4 |

The +9..+13 references are cross-slot reads: they belong to the next eight-byte slot, not to extra fields inside the current slot. This is why the 0x0C59 routine can compute a transition between adjacent slots.

The slot array is therefore a separate structure from the 24-byte records at 0x7C15 + 0x18*index. Their relationship is still unproven; do not merge these two layouts in the emulator data model yet.
