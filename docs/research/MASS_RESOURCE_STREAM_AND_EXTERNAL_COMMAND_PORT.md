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

## 2. Firmware lookup tables used by command handlers

The descrambled IC4 image contains compact lookup tables reached by MOVC:

| ROM address | Entries | Call sites | Observed use |
|---|---:|---|---|
| 0x11E0 | 8 | 0x23DF | First modulation-scale table in helper 0x23C7 |
| 0x11E8 | 8 | 0x23EF | Second modulation-scale table in helper 0x23C7 |
| 0x11F0 | 12 | 0x1CD4 | First 12-entry interpolation table in the 0x1C61 path |
| 0x11FC | 12 | 0x1D27 | Alternate interpolation table selected when the current accumulator byte has ACC.7 set |
| 0x1208 | 128 | 0x0D0E, 0x14CF | 7-bit selection/parameter remapping |
| 0x1288 | 128 | 0x1F67 | Nonlinear scale curve in the common 0x1EDD path |
| 0x1308 | 256 | 0x1F81 | Second-stage byte quantization map |

The first nonlinear lookup in 0x1EDD is now reconstructed instruction-by-instruction. R4=0 diverts to the alternate path and skips 0x1288. Otherwise B is set to RL(R4), while A is RL(RAM[0x30]) unless opcode.0 is clear and opcode.6 is set, in which case the source is complemented first. If B.7 is clear, A is complemented and MUL AB is executed; the 7-bit index is then (~productHighByte) >> 1. If B.7 is set, the unsigned A*B path is used; a product high byte with bit7 set saturates the index to 0x7F, otherwise the index is the high byte itself. That index reads 0x1288 at 0x1F6C. The post-lookup gate at 0x1F6F..0x1F7A permits 0x1308 only when RAM[0x3F].7 is clear, RAM[0x40].4 is set and opcode.2 is set. On that branch, R4 receives the 0x1308 result and B receives R3. Otherwise R4 is set to 0xFF; if opcode.0 is clear and opcode.6 is set, B becomes R3 + highByte((~R3)*curve), otherwise B becomes highByte(R3*curve). The common 0x1F9F guard changes B from zero to one when R3 is nonzero. MASS1EDDPostLookup models these paths separately from the 0x1288 index builder. The assembled MASS1EDDParameterPipeline now connects the first lookup, quantizer/fallback result, optional 0x2381 address, and the 0x1848 multiply helper. When R7 is nonzero it reports the external address and accepts the byte read there as input; it does not claim to emulate the P3-selected physical memory bus. The following 0x1FCB..0x2005 stage is also modeled: opcode.1 enables two successive high-byte MUL scales using RL(RAM[0x31]) and RAM[0x32], with the firmware's nonzero fallback to coefficient 1; then nonzero RAM[0x41]/RAM[0x43] calls 0x23C7. Only its verified opcode-mask 0x41 path is implemented; alternate 0x1890 branches return unsupported rather than fabricated data. E0/E2/E4/E6/E7 are data-dependent ACC bit tests; F7 tests B.7, not persistent mode flags.

Related helpers:
- 0x2381 receives an already-prepared selector in A and constructs DPTR as RAM[0x39:0x38] + 0x30 * (A & 3) + offset. Its callers prepare A in at least two ways: at 0x1FB9..0x1FBF, SWAP A (originally R5) followed by ANL A,#3; at 0x1B58..0x1B61, MOVX A followed by ANL A,#3. The helper itself does not inspect R5. It adds 0xF4 to RAM[0x2F]; if the result has bit 7 set, offset is zero. Otherwise it shifts right through carry after CLR C, then keeps that half-value if adding 0xD0 produces bit 7, or clamps the offset to 0x2F. It then reads through the P3-selected external bus. MASS2381AddressBuilder accepts the prepared A selector and models this address arithmetic only.
- 0x1848 performs sign-aware rotate/multiply arithmetic using A, B/F0 and R2, with saturation returns 0x7F or 0x81 when the D2 condition is active. Its complete fixed-point interpretation remains under analysis.
- 0x23C7 applies data-dependent modulation scaling. When the slot opcode masked by 0x41 equals 0x41, it initializes B to 0xFF, conditionally loads the 0x11E0 coefficient from RAM[0x41] bits 4..6 when RAM[0x41] is nonzero and bit 7 is set, then conditionally multiplies by the 0x11E8 coefficient selected from RAM[0x43] the same way. It finishes with MUL AB using R2 and the current B high-byte coefficient, storing the result's high byte back in R2. The alternate opcode masks take separate paths through 0x1890 and are not yet implemented.

CI checks table bounds and endpoint values against the real 128-KB descrambled IC4 image, verifies monotonicity of the 0x1208, 0x1288 and 0x1308 remapping curves, and evaluates all 128 composed 0x1288 -> 0x1308 lookups.

## 3. Fixed-point helper 0x1848

Subroutine 0x1848 is represented at instruction level in MASS1848Arithmetic. Its entry arguments are A, B/F0 and R2. Crucially, addresses 0xE0..0xE7 are bits of the current accumulator A, so E7 is re-tested after each XCH rather than treated as a persistent mode flag. Address 0x03 is a temporary bit in bit-addressable internal RAM. Address 0xD2 is PSW.2/OV, set by the final ADD A,R2; it is not an independent input flag. The routine:

