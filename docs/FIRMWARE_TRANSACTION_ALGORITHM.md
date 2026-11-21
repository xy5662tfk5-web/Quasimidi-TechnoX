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
5. The selected status byte is masked with 0x1F and used as the dynamic XDATA page (RAM[0x2D] and A0). The firmware then polls offset 0xFB and checks flags E3, E5, E6, E7, and E4. These checks control retries, writes to offset 0xFD, and progression into the lower-level slot descriptor processing beginning at 0x0F33.

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

- 0x1780 accepts the current candidate only when E3 is set and neither E5 nor E6 is set. It then calls 0x1009, delays, calls 0x018D, and returns zero. If the candidate is not ready, it follows the page link at 0x7E + page_id and retries; if it reaches RAM[0x51], it transitions to the alternate candidate path at 0x17AC.
- 0x17AC performs a related check without the E3 requirement: it requires E5 and E6 both clear before calling 0x1009, delaying, calling 0x018D, and returning zero. Otherwise it follows the linked page list; reaching RAM[0x51] returns without acquiring a page.
- 0x17D4 is a third retry path. It selects the same candidate chain and checks E3/E5/E6. If E3 is clear, or E5/E6 are both clear, it calls 0x1009 and returns zero. Otherwise it follows the page links and retries. This routine is invoked by the nonzero descriptor path when RAM[0x52] == 0.

The delay is a short software loop (R0=0, NOP, DJNZ); no time unit is inferred.

### 0x1009: discover a usable endpoint on a page

0x1009 selects page RAM[0x2D], derives a high-nibble group from the page ID, and scans offsets 0x80, 0x88, ... 0xF8.

A candidate byte is accepted when (value & 0x7E) == 0x16. The routine then switches to XDATA page 0x80, prepares arguments (R5 = group, R4 = 0, R3 = 1, R2 = 0x80), calls 0x0DB7, restores the original page, and continues scanning.

This is endpoint discovery/initialization on a selected page. The signature is observed in firmware; the endpoint's hardware identity is not established.

### 0x018D and 0x0211: validate and initialize selected page

0x018D rejects any selected page with upper three bits set (page & 0xE0 != 0) by jumping to address zero. For a valid page it selects that XDATA page and calls 0x0211, then updates the linked page queue and increments RAM[0x52].

0x0211 reads status offset 0xFB; if E5 is set it raises internal flag bit 0x0A. It calls 0x0DF7 with the selected page, clears offset 0xFB, writes 0x40 to offset 0xFA, then scans endpoint bytes beginning at offset 0x30. It initializes endpoint fields and checks associated flags. This is page/endpoint initialization, not proof of a synth voice register protocol.

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
