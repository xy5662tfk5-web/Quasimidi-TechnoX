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
- Added startup/offline parsers for the two byte-stream formats sent through XDATA `0xFF10`: count + three-byte preamble + `count*3` bytes, and circularly-rotated count + three fixed preamble bytes + `count` payload bytes. Zero-count blocks are represented as no-write resources.
- Regression tests compare every FX label and resource pair with the real descrambled IC4 ROM, then resolve both resource formats for every effect in both IC51 banks.
- This gives a second verified IC51 path beyond the descriptor/command-stream catalog. It is resource transport metadata only; no FX algorithm, PCM format, or physical DSP has been identified.

 
## FX resource catalog reaches the voice backend

- Added an immutable `MASSFXResourceCatalog` that loads the fixed 21-entry IC4 FX directory and resolves both referenced IC51 stream resources for every FX type in both IC51 banks.
- Both transport formats are parsed as complete resource sequences: each nonzero block is retained in order, while the terminating zero-count byte is recorded as an end marker and is not sent to the host port.
- The processor constructs this catalog once at startup from the descrambled IC4 code and validated IC51 image. It passes the read-only catalog to `MASSAudioEngine` and the active voice backend; backend replacement reattaches the same pointer.
- Tests verify all 42 bank/type pairs, both stream terminators, representative Room block data, and catalog forwarding across backend replacement.
- This is FX resource data plumbing only. The runtime FX type-to-directory index mapping, DSP command meaning, FX algorithms and physical receiver are still unresolved; the generic oscillator is still not the original sound engine.

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

 
## IC51 bulk-loader lookup inventory

- Added `MASSIC51BulkBlockDirectory` to parse all 256 candidate lookup positions for a selected IC51 bank using the firmware-observed `DPTR = 2 * R7` and four-byte read.
- The lookup positions are sliding windows, not independent four-byte records: adjacent positions overlap by two bytes. The catalog deliberately calls their aggregate lengths a *candidate byte-count sum*, not unique sample storage.
- Regression anchors for the real v1.05 image: bank 0 candidate sum 7,484,183 bytes with 111 source-address wraps; bank 1 candidate sum 4,574,119 bytes with no wraps. Entry `0x48` remains the independently traced startup transfer: bank 0 `0x3C64 / 0x354F`, bank 1 `0x295C / 0x2574`.
- This expands the audit surface for the 0xAF1A loader. It does not prove that all 256 indices are invoked, that the candidate lengths describe unique payloads, or that the copied data is PCM / ADPCM.

 
## FX selector range now matches IC4 firmware

- Disassembled the helper at `0x7FF2`: it reads `0x7BFE`, writes `0x3098`, and clamps values `>= 0x15` to `0x14` before the FX directory lookup at `0x8004`.
- The 21-entry directory is therefore indexed 0..20; 20 is `no Effect`. `MASSCommonControlStore`, Performance-bank loading, controller updates and saved-state restoration now normalize FX1/FX2 to this range.
- Regression coverage verifies both the endpoint 20 and clamping of 21 / 127 to 20.
- The current trace reads `0x7BFE=0x00` from an explicit synthetic test RAM overlay. This validates the firmware branch mechanics, not the real-world source mapping from Performance FX fields. Runtime FX selection and DSP effects are not yet implemented.

## FX1 / FX2 resources now use explicit processor names

- The owner's manual confirms paired 21-position effect menus: FX1 is the reverb/room/delay family, while FX2 is the chorus/flanger/phaser/panning/short-delay and special-effects family.
- IC4 `0x8004` reads the record's first selector and calls `0xAF93`, then reads the second selector and calls `0xB015`. The catalog fields and tests now identify these as FX1 and FX2 resources.
- Effect menu labels are one-based 1..21, whereas firmware uses zero-based table indices 0..20; both processor index 0 is the first menu type and index 20 is no Effect.
