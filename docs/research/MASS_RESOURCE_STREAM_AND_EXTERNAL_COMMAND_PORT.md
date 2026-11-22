# M.A.S.S. Resource-Stream Interpreter and External Command Interface

## Scope

This note records firmware behavior established from the descrambled IC4 v1.05 image and the targeted disassembly of indirect command handlers. It identifies the resource-pointer path, command-stream bytecode, pitch-related arithmetic, and the external four-byte command port.

It does **not** identify the external sound-device IC, decode the full resource memory, or reconstruct the M.A.S.S. sample/oscillator algorithm. The addresses below are firmware/XDATA addresses, not asserted physical chip pins unless stated.

## 1. Resource pointer construction

### Normal Part path: `0x418E`

For the normal branch (Part index other than the special index 9), `0x418E` receives:

- `R7`: runtime Part index
- `R5`: Part parameter, saved in `RAM[0x6F]` and clamped to `0..3`
- `R3`: resource sub-index, saved in `RAM[0x70]`

The routine performs two related lookups:

1. It reads `0x5C3D + RAM[0x6F]` from code ROM. The first four bytes are `07 07 06 06`. This P3 control mask is written to byte `+9` of the 12-byte per-Part resource-selection record at `0x2900 + 12*PartIndex` (address `0x2909 + 12*PartIndex`).
2. It forms a 256-entry pointer-table index:
   `index = RAM[0x70] + 0x80 * (RAM[0x6F] & 1)`.
   The code extracts parameter bit 0 into bit 7, then adds `RAM[0x70]`.

### Bank selection and pointer read: `0xB3BC`

`0xB3BC` receives the pointer-table index in `R7` and the bank mask in `R5`:

- `R5` is loaded from the `0x5C3D` mode-mask lookup. The same mask is stored in selection-record byte `+9` and reused by the descriptor/stream path.
- `B385` tests bits 0, 1, and 2 of A (=R5), clearing P3.5, P3.4, and P3.1 respectively. These are active-low external control lines from the DS5002FP.
- With those lines asserted, `B3BC` reads two bytes from XDATA address `2*index` and `2*index+1`.
- The first table byte returns in `R6` (pointer high byte), the second in `R7` (pointer low byte).
- `B395` restores P3.5, P3.4, and P3.1 high after the read.

Thus the normal path uses a 256-entry, two-byte pointer table in a bank-selected XDATA window. The mask values for Part parameters 0..3 are `07, 07, 06, 06`. The special Part-index-9 branch follows a different path and is not covered by this formula.

### Descriptor hand-off

`0x418E` writes the returned pointer to descriptor bytes `+5/+6`:

- `+5`: pointer high byte (`R6`)
- `+6`: pointer low byte (`R7`)

`0x1494` reads a 12-byte resource-selection record from XDATA. It sets P2 from `RAM[0x29]`, R0 from `RAM[0x2A]`, and reads the record through `MOVX @R0`. The record is the per-Part block at `0x2900 + 12*PartIndex`; the runtime pointer to it is staged at `0x2B98`. Bytes `+5/+6` carry the external descriptor pointer: byte `+5` becomes DPH (`RAM[0x37]`), byte `+6` becomes DPL (`RAM[0x36]`). At `0x15AE`, descriptor scanning begins at this pointer plus eight bytes. Byte `+9` contains the same P3 mask loaded from `0x5C3D`; firmware copies its bits 0..2 into internal flags 0..2, controlling active-low P3 lines during subsequent external-memory reads.

This establishes the chain:

`Part parameter -> 0x5C3D P3 mask + IC51 pointer-table index -> 16-bit descriptor address -> per-Part 12-byte XDATA selection record -> 0x1494 / 0x15AE descriptor scan -> command-stream pointer -> 0x189D`.

## 2. Command-stream interpreter

`0x189D` uses the descriptor pointer and the per-mode metadata table at `0x2868 + 0x80*mode`. The command interpreter is reached through the indirect dispatch at `0x19BD`.

### Stream control bytes

