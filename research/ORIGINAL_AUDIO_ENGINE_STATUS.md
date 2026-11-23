# Original audio engine reconstruction — verified checkpoint

Branch: `research/original-technox-audio-engine`

## What is now established

- IC4 v1.05 runs in the in-project Intel 8032/MCS-51 machine model.
- The CPU model accepts timed MIDI bytes through the MC6850-compatible ACIA path.
- IC51 is selectable through the modeled P3 bus signals and can be read in two banks.
- The IC51 resource resolver can follow the firmware-derived bank/program pointer table, parse a descriptor, and decode the command stream into structured resource commands.
- The IC4 firmware accesses an undocumented external audio register window at `0x8000–0x8003`. The emulator captures contiguous four-register writes as packets and captures paired bytes as 16-bit burst words. Each byte now carries its own P2/P3, device-index, bus-mode, IC51-select, reset, PC, and cycle snapshot; host reads capture the corresponding bus context and caller return address.
- The existing firmware integration fixture drives a MIDI note stimulus and asserts a repeatable change from 548 startup packets to 553 additional packets. It checks the two 256-write sweeps and the later 32-iteration/table-write sequence by call-site/return-PC and packet shape.

## Trace retention correctness

- SoundHost write/read traces are bounded rings (4096 writes, 2048 reads). Their accessors take absolute event indices modulo capacity, so only indices in the newest retained window are valid after wraparound.
- The MIDI-stage report now clamps each stage's output to that retained window and explicitly reports total, retained, and overwritten events. This prevents wrapped indices from silently printing newer events as if they belonged to an earlier part of the stage.
- This is a trace-reporting correctness fix; it does not alter firmware execution or the emulated audio-device behavior.

## Latest tracing improvement

- The real-ROM MIDI fixture already separates Performance-mode Program Change, Note On, and Note Off, then exercises Multi-mode SysEx setup and a synthetic-ready Note On/Off path.
- The stage report now also emits every captured SoundHost response read made during that stage, including register index, returned byte, instruction PC, caller return PC, P2/P3, bus mode, IC51-select, reset state, and machine cycle. This makes read-side handshake behavior directly comparable between MIDI stages.
- SoundHost write events now retain the matching per-write context as well: selected device index, instruction/caller PCs, P2/P3, bus mode, IC51-select, reset state, and cycle. The MIDI-stage report prints these writes alongside response reads, enabling transaction-level comparison without assigning undocumented register meanings.
- This is additional observability only. It does not change the open-bus model or claim that the returned bytes represent real DSP responses.

## MIDI-stage write classification

- Each stage's retained SoundHost write window is summarized by register index and selected device index, in addition to the raw chronological event list.
- Register-3 writes of `0x40` and `0xC0` are counted separately as the two already-observed handshake-strobe values. The report does not infer meanings for other values or for the device-index latch.
- The summary is a comparison aid for Program Change, Note On, and Note Off traffic; it is not a decoded command protocol.

## Unknown 7Cxx peripheral trace

- The firmware fixture already captures unmodeled external-bus accesses in `0x7C00–0x7FFF` with address, read/write direction, value, instruction PC, caller return PC, P3, bus mode, overlay flag, and cycle.
- MIDI-stage reporting now prints the trace-window total, retained count, and overwritten count before listing the 7Cxx events. The existing event list is restricted to the current stage and retained ring-buffer window.
- This improves the evidence around the repeatedly polled `0x7D95` status location and initialization writes in `0x7C00–0x7C1F`. It does not identify the peripheral or assign register meanings.

## Program identity now reaches the voice layer

- `VoiceSlot` captures the selected Part's IC51 bank and sound number at note-on.
- `MASSAudioEngine` reads the active Part's bank/sound parameters and passes them into newly allocated and mono/lead-retargeted voices.
- This closes a real integration gap: a future resource-backed DSP backend can now identify the exact IC51 program for each active voice without consulting UI/editor state.
- This is program routing metadata only. The current `BasicMASSVoiceBackend` remains a generic test synthesizer; no PCM/sample playback or original DSP behavior is claimed.

## Predecoded IC51 program catalog

- Added `MASSResourceProgramCatalog`: startup-time resolution of all 512 bank/program pairs into a fixed-index catalog.
- Lookup by bank and sound is bounds-checked and returns a resolved descriptor/command-stream view; it does not parse ROM data or allocate on the lookup path.
- Tests cover loading the full catalog, all 512 indexed entries, and invalid bank/program indices.
- Together with `VoiceSlot.bankNumber/soundNumber`, this establishes the program-identity handoff needed by a resource-backed voice backend. The catalog still contains firmware resource commands, not decoded PCM waveforms.

