# FX selector dispatch and immutable IC51 upload plans

## Scope

This note separates three paths that had previously been easy to conflate:

1. the firmware handlers that write the selected FX type to XDATA;
2. the FX1/FX2 loaders that resolve those selector values through separate IC4 directories and IC51 resource streams;
3. the emulator's immutable copy of the exact bytes written to the external `0xFF10` port.

The work below reconstructs the firmware-visible data and transport. It does not identify the receiving FX DSP, assign register meanings to the stream bytes, or implement the FX algorithms.

## 1. FX1 and FX2 store handlers

The descrambled IC4 v1.05 bytes contain two separate selector-store blocks:

| Family | Handler entry | Selector source | Clamp | XDATA write | Continuation |
|---|---:|---|---|---:|---|
| FX1 | `0x5407` | `IRAM[0x6A]` | values >= `0x15` become `0x14` | `0x7BFE` at PC `0x5416` | `0x7C29`, set `0x2B94=1`, then `0x563E` |
| FX2 | `0x543D` | `IRAM[0x6A]` | values >= `0x15` become `0x14` | `0x7C06` at PC `0x544C` | `0x7C29`, set `0x2B94=1`, then `0x563E` |

Both blocks are conditional on the status byte at XDATA `0x3708` being nonzero, after a separate `0x3702` condition. The destinations are different: they must not be aliased.

### 0x7C29 is a deferred-request scheduler, not the FX resource loader

Both handlers call `0x7C29` after storing their selector. It merges the current request with any already-pending request and writes independent flags:

- FX1: `R7 != 0` or an already nonzero `XDATA[0x308B]` sets `0x308B=1`; otherwise it writes zero.
- FX2: `R5 != 0` or an already nonzero `XDATA[0x308D]` sets `0x308D=1`; otherwise it writes zero.
- `R3` is copied to `XDATA[0x3097]` as the staging byte; `XDATA[0x309C]` is set to 1.
- It then calls `0xB14B`, six `0xB0EF` six-byte writers, `0xB2ED`, and one final `0xB0EF`. The actual firmware emits exactly 54 service bytes to XDATA `0xFF10`. With the request values used by the two handlers, the byte sequence is:

```
01 00 45 37 8C C1
01 01 8A 00 01 00
01 01 87 00 00 00
01 01 86 00 00 00
01 01 8C 00 50 00
01 01 8E 00 00 00
01 01 8D 00 00 00
01 00 08 1C 02 CF
01 01 89 00 00 00
```

Every byte is a write to `0xFF10`; with firmware bit `0x1B` set, the wrapper switches direct SFR `0x8E` to mode `0x02` for the MOVX write and restores it to `0x01`. These 54 bytes are a fixed service/control sequence emitted by `0x7C29`; they are **not** either IC51 effect payload.

After the sequence, `0x7C29` reads four separate status/input bytes: `0x7BFF -> 0x309A`, `0x7C00 -> 0x309D`, `0x7C07 -> 0x309B`, and `0x30BA -> 0x309E`. Their physical device/register meanings and acknowledgement semantics remain unknown.

A new real-ROM machine regression executes `0x7C29` twice, once with only the FX1 request input asserted and once with only the FX2 request input asserted. It checks the independent pending flags, staging/counter values, all 54 bytes in exact order, address `0xFF10`, bus mode `0x02`, and P2 `0xFF`. It does not assign a DSP command meaning to any byte.

### Deferred loader consumers

The pending flags are consumed later by separate firmware blocks:

- FX1 consumer `0x7D67` checks `0x308B`. When set it reads `0x3097` into R7 and calls `0x7FF2`; when clear it calls `0x7EE2`. It then clears `0x308B`, samples `0x7BFF` into `0x308A`, samples `0x7C00` into `0x308E`, and increments `0x309C`.
- FX2 consumer `0x7D34` checks `0x308D`. When set it reads `0x3097` into R7 and calls `0x88D1`; it then clears `0x308D`, samples `0x7C07` into `0x308C`, samples `0x30BA` into `0x3090`, and increments `0x309C`.

The staging byte read into R7 is passed into the loader context, but the FX type itself is read by the FX1/FX2 helper from the separate selector locations `0x7BFE` and `0x7C06`. Keep these fields distinct.

An adjacent FX2 preparation block at `0x7D21` calls `0xB28D`, checks `0x308D`, and calls `0xB08B` when FX2 is pending; this routine emits a repeated FF10 sequence. Its exact hardware interpretation and relation to the later `0x7D34` loader remain open.

A machine regression now starts at each complete handler entry (`0x5407` and `0x543D`), tests values `00, 01, 13, 14, 15, 7F`, and checks the clamped result in the corresponding external XDATA location. The same test then runs the FX1 loader helper to confirm that the value at `0x7BFE` is copied to the loader's working selector at `0x3098`.

**Unresolved:** direct-call scans find no ordinary LCALL/ACALL to either handler entry. Their higher-level dispatcher/indirect-entry provenance is not yet pinned. The test enters the verified handler blocks directly; it does not claim that a real Performance edit or controller event has been traced all the way into them.

## 2. FX loader reads are distinct from selector stores

The downstream loader paths remain independently verified:

- FX1 helper `0x7FF2` reads `0x7BFE`, copies/clamps it into `0x3098`, and indexes the 21-entry directory at `0x8423`. The normal caller in the Note On firmware path is at `0x7D72`.
- FX2 loader entry `0x88D1` reads `0x7C06` into its working selector and uses the separate 21-entry directory at `0x8F99`. The ordinary conditional caller is around `0x7D3F`.

The real-ROM MIDI fixture has been observed executing the FX1 caller `0x7D72 -> 0x7FF2`; however, in that fixture the low `0x7Bxx/0x7Cxx` peripheral window is explicitly served by the test overlay. This proves the firmware read/call path under the stated fixture, not the physical source of the selected byte. The FX2 runtime loader has a separate direct regression at `0x88D1`, also with a test-provided selector.

## 3. IRAM[0x6A] has multiple producers; do not equate it with one persistent FX setting

The raw IC4 image contains multiple writes to direct IRAM address `0x6A`. They include:

- PC `0x5F62`: copy XDATA `0x28F2`;
- PC `0x5F95`: copy a table byte selected through `MOVC`;
- PCs `0x6337` and `0x63B6`: clear it;
- PC `0x64A8`: copy XDATA `0x28FD`;
- PC `0x713A`: copy a byte returned by helper `0xEBED`;
- PC `0x7AAD`: another `0xEBED`-based parameter read;
- additional context-specific writers occur at `0x8246`, `0x8BB7`, `0x8C5E`, `0x95A9`, and `0xA262`.

This is a reusable scratch/parameter register, not a globally persistent FX selector variable. The writer that supplies `IRAM[0x6A]` immediately before either `0x5407` or `0x543D` still needs to be tied to the real dispatcher context.

### Separate generic parameter-validation path at 0x5ECD / 0x5F6E

A distinct generic parameter dispatcher also uses `IRAM[0x6A]`:

1. `0x5ECD` reads an index from XDATA `0x28F7`, computes `0x612F + index * 0x0C`, and stores that 12-byte table-entry address at XDATA `0x28F5/0x28F6`.
2. The first two bytes of the entry are copied to `0x28F3/0x28F4` as a separate XDATA parameter address. For table index zero, entry bytes `37 03` form address `0x3703`.
3. The validator setup at `0x5F6E` reads entry offsets `+0x0A` and `+0x0B` via `MOVC`, copying them to `IRAM[0x69]` and `IRAM[0x6A]`. It then passes the parameter address `0x28F3/0x28F4` to `0x5D75`.
4. Direct LCALL sites at `0x6015` and `0x601C` enter `0x5F6E` from the separate `0x5FE6` dispatcher.

Those bytes are table-driven validation bounds for that generic parameter path; for example, table index zero has trailing bytes `00 01`, while index one has `00 0F`. This is **not** proof that `0x5F6E` supplies the FX1/FX2 selector handlers. It is a useful example of why the same IRAM scratch byte cannot be given one permanent meaning.

## 4. Exact FF10 upload plans are now prebuilt and handed to the backend

The firmware-derived IC51 decoders already recover both byte-stream formats and the separate 21-entry FX1/FX2 directories. The exact serializer converts each decoded stream into the ordered bytes that IC4 writes to external XDATA `0xFF10`:

- FX1 triplet format: count, two preamble bytes, then the number of payload bytes actually emitted by the IC4 8-bit `MUL AB` / `DJNZ R1` loop, `(count * 3) mod 256`.
- FX2 rotated-header format: rotated count, three fixed preamble bytes, then count payload bytes.
- Skipped bytes and zero-count terminators are retained as metadata but are not placed in the output write stream because the firmware does not write them to `0xFF10`.

New class `MASSFXUploadPlanCatalog` materializes all combinations during startup:
- FX1: 2 IC51 banks × 21 effects;
- FX2: 2 IC51 banks × 21 effects;
- 84 immutable program plans in total.

The JUCE processor loads this catalog after the FX1 and FX2 resource catalogs. `MASSAudioEngine` passes a read-only pointer through `IMASSVoiceBackend::setFXUploadPlanCatalog`, including when a backend is replaced. The real-time callback is not required to parse IC51 descriptors or allocate output vectors. A future verified SoundHost implementation can select a ready-made byte plan by (family, bank, effect index).

The regression resolves every plan across all 84 family/bank/effect combinations and checks its total byte count against the decoded source streams. It also verifies the backend handoff and lookup for FX1 Room (325 bytes) and FX2 Equalizr1 (177 bytes).

**Evidence boundary:** this closes the byte-exact resource-to-backend *data handoff*. It does not yet send these plans to a modeled physical DSP during playback, interpret their contents as DSP registers, or produce original FX audio. The receiving device, acknowledgement protocol and algorithm semantics remain unresolved.

## 5. Next trace

1. Capture the actual event/dispatch entry that reaches `0x5407` and `0x543D`, including the caller and the instruction that populated `IRAM[0x6A]`.
2. Compare the real Performance FX values with the values stored at `0x7BFE` and `0x7C06`; do not rely on the synthetic low-XDATA overlay.
3. Trace the `0x7C29` continuation and determine whether it schedules an upload, status poll, or unrelated control transaction.
4. Keep the verified `0xFF10` upload byte plan separate from the still-unidentified `0x8000–0x8003` four-byte command protocol.
5. Identify the receiving chip/board routing before implementing any FX DSP state machine or algorithm.