- `0xFF` is the stream terminator; the interpreter exits before dispatching it.
- A byte whose masked group is `0x38` is consumed as a slot marker before the jump table: the firmware writes a `0x38` filler record, advances the stream pointer by one, and completes the current eight-byte output slot.
- Other bytes are dispatched using `(byte & 0x38) >> 1` into the table at `0x1BC3`.

The dispatch table contains eight four-byte entries (an `LJMP` plus padding). The handler table includes the `0x38` entry, but the interpreter's earlier marker check means that entry is not reached for ordinary stream parsing:

| Masked group | Handler | Observed byte consumption and behavior |
|---|---:|---|
| `0x00` | `0x1C2B` | Conditional 4- or 7-byte path. If firmware flag bit `0x0A` is set, it consumes 4 bytes and writes a synthetic `0x38` slot. Otherwise byte +3 bit 3 clear consumes 4 bytes; set branches into `0x224C` at byte +3 and consumes 7 bytes total. |
| `0x08` | `0x1C61` | 7-byte record: opcode plus six payload bytes; stores/scales fields and may interpolate a 24-bit value. |
| `0x10` | `0x1EDD` | 7-byte record: opcode plus six payload bytes. If byte +1 is zero, it takes a filler-slot path but still advances exactly 7 bytes. Otherwise it performs the active 24-bit/nonlinear parameter transform. |
| `0x18` | `0x224C` | 4-byte record: opcode plus three payload bytes. When firmware flag bit `0x08` is set it skips the command but still consumes 4 bytes; otherwise it writes the four-byte command port. |
| `0x20` | `0x2301` | 4-byte terminal control record: opcode plus three payload bytes. With flag `0x0A` clear it fills all unoccupied eight-byte slots through offset `0xF0` with `0x38`, sets the current slot to `0xF8`, and stores the payload in the final slot. With flag `0x0A` set it skips the filler loop and jumps directly to slot `0xF8`. It emits a control packet and exits via `0x19CD`; following bytes are not parsed. |
| `0x28` / `0x30` | `0x2374` | One-byte advance for each group; the output slot is then advanced by the interpreter. |

The interpreter initializes a 16-iteration counter (`RAM[0x2C] = 0x10`). The caller `0x16C3` sets P2 to `RAM[0x2D]` before calling `0x189D`; therefore `RAM[0x2E]` is the low-byte offset within that selected XDATA page, not a page number:

`RAM[0x2E] = 0x80 + 8 * (RAM[0x2B] & 0x0F)`

After each slot, it increments `RAM[0x2E]` by 8 and `RAM[0x2B]` by 1. This fills 16 eight-byte records at offsets `0x80, 0x88, ... 0xF8` within one XDATA page selected by `RAM[0x2D]`. The companion one-byte slot identifiers/status values at offsets `0x70..0x7F` are scanned by `0x0D0E`. The page value is derived from `RAM[0x3E] & 0x1F`; startup code initializes the 32 pages `0x00..0x1F` using the page map at `0x0078`. The exact meaning of each page (Part, voice, or resource workspace) remains unresolved.

## 3. Pitch and parameter arithmetic

Handler `0x1C61` uses the 12-entry table at `0x11F0`:

`80 88 90 98 A1 AB B5 C0 CB D7 E4 F2`

These are Q7-like approximations of the equal-tempered semitone ratios. The reverse table at `0x11FC` is:

`FF F2 E4 D7 CB C0 B5 AB A1 98 90 88`

The handler divides a pitch-related quantity by 12, selects the semitone coefficient, and uses 24-bit arithmetic to scale the parameter. It then updates the per-window state and calls `0x09B9`, which interpolates the 24-bit value and sends an update through the four-byte port.

Handler `0x1EDD` also performs 24-bit arithmetic and uses nonlinear lookup tables at `0x1288` and `0x1308`. Their exact parameter semantics are not yet identified.

## 4. External four-byte command port

### Packet format

`0x0DB7` writes four bytes to the current P2-selected XDATA page at offsets 0..3 in this order:

`[R5, R2, R3, R4]`

When the caller is sending a hardware command, it temporarily selects P2 page `0x80`, so the port is `0x8000..0x8003`. Helper `0x09B9` does this for an interpolated update and restores P2 to `RAM[0x2D]` afterward. The page-`0x80` hardware port is therefore separate from the 16-record buffer in the selected `RAM[0x2D]` page. The first byte is the command; the remaining three are its payload.

