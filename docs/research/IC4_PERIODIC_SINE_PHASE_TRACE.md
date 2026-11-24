# IC4 periodic sine-derived Part modulation trace

Status: verified against the descrambled IC4 v1.05 ROM and exercised by the real 8032 machine regression. The probe uses a synthetic ready response at the real first-tick XDATA address; it does not emulate the physical sound processor.

## 1. Confirmed scheduler and first-tick handshake

The periodic scheduler entry is `0x044C`. Its first tick performs the following sequence:

1. Reads the remaining-period value at direct RAM `0x49`; at the first tick this is zero.
2. Copies the configured step count at `0x47` into `0x48`.
3. Increments direct RAM `0x49`. This instruction does not modify ACC.
4. Uses the still-zero ACC as the index to `MOVC A,@A+DPTR` with `DPTR=0x0078`; therefore the actual ROM lookup is `CODE[0x0078]`, not `CODE[0x0079]`.
5. The value from that lookup is written to P2. In the actual v1.05 ROM, `CODE[0x0078]=0x00` on this first tick.
6. Sets `R1=0xFA` and reads `MOVX A,@R1`. Because `MOVX @Ri` uses the P2 output latch for the high address byte, this first status poll is at **XDATA `0x00FA`**.
7. The scheduler proceeds only when this read returns `0x40`; it then compares XDATA `0x2064` against the incremented RAM counter and calls `0x0C1A` on a match.

The status byte at `0x00FA` is part of the firmware's own XDATA status-page map, not a register in the unidentified sound chip. IC4 routine `0x0148` initializes this map: it clears 32 P2-selected 256-byte pages in the order `CODE[0x0078..0x0097]`, then writes `0x40` at offset `0xFA` in each page. The actual first tick polls the first seeded entry, `XDATA 0x00FA`. The strengthened regression now executes the real `0x0148` initializer before entering `0x044C`; it no longer injects a synthetic status response.

## 2. The periodic sine updater at 0x0C1A

The real updater reads XDATA `0x2050` as a gate/configuration byte.

- A zero value exits immediately at `0x0C22`; no phase or sample output bytes are changed.
- For the test value `0x3C`, two rotate-right operations yield `0x0F`, which becomes the phase increment.
- The updater uses the step count in RAM `0x48` to repeat the addition loop, then updates the two-byte phase state in XDATA `0x2051–0x2052`.
- It rotates the high phase byte, masks it to a six-bit index, reads the ROM table at `0x001E + index`, and writes that signed sample byte to XDATA `0x2053`.

The actual firmware writes are independently observed in the XDATA trace:

| Output field | Writer PC | One-step value | Two-step value |
|---|---:|---:|---:|
| XDATA `0x2051` | `0x0C49` | `0x00` | `0x00` |
| XDATA `0x2052` | `0x0C4E` | `0x0F` | `0x1E` |
| XDATA `0x2053` | `0x0C57` | `0x51` | `0x7E` |

All three writes in the periodic scheduler fixture have the stack return PC `0x0473`, confirming that the updater was called from the scheduler path.

## 3. Exact 64-entry ROM lookup

The signed sine lookup is the 64-byte table at IC4 code addresses `0x001E–0x005D`:

```
00 0C 19 25 31 3C 47 51 5A 62 6A 70 75 7A 7D 7E
7F 7E 7D 7A 75 70 6A 62 5A 51 47 3C 31 25 19 0C
00 F4 E7 DB CF C4 B9 AF A6 9E 96 90 8B 86 83 82
81 82 83 86 8B 90 96 9E A6 AF B9 C4 CF DB E7 F4
```

This is a signed 8-bit periodic table: zero, positive peak `0x7F`, zero, negative peak `0x81`, and back to zero. The ROM regression checks all 64 bytes against the above sequence, not just the two values exercised by the scheduler probe.

For the `0x3C` gate:
- one phase step leaves `0x2052=0x0F`; the table index is `0x07`, so the output is `CODE[0x0025]=0x51`;
- two total phase units leave `0x2052=0x1E`; the table index is `0x0F`, so the output is `CODE[0x002D]=0x7E`.

## 4. Downstream Part/slot-dependent use

The scheduler's status read at `0x0465` is a branch point. When the byte is `0x40`, the scheduler compares the tick at `0x2064`, calls `0x0C1A` if due, then returns at `0x047D`. When the byte is not `0x40`, control branches to `0x047E` and continues through the alternate processing path at `0x04B5–0x054E`. This alternate path reads the sine state at `0x2053`; it is not an unconditional straight-line continuation after the `0x0C1A` call.

The sine byte is consumed in that alternate firmware path; it is not written directly to the external command window `0x8000–0x8003`.

Observed instruction/data relationships:

- The routine obtains a slot selector in RAM `0x30` from the high nibble of an XDATA `0x28FC` read.
- It reads several slot-indexed bytes from the `0x28xx` area, including offsets based on `0x28C8`, `0x2888`, and `0x2898`.
- It reads one slot-indexed value from `0x2020 + slot`, then a second from `0x2B61 + slot`.
- The latter values are multiplied; the sine sample from `0x2053` is subsequently sign-adjusted, rotated and multiplied into a fixed-point intermediate.
- The intermediate is split into scratch bytes in internal RAM `0x36/0x37`.
- The firmware then accesses a two-byte slot-indexed field in the `0x20xx` region using an address derived from `2 × slot`, adds the intermediate with carry, and applies a saturation branch before storing the final high byte.

The ROM therefore establishes a **Part/slot-dependent sine-derived parameter modulation path** with separate coefficient/state tables. It does not yet establish the user-facing name of the modulated parameter, the physical destination of that parameter, or that the table is used to generate audio samples.

## 5. Regression coverage

The Intel/Linux regression runs the actual IC4 v1.05 status-map initializer at `0x0148`, followed by the scheduler at `0x044C`. It checks:

- all 32 P2-selected XDATA pages from `CODE[0x0078..0x0097]` receive a `0x40` status byte at offset `0xFA`, with adjacent `0xFB` cleared;
- the scheduler's first-tick page lookup reaches the real XDATA status byte at `0x00FA`;
- scheduler return at `0x047D` with the firmware-initialized `0x40` status and matching `0x2064` tick;
- gate-off behavior with no writes to `0x2051–0x2053`;
- one-step and two-step phase values and their ROM table outputs;
- exactly one write to each output address at the firmware PCs `0x0C49`, `0x0C4E`, and `0x0C57`, each returning to `0x0473`;
- no SoundHost reads, writes, or command packets during this isolated XDATA phase/sine path;
- all 64 entries of the ROM lookup table.

The passing trace was captured on commit `96161eecb8c4a78db6ef3e4b531758357d07031f`, run `45416665661`. The follow-up regression strengthens the assertions with the full 64-byte table and exact writer PCs.

## 6. What remains unknown

- The exact semantic name/range of the Part/slot-indexed source values and destination pair in XDATA `0x20xx`.
- Whether the modulation is always active in normal hardware state or depends on particular system/Part configuration.
- The audio-peripheral protocol, its actual response bytes, and how this modulation reaches the physical sound generator.
- Whether any part of the sine path is related to the external sound chip's own LFO or merely produces a parameter offset in IC4.

Do not implement this table as an audio oscillator or claim authentic LFO sound in the plugin until the parameter mapping and physical destination are verified. The active `BasicMASSVoiceBackend` remains a generic test oscillator.