## Predecoded resource catalog now reaches the voice backend

- The plugin now loads the complete 512-entry IC51 program catalog once during processor construction, after validating the raw ROM image.
- `MASSAudioEngine` publishes a read-only catalog pointer to the active `IMASSVoiceBackend`. Replacing the backend also forwards the same catalog pointer to the new backend.
- Each `VoiceSlot` already captures its bank/program pair at Note On. A backend can now resolve that pair directly to the firmware-parsed descriptor and command stream without parsing ROM data, allocating, or consulting editor state in the audio callback.
- A regression probe in `test_mass_resource_rom` verifies that Part 13 bank 2 / sound 10 reaches the backend and resolves the same descriptor and command count as the catalog.
- This is a data-path integration milestone, not synthesis: `BasicMASSVoiceBackend` remains the generic oscillator test backend, and the resource commands still do not describe decoded PCM or a proven DSP algorithm.

 
## FX resource and host-stream layer

- Recovered the 21-entry IC4 FX directory at `0x8423` (32-byte records), including the exact first/second IC51 resource selectors used by firmware calls at `0xAF93` and `0xB015`.
- Added startup/offline parsers for the two byte-stream formats sent through XDATA `0xFF10`: count + two-byte preamble + `count*3` payload bytes, and circularly-rotated count + three fixed preamble bytes + `count` payload bytes. Zero-count blocks are represented as no-write resources.
- Regression tests compare every FX label and resource pair with the real descrambled IC4 ROM, then resolve both resource formats for every effect in both IC51 banks.
- This gives a second verified IC51 path beyond the descriptor/command-stream catalog. It is resource transport metadata only; no FX algorithm, PCM format, or physical DSP has been identified.

 
## Separate FX1 and FX2 directories reach the voice backend

- FX1 is the 21-entry Room/Reverb/Delay family in the IC4 directory at `0x8423`, with a `0x20`-byte entry stride.
- FX2 is a separate 21-entry Chorus/Flanger/Phaser/Panning/Delay/EQ/Drive family in the IC4 directory at `0x8F99`, with a `0x2A`-byte entry stride. Directory classes and resource catalogs remain separate; FX1 selectors never resolve against the FX2 table.
- Both directory families provide two IC51 upload-stream selectors at offsets `+0x0B` and `+0x0C`. The neutral names `streamA` and `streamB` describe the two firmware upload calls, not processor type IDs.
- `MASSFX1ResourceCatalog` and `MASSFX2ResourceCatalog` predecode all 21 types in both IC51 banks. The processor loads both catalogs at startup and passes immutable pointers to the active backend; backend replacement preserves both pointers.
- The FX2 loader around `0x88EA` reads its selector from XDATA `0x7C06`, clamps values >= `0x15` to `0x14`, selects the record at `0x8F99 + index * 0x2A`, then sends the two stream selectors through `0xAF93` and `0xB015`. Regression checks pin those instructions against the real IC4 ROM.
- Tests compare all 21 FX1 and all 21 FX2 directory names/resource selectors against the real descrambled IC4 image, resolve every entry in both IC51 banks, and verify the backend receives both catalogs and resolves the independent FX1/FX2 controls during render.
- The 50 factory Performance SysEx frames use FX1 indices `{0,1,2,3,7,8,10,11,12,18,19}` and FX2 indices `{0,4,5,6,7,9,10,11,13,15,18}`. CI checks the exact per-index histograms rather than an earlier incorrect assumed list.
- This confirms directory selection and resource upload framing only. No FX DSP algorithm, real-time effect processing, physical receiver identity, or audio response is implemented by these catalogs.

## Program catalog now caches both command-stream flag outcomes

- The resource command decoder has two firmware-confirmed group-0 paths selected by transient bit 0x0A; terminal group-0x20 behavior also changes with that bit.
- `MASSResourceProgramCatalog::loadBothFirmwareFlagVariants` predecodes all 4 banks × 128 sounds under both interpreter outcomes at processor startup. The backend can retrieve either immutable decoded result through `find(bank, sound, firmwareFlag0ASet)`.
- This flag is not a stored Performance/Part value. Disassembly shows it can be synchronized from XDATA page offset `0xFB` bit 5 in page-setup routines, is cleared by `0x1494`, and is temporarily set around a nested interpreter entry at `0x192D` before being cleared again.
- Regression coverage checks both possible decoder outcomes for all 512 bank/program pairs, matching descriptor addresses and command-stream pointers, valid terminal states, non-truncated streams and the flag-context marker in each cached entry.
- The cache preserves both results so a future backend can model the correct interpreter call context. It does not claim that one flag value applies to an entire preset or voice.

 
## Firmware-loaded 16-KiB sound-memory image reaches the audio backend