The stream handlers use this port in several ways:

- `0x1C61` updates an interpolated 24-bit parameter through `0x09B9`.
- `0x224C` reads command byte + payload bytes into `F0,R2,R3,R4` (where `R4 = payload[2] & 0x07`). When the early flag `0x08` branch is clear, its final hardware packet is `[RAM[0x2B], payload[0], payload[1], payload[2] & 0x07]`; it may also initialize the page-0 hardware command area for selected `RAM[0x2B]` low-nibble values.
- `0x2301` writes the three stream payload bytes to the final slot at `0xF9..0xFB`. For the hardware packet it sets `R5 = RAM[0x2B] | 0x0F`, `R2 = direct[0x07]`, `R3 = direct[0x02]`, and `R4 = payload[3] & 0x07`; `0x0DB7` therefore writes `[RAM[0x2B] | 0x0F, direct[0x07], direct[0x02], payload[3] & 0x07]` to `0x8000..0x8003`. It then exits the stream interpreter via `0x19CD`; bytes after the four-byte record are not parsed.
- `0x1EDD` can emit a sequence of up to three packets: command `RAM[0x2B]-2` with buffered data, command `RAM[0x2B]` with `[R5,R6,R7]`, and command `RAM[0x2B]-1` with `[R2,R3,R4]`.

### Handshake

