# Descriptor decoder at 0x1494 and 128-byte runtime slots

Status: instruction-by-instruction reconstruction from the reachable IC4 v1.05 disassembly. The routine consumes a 13-byte descriptor and constructs a pointer into a 16-slot, 128-byte-stride XDATA region. This is a firmware-side resource/transaction structure; its physical backing device and audio meaning remain unknown.

## 1. Descriptor source and entry contract

Callers prepare three direct-RAM values before calling `0x1494`:

- `RAM[0x28]`: descriptor mode/command selector. The routine retains its low nibble.
- `RAM[0x29]`: XDATA page selected into 8051 P2.
- `RAM[0x2A]`: byte offset of the descriptor within that page.

At `0x14BF`, firmware writes `RAM[0x29]` to SFR P2. It then reads the descriptor through `MOVX A,@R0`, beginning with `R0=RAM[0x2A]`.

## 2. Exact 13-byte descriptor layout

The code reads 13 bytes at offsets 0 through 12. Bytes 7, 8 and 9 are deliberately skipped by incrementing R0 three times without reading them.

| Descriptor offset | Firmware operation | Destination / interpretation |
|---:|---|---|
| 0 | Read, AND `0x7F` | `RAM[0x2F]` |
| 1 | Read | `RAM[0x30]` |
| 2 | Read, lookup via code table `0x1208` | `RAM[0x31]` |
| 3 | Read | `RAM[0x33]` |
| 4 | Read | `RAM[0x34]` |
| 5 | Read | `RAM[0x37]` |
| 6 | Read | `RAM[0x36]` |
| 7–9 | Skipped | Not consumed by this routine |
| 10 | Read; bits 0–2 copied from CPU flags E0–E2 | Internal flags 0, 1, 2 |
| 11 | Read, lookup via code table `0x1208` | `RAM[0x32]` |
| 12 | Read | `RAM[0x35]` |

The table lookups are actual `MOVC A,@A+DPTR` reads from ROM address `0x1208`, not guesses based on parameter ranges.

## 3. Mode-indexed 128-byte slots

The routine computes a base pointer from the low nibble of `RAM[0x28]`:

```
slot_base = 0x2068 + 0x80 * (RAM[0x28] & 0x0F)
```

The result is stored in the 16-bit pointer `RAM[0x3C:0x3D]`. Thus the arithmetic defines 16 possible slots with a 128-byte stride, covering:

- mode 0: `0x2068–0x20E7`
- mode 1: `0x20E8–0x2167`
- ...
- mode 15: `0x27E8–0x2867`

When internal flag 0x06 is clear, the routine adds the descriptor's first decoded byte (`RAM[0x2F]`) to the base pointer. When flag 0x06 is set, it keeps the slot base unoffset. Other branches inspect/update slot state and may set or clear the busy/valid flags B5, B4 and B1.

This is an indexed record/slot mechanism. The existence of a 128-byte stride does not by itself prove that the slots are voices, waveforms, or sample buffers.

## 4. How the descriptor reaches the selected-page mailbox

After parsing and slot selection, the routine stores the selected page in `RAM[0x2D]` and eventually reaches `0x16C3` (directly or via the shared `0x1491` jump path).

At `0x16C3`, firmware:
- restores P2 from `RAM[0x2D]`;
- uses offset `0xFB` for the status/handshake byte;
- writes the high-nibble mode/command value derived from `RAM[0x28]` to `0xFC`;
- writes `RAM[0x3C]` and `RAM[0x3D]` to offsets `0xFE` and `0xFF`;
- increments the internal pointer `RAM[0x36:0x37]` by two;
- calls `0x189D` to stage the selected record and its metadata;
- polls/updates status at offset `0xFD`.

This is a second, page-selected transaction protocol. Keep it distinct from the fixed P2=`0x80` four-register port at `0x8000–0x8003`.

## 5. Confirmed versus unknown

Confirmed from code:
- `0x1494` reads a 13-byte descriptor from XDATA page/offset supplied in `RAM[0x29:0x2A]`.
- Descriptor bytes 7–9 are skipped.
- Two descriptor bytes are translated through ROM table `0x1208`.
- A mode nibble selects a 128-byte-stride slot beginning at `0x2068).
- The resulting pointer and selected page feed the `0x16C3` status/pointer mailbox and `0x189D` record staging.

Not confirmed:
- Whether page `0x20` and the selected `0xFB–0xFF` window are ordinary RAM, dual-port RAM, or mapped registers of a coprocessor.
- Whether the 16 slots are sound voices, parameter/resource descriptors, DMA buffers, or another structure.
- Whether this transaction starts, updates, or merely synchronizes audio playback.
- The identity of the hardware behind the 80-pin QFP.

## 6. Next analysis

1. Trace writers/readers of all 128-byte slots at `0x2068 + 0x80*n`.
2. Compare the descriptor's 13-byte inputs at each `0x1494` call site, especially the Note On chain from `0x4256`.
3. Correlate `RAM[0x28]` modes and slot offsets with the four event IDs 12–15.
4. Trace the values returned by `0x189D` and the status byte at `0xFD`.
5. Only after the slot structure is identified should the host-side firmware transaction model be promoted from protocol reconstruction to an audio command interface.