1. Tests ACC.7; if set, records temporary bit 0x03 and two's-complement negates A.
2. Exchanges A/B and tests the new ACC.7 independently. If set, toggles bit 0x03 and negates A again.
3. Rotates A left, forces bit 0 to one, multiplies A*B, exchanges A/B to retain the high product byte in A, and conditionally negates it according to temporary bit 0x03.
4. Loads R2 into B/F0, repeats the fresh ACC.7 tests and sign corrections around the second exchange, performs the second rotate/force-bit/multiply/high-byte extraction, then adds the original R2.
5. The final ADD sets PSW.2/OV and carry. If OV is clear, the raw low byte is returned. If OV is set, the result saturates to 0x7F when carry is clear or 0x81 when carry is set.

The E7 branch changes the operands before both multiplications; it is not a final sign-bit toggle. The C++ model retains the byte-level operations and carries rather than substituting a floating-point approximation. Tests cover E7 on/off, positive and carry-producing sums, and both saturation endpoints.

The 0x1C61 handler can perform a 24-bit interpolation. XDATA 0x2067 supplies the signed interpolation control byte; zero bypasses the operation. Its bit7 selects the sign branch: the handler uses the byte directly when nonnegative or computes its 8-bit two's-complement negation when negative, divides that magnitude by 12, and uses the quotient as the number of later shifts while the remainder selects a coefficient from 0x11F0 (nonnegative) or 0x11FC (negative). Separately, R4 has already been masked to three bits and remains the high byte of the 24-bit operand R4:R3:R2. MASS1C61InterpolationLookup now models that division/table selection exactly. MASS1C61InterpolationArithmetic models the following 24-bit multiply by the table coefficient (keeping the product shifted right eight bits), then repeats the firmware's RLC sequence for quotient+1 on the positive branch or RRC for quotient on the negative branch. Carry is cleared only before the first byte of the first iteration and then propagates across bytes and subsequent iterations exactly as in the 8051 loop. Independent positive and negative test vectors cover the complete 24-bit result.

## 4. Command-stream interpreter

`0x189D` uses the descriptor pointer and the per-mode metadata table at `0x2868 + 0x80*mode`. The command interpreter is reached through the indirect dispatch at `0x19BD`.

### Stream control bytes

- `0xFF` is the stream terminator; the interpreter exits before dispatching it.
- A byte whose masked group is `0x38` is consumed as a slot marker before the jump table: the firmware writes a `0x38` filler record, advances the stream pointer by one, and completes the current eight-byte output slot.
- Other bytes are dispatched using `(byte & 0x38) >> 1` into the table at `0x1BC3`.

The dispatch table contains eight four-byte entries (an `LJMP` plus padding). The handler table includes the `0x38` entry, but the interpreter's earlier marker check means that entry is not reached for ordinary stream parsing:

| Masked group | Handler | Observed byte consumption and behavior |
|---|---:|---|
| `0x00` | `0x1C2B` | Conditional 4- or 7-byte path. If firmware flag bit `0x0A` is set, it consumes 4 bytes and writes a synthetic `0x38` slot. Otherwise byte +3 bit 3 clear consumes 4 bytes; set branches into `0x224C` at byte +3, where bytes +3..+6 become the nested opcode and three payload bytes. The final packet is `[RAM[0x2B], byte[+4], byte[+5], byte[+6] & 7]` unless the nested flag `0x08` skips the handler. |
| `0x08` | `0x1C61` | 7-byte record. Input maps to `F0,R2,R3,R4,R7,R6,R5` in byte order. The handler writes raw fields to slot offsets 0,3,4,5,6; the merge decision tests ACC.3 of the current R4 byte (not a separate E3 mode flag). It then performs fixed-point/pitch-related arithmetic and may interpolate a 24-bit quantity. |
| `0x10` | `0x1EDD` | 7-byte record. Input maps to `R2,R3,R5,R6,F0,R4,R7`. When byte +1 (`R3`) is zero, the handler restores external controls, writes a `0x38` filler slot and advances exactly 7 bytes without reading the remaining fields semantically. Otherwise it performs mode/flag-dependent nonlinear arithmetic and can emit up to three hardware packets. |
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
- `0x1EDD` reaches a packet phase after its register/parameter transform. It always emits command `RAM[0x2B]` with `[R5,R6,R7]` and command `RAM[0x2B]-1` with `[R2,R3,R4]`. When firmware flag `0x0A` is set, it additionally emits command `RAM[0x2B]-2` with the three bytes staged at the current slot. The C++ packet builder accepts post-transform registers and the staged bytes separately; it does not pretend to reproduce the nonlinear transform itself.

### Handshake

`0x0DCE` compares internal bit `0x0C` with ACC.7 of the current R4 byte. On a state change it updates bit `0x0C` and writes either `0x40` or `0xC0` to port offset 3 (`0x8003`), then reads offset 2 (`0x8002`). ACC.7 is a data bit from R4, not a global flag. This is a toggle/acknowledgement pattern; no busy-wait loop proving a conventional ready flag was found.

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