`0x0DCE` synchronizes internal flag `0x0C` with `E7`. On a state change it writes either `0x40` or `0xC0` to port offset 3 (`0x8003)), then reads offset 2 (`0x8002`). This is a toggle/acknowledgement pattern; no busy-wait loop proving a conventional ready flag was found.

The MAME skeleton currently leaves `0x8000..0x8003` as no-op, and the physical target of this port remains unidentified. The firmware behavior strongly establishes a command interface, but not the chip model.

## 5. Note-state path connected to the command engine

- `0xAC00` computes a per-Part note-map byte address from Part index and note number, sets the note bit in XDATA page `0x35`, and clears the same bit in page `0x36`.
- `0xAC88` uses the per-Part byte at `0x2FA2 + PartIndex` to select a bit operation on the same page pair.
- `0xAD8C` scans the 16-byte page-`0x36` map for set bits, clears them, reconstructs note numbers, and calls `0x4256` with velocity zero (Note Off).
- `0x3ECE` initializes `0x2FA2 + index` to zero. The only explicit writer found so far is this zeroing path; a nonzero writer, if any, remains unresolved.

This is a real firmware note-state/release mechanism, but the selector's complete lifecycle is not yet known.

## 6. What this proves—and what it does not

**Established:** TechnoX firmware resolves per-Part resource pointers through bank-selected external XDATA, interprets a structured command stream, performs pitch-related fixed-point arithmetic, and sends three-byte parameter payloads through a four-byte external command port.

**Not established:** the identity of the receiving IC, the meaning of every command ID, the contents/codec of the external resource memory, or the original M.A.S.S. oscillator/sample playback algorithm. The current plug-in still uses its generic test synthesis backend; it must not be presented as original TechnoX sound.

## 7. IC51 pointer directory, external descriptors and command streams

MAME's `src/mame/skeleton/qmquasar.cpp` declares IC51 as the sample ROM region. The TechnoX v1.05 image is `0x20000` bytes (128 KB), CRC32 `d669570e`, SHA1 `0645a1b022ed25a836152933e6df32da4448f49a`.

The image divides into two 64KB banks:

- bank 0: file offsets `0x00000..0x0FFFF`
- bank 1: file offsets `0x10000..0x1FFFF`

The first `0x200` bytes of each bank form a 256-entry pointer table. The entries are **big-endian 16-bit descriptor addresses**. The byte order is established by the complete firmware path:

1. `0xB3BC` reads the first table byte into R6 and the second into R7.
2. `0x418E` stores R6/R7 at descriptor bytes `+5/+6`.
3. `0x1494` loads byte `+5` into `RAM[0x37]` and byte `+6` into `RAM[0x36]`.
4. At `0x15AE`, the firmware places `RAM[0x37]` in DPH and `RAM[0x36] + 8` in DPL. Thus the pointer-table value is big-endian and the descriptor scan begins at descriptor address `+8`.

Combining the pointer-table lookup with the normal `0x418E` path gives four 128-entry resource groups:

| Part parameter | P3 mask | Pointer-table bank | Pointer index |
|---:|---:|---:|---|
| 0 | `0x07` | 0 | `R3 + 0x00` |
| 1 | `0x07` | 0 | `R3 + 0x80` |
| 2 | `0x06` | 1 | `R3 + 0x00` |
| 3 | `0x06` | 1 | `R3 + 0x80` |

The two P3 masks differ only in bit 0, which controls P3.5. Since IC51 is a 128KB EPROM, P3.5 is a strong candidate for its A16 bank-select line; P3.4 and P3.1 remain asserted low in both masks and are likely enable/control lines. This pin-to-EPROM mapping is a hardware hypothesis until confirmed by PCB continuity or a schematic.

### External resource descriptor format

The pointer-table entry points to an eight-byte descriptor header, followed by descriptor tables; it does not point directly to the command stream. `0x1494` reads a separate 12-byte Part selection record from XDATA `0x2900 + 12*PartIndex` (P2 page plus R0 offset), obtains the descriptor address from bytes +5/+6, and `0x15AE` scans the descriptor beginning at `descriptorAddress + 8`:

- The first eight descriptor bytes are retained as a fixed header; their full semantics are not yet mapped.
- Table A contains group IDs `0..4`. Each group contains records of `selector + 2 data bytes`; selector `0x7F` ends the current group. A group ID `>= 5` ends Table A.
- Table B contains records of `key + 2 data bytes` for keys `0..5`. A key `>= 6` ends Table B.
- The next two bytes form the command-stream pointer in **little-endian** order. The code reads the first into `RAM[0x36]`, the second into `RAM[0x37]`, then loads DPL from `RAM[0x36]` and DPH from `RAM[0x37]`.

The same P3 mask from `0x5C3D` is stored at byte +9 of the XDATA selection record and reused for pointer-table, descriptor and command-stream reads. Scanning the actual IC51 image confirms that all 512 pointer targets parse as descriptors in their corresponding 64-KB pointer bank, and all 512 resulting stream pointers decode in that same bank without truncation. The exact external bus/device implementation remains a hardware-level question, but the firmware-selected IC51 memory window is now supported by the ROM-wide structural scan.

### XDATA staging buffer and hardware port

`0x16C3` sets P2 to `RAM[0x2D]` before calling `0x189D`. The stream interpreter writes sixteen eight-byte records into offsets `0x80..0xF8` of that one selected XDATA page. The companion slot identifiers/status values at offsets `0x70..0x7F` are scanned by `0x0D0E`. Startup initializes the 32 pages `0x00..0x1F` using the page map at `0x0078`; the exact meaning of each page remains open.

The four-byte hardware port at `0x8000..0x8003` is separate. The firmware temporarily selects P2 page `0x80` for hardware writes, then restores P2 to `RAM[0x2D]`. This distinguishes the decoded per-page staging records from the external sound-processor command interface.

IC51 is therefore a structured resource directory, not a flat PCM stream. The C++ components now include `MASSResourceROM` for the big-endian IC51 directory, `MASSResourceSelectionRecordParser` for the 12-byte P2:R0 XDATA selection record (including R0 wraparound), `MASSResourceDescriptorParser` for descriptors stored in each selected 64-KB IC51 bank, and `MASSCommandStreamDecoder` for streams in the same bank. The regression test now parses all 512 real descriptors and decodes all 512 streams in both flag-0A scenarios; end-state counts are asserted. None of these components claims to decode PCM or reproduce the original M.A.S.S. synthesis.

## Next reverse-engineering target

Identify the memory bank selected by descriptor byte `+10`, locate the external descriptor/command-stream backing memory, and correlate the extracted 24-bit values and hardware command IDs with sample address, pitch, envelope, and modulation behavior.
