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



## 6. Event-command path into the FX1/FX2 store handlers

The static IC4 v1.05 bytes recover the subcommand dispatcher that leads to the two store blocks. It is a distinct layer from both `0x7C29` (request scheduling) and the `0x7FF2/0x88D1` resource loaders.

### Three-field event staging

At `0x57A8`, the firmware stores its incoming register tuple without reinterpretation:

| Input register | Staging byte | Later role |
|---|---:|---|
| `R7` | IRAM `0x60` | high-nibble event class and low-nibble Part/context selector |
| `R5` | IRAM `0x61` | subcommand within the event class |
| `R3` | IRAM `0x62` | subcommand value / selector byte |

The helper splits `IRAM[0x60]` into its low nibble at `0x63` and high nibble in `0x60`. The fan-out code around `0x597E–0x59B7` selects Part/context values `0x0C–0x0F`, passes the class/subcommand fields into `0x5671`, and copies the staged value `IRAM[0x62]` into `IRAM[0x67]`.

For the class-`0x0B` branch, `0x5671` enters `0x56C9`. That branch restores the staged context to R7, copies the subcommand from `IRAM[0x66]` to R5, and copies the payload from `IRAM[0x67]` to R3 before calling `0x51B6`. `0x51B6` stores those values in `0x68/0x69/0x6A` and calls `0xED30`.

### Exact command table records

`0xED30` uses its caller return address (`0x51C1`) as a code-table base, scans three-byte records, and performs a computed jump to the first two bytes of the record whose third byte matches R0. The two relevant records are:

| Table address | Record bytes | Subcommand | Handler |
|---:|---|---:|---:|
| `0x51DC` | `53 F3 50` | `0x50` | `0x53F3` — FX1 selector handler |
| `0x51DF` | `54 29 51` | `0x51` | `0x5429` — FX2 selector handler |

From those entries, the handler body checks the `0x3702` context and `0x3708` gate, clamps R3/IRAM `0x6A` to 0..20, then writes to `0x7BFE` (FX1) or `0x7C06` (FX2). It calls `0x7C29` only after the selector store.

This establishes the *firmware command dispatch table* and the three-byte staging contract. It does **not** yet prove which real panel, Performance edit, MIDI, or SysEx producer supplies a given tuple, nor which instruction is the final producer of IRAM `0x6A` on that real path. `0x6A` remains a shared scratch/parameter byte with multiple writers. The remaining provenance trace must begin at the real caller of `0x57A8` and retain its R7/R5/R3 values through the Part fan-out.

The machine regression added alongside this note runs the original `0x51B6 -> ED30` dispatcher for both command IDs and values `00, 01, 14, 15, 7F`, then checks the exact destination write after clamping. It validates the code table and handler route under explicit `0x3702/0x3708` fixture values; it is not a captured hardware event.


## 7. Gate polarity and external 0x7BDx event source

The two selector handlers have now been checked at instruction level, not inferred from the test fixture:

- At `0x53F3` (FX1) and `0x5429` (FX2), the first condition reads `XDATA[0x3702]`, XORs it with `IRAM[0x68]`, and skips to `0x563E` if they differ.
- The second condition reads `XDATA[0x3708]` and executes `JNZ` over the skip branch. Therefore the handler requires **nonzero** `XDATA[0x3708]`; zero disables the selector write.
- A prior direct-dispatch test fixture incorrectly supplied zero at `0x3708`. That fixture was corrected to one; the direct handler path and the higher-level event path now use the observed polarity.

The upstream entry points at `0x3070` and `0x307A` call helper `0x40C4`, which forms the three fields passed to `0x57A8`:

1. `XDATA[0x3702] + 0xB0` is staged as R7 (the event class plus context).
2. `XDATA[0x7BD9]` is staged as R5 (the command/subcommand).
3. R3 is selected from either `XDATA[0x7BDA]` or `XDATA[0x7BDB]`, depending on incoming R7.

The two event entry paths select those data bytes differently:

- `0x3070` sets bit `0x1A` before the rotate and enters `0x40C4` with R7=0, selecting `0x7BDA`.
- `0x307A` complements bit `0x1A` before the rotate. Entering with bit `0x1A=0` produces R7=1 and selects `0x7BDB`.

A new runtime test is being added to run both real IC4 entry blocks, through `0x40C4 -> 0x57A8 -> Part fan-out -> 0x5671 -> 0x51B6 -> ED30`, then verify the corresponding FX selector address and the 54-byte scheduler service request. The source bytes are visible in the descrambled ROM at `0x3070`, `0x307A`, and `0x40C4`. The XDATA window values remain synthetic test stimuli; their physical producer/device has not yet been identified.

## 8. Deferred FX load: payload and post-load service are distinct transactions

Executing the deferred consumers with the real IC4 and IC51 images shows that the complete consumer emits more than the selected resource payload:

### FX1 Room, bank 0

- IC51 upload: 325 writes to `0xFF10` (75+244 payload bytes and their two block headers/preambles).
- Post-load service: 48 additional writes, eight six-byte transactions. They are emitted by the B0EF/B14B writer code after the resource stream returns.
- Total at the deferred consumer boundary: 373 writes.

Post-load service bytes, in order:

```
01 01 88 00 00 00
01 01 86 00 00 00
01 01 87 00 32 00
01 01 8A 00 05 00
01 01 8B 00 06 00
01 01 8C 00 02 00
01 00 45 37 88 01
01 02 C1 27 10 00
```

The first six packets and the final packet are emitted through B0EF; the seventh uses B14B. Their recorded return-PC groups are `7EED`, `7EF8`, `7F6B`, `7F77`, `7F9F`, `7FAB`, `7FB0`, and `7C28`. Byte-writer PCs are B0FC/B10A/B118/B126/B134/B143 for B0EF; the B14B packet uses B158/B167/B176/B185/B197/B1A6.

### FX2 Equalizr1, bank 0

- IC51 upload: 177 writes to `0xFF10`.
- Post-load service: 18 writes, three six-byte B0EF transactions.
- Total at the deferred consumer boundary: 195 writes.

Post-load bytes:

```
01 03 F8 00 0F 00
01 03 F9 00 0F 00
01 03 FA 00 0F 00
```

All three packets have return-PC `0x8765` and use writer PCs B0FC/B10A/B118/B126/B134/B143.

These post-load transactions are not part of the IC51 resources and must not be appended to the immutable 325-byte/177-byte effect upload plans. Their device-level meaning remains unclassified until the receiver and acknowledgement behavior are identified. Regression checks now assert the 325+48 and 177+18 sequences separately, including address, byte value, mode, P2, writer PC and caller return context.

## 9. Next trace

1. Identify the physical producer of the external XDATA source bytes at `0x7BD9–0x7BDC` and map which panel/Performance/controller operation updates them.
2. Trace the real Performance/controller/MIDI caller of `0x3070` or `0x307A`, retaining the input bit-0x1A state and the R7/R5/R3 tuple through `0x40C4` and `0x57A8`.
3. Compare real Performance FX1/FX2 values with stored `0x7BFE/0x7C06` and subsequent deferred consumers `0x7D67/0x7D34`.
4. Keep the 54-byte `0x7C29` scheduler request, the 325/177-byte IC51 resource payloads, their 48/18-byte post-load sequences, and the separate `0x8000–0x8003` command protocol as distinct transactions.
5. Identify the receiving chip/board routing and acknowledgement behavior before implementing any FX DSP state machine or algorithm.
