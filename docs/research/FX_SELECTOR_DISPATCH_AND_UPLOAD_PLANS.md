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