- Added `MASSIC51ExternalSoundMemoryImage`, a startup-time reconstruction of the C000-FFFF image written by the original IC4 bulk loader.
- The image uses the observed IC51 bank-0 table entry R7=`0x48`: source address `0x3C64`, 13,647 bytes (`0x354F`), and the exact IC4 logical-to-physical map at code address `0xADDD`. The final written byte is at `0xF5B2`; the array retains all 16,384 bytes and leaves unwritten locations zero.
- The processor builds the immutable image once at startup and passes it to `MASSAudioEngine` / `IMASSVoiceBackend`. Firmware order is confirmed: IC4 first tests C000-FFFF at `0xB34D`, then the startup caller sets R7=`0x48` at `0x379F` and calls the IC51 bulk loader at `0x37A1` / `0xAF1A`. The engine forwards the same image pointer when a backend is replaced; no ROM parsing, permutation or allocation occurs in the audio callback.
- The real-ROM integration test compares the reconstructed image against every byte of the XDATA window after executing IC4 boot. The resource test also verifies image bounds, transfer metadata, mapped source bytes and backend hand-off.
- This is an exact ROM-to-memory reconstruction, not sample decoding: the receiver identity and format of the 16-KiB contents remain unknown, and the generic oscillator is still not the original DSP.

 
## Complete 1EDD parameter stages are now composed

- Added `MASS1EDDFullParameterPipeline` to chain the reconstructed nonlinear `0x1288/0x1308` lookup and optional `0x2381/0x1848` external-byte transform with the subsequent `0x1FCB..0x2005` slot coefficient stage.
- The coefficient stage includes the opcode-bit-1 two-MUL scale and the four `opcode & 0x41` modes: `0x00` bypass, `0x01` and `0x40` alternate `0x1890` arithmetic, and `0x41` table-driven `0x11E0/0x11E8` modulation scale.
- The composed API returns the intermediate nonlinear/external results and the final slot coefficient. The caller must still supply the byte read from the P3-selected external bus; the physical source and complete slot-to-DSP packet-state mapping are not claimed.
- Regression tests cover all four dispatch modes, the two-MUL scale, and a full nonzero-R7 path through the external-address/byte/1848 sequence.

## What is not established

- The four external register meanings are not decoded. The current names `register0..register3` are intentionally neutral.
- A captured firmware packet is not yet a sample-playback command specification. Do not assign sample address, pitch, loop, envelope, or voice semantics based only on byte patterns.
- `MASSResourceProgramResolver` recovers IC51 metadata and command streams; it explicitly does not decode PCM.
- `BasicMASSVoiceBackend` is still a generic oscillator test voice. It is not original TechnoX audio.
- The physical sound processor / DSP identity, its host-port handshake, sample-memory mapping, sample encoding, voice architecture, filters and effects remain unverified. Period advertising describes 6 MB of sampled waveforms, so the 128 KB IC51 image cannot by itself be treated as the complete waveform store; the larger sample-memory path still has to be located and mapped.
- The modeled C000–FFFF sound-memory backing store is an emulation aid, not proof of the physical memory topology or a decoded audio ROM.

## Next reverse-engineering milestone

1. Use the new per-byte write and read context to correlate every 8032 access to `0x8000–0x8003` with caller/return address, P2/P3 state, sound-bus mode, reset state, and preceding IC51 reads. Identify any control-line transitions that occur between bytes of one packet.
2. Separate initialization/configuration bursts from note-on, note-off, pitch, controller, and resource-load traffic using controlled MIDI stimuli.
3. Determine the exact host-port protocol, including ready/busy/ack timing and the meaning of reads versus writes. The present open-bus response must not be mistaken for hardware behavior.
4. Trace the external sample-memory address/data path and establish whether IC51 holds descriptors/parameter streams while a separate memory holds PCM waveforms.
5. Only after the above is supported by repeatable traces or hardware documentation, implement a packet interpreter and a sample-accurate backend. Keep the generic oscillator clearly labeled as a test backend until replacement.

## Regression anchors

The current `tests/test_technox_8032_machine.cpp` contains the firmware/MIDI fixture and packet-shape assertions. The current `tests/test_mass_resource_rom.cpp` contains the IC51 directory, descriptor, command-stream and firmware arithmetic tests. Run the full CTest suite after every change to the CPU, bus, ROM mapping, or resource decoder.

