# Startup external XDATA memory test at 0xB34D

Status: exact firmware loop reconstructed from IC4 v1.05. The startup routine performs a write/read test over the 16 KiB XDATA address range `0xC000–0xFFFF`. This establishes that the range is expected to behave as readable and writable storage during startup; it does not identify the physical memory chip or prove that the region is sample RAM.

## 1. Startup call site

The routine is called at `0x378C`, during startup, after the fixed `0x8000–0x8003` interface initialization/test at `0x26A2` and before later subsystem initialization continues.

The caller clears an internal status flag before the call and sets it again after the routine returns. A failure therefore prevents normal startup from progressing.

## 2. Exact test loop

Routine `0xB34D`:

1. Clears internal flag B2.
2. Sets SFR `0x8E` to `0x02`.
3. Loads DPTR with `0xC000`.
4. Loads accumulator with `0xAA`.
5. Writes `0xAA` to `MOVX @DPTR`.
6. Reads the same address back.
7. If the byte is not `0xAA`, loops at the read/compare instruction until it matches.
8. Increments DPTR.
9. Tests `DPH | DPL`; if nonzero, repeats from the write.
10. When DPTR wraps from `0xFFFF` to `0x0000`, sets `0x8E` to `0x01` and returns.

The tested address count is:

```
0x10000 - 0xC000 = 0x4000 = 16,384 bytes
```

Thus every address from `0xC000` through `0xFFFF` is written and read back once with the test value `0xAA`.

## 3. What this tells us

Confirmed:
- Startup expects the 16 KiB external XDATA range `0xC000–0xFFFF` to support byte writes and reads.
- The routine is a memory/interface self-test, not an audio note handler.
- A readback mismatch causes a tight wait loop; there is no graceful error return in this routine.
- The firmware explicitly tests this high XDATA region separately from the fixed `0x8000–0x8003` command port and the `0x7Cxx` parameter window.

Not established:
- Whether the 16 KiB is a discrete SRAM, a dual-port RAM, a mapped region of another processor, or a peripheral that echoes stored values.
- Whether any part of this region contains sample data, voice state, descriptors, or working memory during normal operation.
- Whether MAME's partial map of `0xC000–0xC0FF` and `0xFF10` reflects only known accesses rather than the complete hardware decode.

## 4. Why this matters for the hardware reconstruction

The external data map should now be treated as several distinct interfaces, not one presumed DSP register bank:

| XDATA region | Firmware evidence | Safe interpretation |
|---|---|---|
| `0x7C00–0x7FFF` | Part/runtime records, control bytes, status reads | Parameter/event window; backing hardware unknown |
| `0x8000–0x8003` | Four-byte indexed transfer and handshake | Fixed command/data port |
| `0xA000–0xA003` | ACIA access | MIDI serial interface |
| `0xB000–0xB003` | LCD controller access | Display interface |
| `0xC000–0xFFFF` | Startup write/read test across all 16 KiB | Read/write storage or storage-like device region |

The 16 KiB test makes `0xC000–0xFFFF` a high-priority area for future cross-reference analysis. Search for normal-operation reads/writes in this region before assigning it a role. Do not assume it is the missing 6 MB sample memory: its tested capacity is only 16 KiB.

## 5. Next steps

1. Find every reachable instruction sequence that accesses DPTR addresses `0xC000–0xFFFF` after startup.
2. Separate the startup destructive test from subsequent application use.
3. Check whether the `0x8000` command transfer or the page-selected `0x16C3` mailbox points into this region.
4. Compare the board's RAM/ASIC chip inventory and address-bus wiring against the 16 KiB range.