## Paged IC51-to-XDATA transfer model

- Added `MASSIC51BulkBlockHeader::copyPayloadToPagedXdata`, which models the firmware's two address stages separately: IC51 source bytes advance linearly, while logical R0 is translated through a caller-supplied 256-byte logical-to-physical map before each XDATA write.
- A logical R0 wrap advances to the next 256-byte XDATA page, matching the observed P2 increment in the `0xAF05` helper.
- The 256-byte lookup at IC4 address `0xADDD` is recovered from the **descrambled** v1.05 program ROM. All 256 bytes match `TechnoX8032Machine::mapSoundMemoryLowAddress`; the map is a bijection over all 256 low-byte addresses.
- The real-ROM 8032 integration test now executes the actual IC51 bank-0 transfer, verifies all 13,647 destination bytes against the IC51 source block after address permutation, and checks the final physical write at `0xF5B2`.
- The independent C++ paging helper still has synthetic-map tests for page rollover and invalid destination sizes; the machine-level test covers the recovered firmware map and real transfer.
- This confirms the firmware's staged memory image and address mapping. It does not identify the receiver, decode the copied data format, or establish PCM playback.

 
## IC51 bulk-loader call-site audit

- The current reachable IC4 disassembly has exactly two direct callers of `0xAF1A`: `0x37A1` (preceded by `R7=0x48` at `0x379F`) and `0x38F1` (preceded at `0x38EF`). Both select the same bank-0 bulk block.
- `0x37A1` is embedded in the separate `0x367F` diagnostic/status path; it brackets the loader with bit `0x95) changes and continues into further status waits. `0x38F1` is in the `0x381C` initialization routine, called from `0x326A), and sets bit `0xAF` after the transfer.
- Added runtime entry tracing records call PC, selector R7, P2/P3, external bus mode `0x8E`, bit `0x1B`, and IE.EA at the instant `LCALL 0xAF1A` executes.
- The cold-boot regression now asserts exactly one executed transfer entry, from `0x38F1`, with R7=`0x48`. The 37A1 path is verified in the ROM image but is not claimed to execute during this normal boot.
- Existing byte-exact checks remain: IC51 bank 0 source `0x3C64), length `0x354F` (13,647 bytes), destination remapped through `0xADDD` to last address `0xF5B2`, plus full 16-KiB C000–FFFF memory-image comparison. This is an external-memory upload, not yet a decoded audio waveform or DSP implementation.

## Firmware FX selector paths

- The FX1 type helper at `0x7FF2` reads XDATA `0x7BFE`, writes the selected value to `0x3098`, and clamps values >= `0x15` to `0x14` before the 21-entry FX1 directory path. The current XDATA watch fixture exposes `0x7BFE=0x00` only under an explicit synthetic test overlay, so its upstream population is not yet validated against physical hardware.
- The independent FX2 loader around `0x88EA` reads XDATA `0x7C06`, clamps values >= `0x15` to `0x14`, uses the 21-entry directory at `0x8F99` with `0x2A` stride, and calls `0xAF93` / `0xB015` for the two resource streams. The firmware bytes for its selector, clamp, table base, selector offsets and both calls are regression-checked against the real IC4 ROM.
- The two loader paths have separate conditional callers: FX2 is called at `0x7D3F` when XDATA `0x308D` is nonzero; FX1 is called at `0x7D72` when XDATA `0x308B` is nonzero. Both callers read the shared staging byte at `0x3097` before invoking their respective loader. These call-site bytes are now regression-checked against the real IC4 image.
- Both selectors are zero-based 0..20; 20 is `no Effect`. Factory MIDI/SysEx Common blocks store separate FX1 and FX2 selector bytes, and the exact frequency of each index across all 50 factory Performances is asserted in CI.
- `MASSCommonControlStore`, Performance-bank loading, controller updates and saved-state restoration normalize both controls to 0..20. The JUCE processor publishes the two controls independently, and the backend interface receives separate immutable FX1 and FX2 catalogs.
- The upstream mapping from Performance/Common values into the two XDATA selector locations, hardware response and DSP processing remain to be verified. Catalog resolution is implemented; original FX audio is not.

## Effect menu indexing

- The manual's menu labels are one-based 1..21, while firmware directory indices are zero-based 0..20. Index 0 represents the first named algorithm; index 20 is `no Effect`.
- FX1 and FX2 share the numeric range, but their directory contents, stream selectors and catalog pointers must remain separate.
