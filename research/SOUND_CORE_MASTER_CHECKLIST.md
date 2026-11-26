# TechnoX original sound-core master checklist

Branch: `research/original-technox-audio-engine`
Purpose: one authoritative work queue for the original-audio reconstruction. This file separates proven firmware facts, unresolved hardware facts, implementation work, and validation. Update this checklist when a milestone is completed; do not reopen a completed item unless new evidence contradicts it.

**Current finishing mode:** prioritize one verified waveform, the first resource-backed voice, and Intel release validation. Freeze address-by-address research unless it directly blocks one of those results. The short ordered path and completion gates are in [`research/FINALIZATION_PLAN.md`](FINALIZATION_PLAN.md); the detailed open probes below remain reference material, not a reason to keep expanding scope.

## Ground rules

1. The target is the original TechnoX sound path, not a sound-alike.
2. Keep the working 8032/MCS-51 + MIDI + Part/Performance architecture intact.
3. Do not change the front-panel UI while working on the sound core.
4. Never assign DSP-register, sample, pitch, loop, envelope, filter, or FX meanings from packet patterns alone. Require a repeatable differential trace, ROM evidence, board documentation, or a hardware capture.
5. Keep the generic oscillator explicitly labelled as a test backend until a resource-backed original path replaces it.
6. Keep ROM parsing, catalog construction, and allocations off the audio callback.
7. Every new conclusion must include the ROM address/call path, test stimulus, captured bytes/registers, and a regression test or an explicit reason it cannot yet be tested.

## Verified foundation — do not duplicate

- IC4 v1.05 ROM audit and address descrambling are implemented; recorded MAME CRC32 values: IC4 `4d393cd9`, IC51 `d669570e`.
- Intel 8032/MCS-51 execution, timers, ACIA MIDI receive, IC4 bus integration, and two-bank IC51 access are implemented and regression-tested.
- Factory MIDI/SysEx parsing, 50 factory Performances, lossless 169-byte Performance images, four runtime Parts 13–16, and 512 bank/program resource entries are implemented.
- 21-slot voice manager, 16-Part state, MIDI CC/RPN/NRPN routing, Performance/Common and drum stores, and modulation framework exist.
- IC51 descriptor/resource command-stream decoding and both firmware flag variants are predecoded for all 512 programs.
- FX1 and FX2 have separate 21-entry directories, separate IC51 stream parsers, and 84 immutable upload plans across both banks.
- The IC4 bank-0 bulk transfer is reproduced byte-for-byte: IC51 source `0x3C64`, length `0x354F`, address mapping from IC4 `0xADDD`, final destination `0xF5B2`; the 16-KiB C000–FFFF startup image is plumbing, not decoded PCM.
- Per-voice bank/program identity and immutable program/FX/upload-plan catalogs reach the backend without parsing or allocation in the render callback.
- Firmware traces cover Note On/Off, velocity-sensitive command bytes, two-Part event expansion, IC51 nested-record copying, pitch interpolation operands, SoundHost writes/reads, and FF10 resource uploads.
- The 8032 firmware is emulated; the external sound processor and its original audio algorithms are not.
- DREAM SAM9407 is an unverified candidate for the TechnoX synthesis path. Public application references name TechnoX/Quasar/Raven, but only a readable board marking and pin-level connection can confirm the fitted device.
- ADSP-2105 is a separate candidate for FX1/FX2 based on Quasimidi-family reports; its presence and exact connection in this TechnoX unit are not yet physically confirmed.

## Current implementation truth

The external synthesis candidate under comparison is **DREAM SAM9407**; the separate FX candidate is **ADSP-2105**. Neither device is confirmed on the TechnoX board. Do not bind the emulator to either chip until board markings or a hardware capture establish the actual parts and bus mapping.

The following are still absent: a validated 8032-to-confirmed-sound-device host-port bridge, decoded full waveform store, original sample playback, proven original voice/filter/envelope behavior, and the separate original FX DSP implementation. `BasicMASSVoiceBackend` remains a generic diagnostic oscillator. Do not describe the plugin as an authentic-sounding TechnoX emulator until these components are implemented and compared with hardware/reference audio.

## Remaining work — ordered by dependency

### A. Finish the firmware-to-command data path

Status: **in progress / partial evidence**

- [ ] Decode the complete `0x09DA–0x0A55` fixed-point expression into neutral, width-accurate arithmetic stages, including carry, sign, truncation, saturation, and byte order.
- [ ] Run one-variable-at-a-time tests for event-record bytes `+1/+2/+3` and the source coefficient tables `0x28C8/0x2888/0x2898`, including the corresponding Slot-1 pages.
- [ ] Validate the direct/indirect IRAM-write probe added for `RAM[0x36/0x37]` in the isolated `0x044C–0x0550` consumer fixture. It now records address, old/new byte, direct vs `@Ri`, candidate PC/opcode bytes, caller, A/B/PSW/DPTR/R0–R7, P2/P3, machine cycle and shared sequence ID. Existing differentials show dependence on `0x2020+slot` and `0x2B61+slot`; the exact writer branch and complete producer chain remain open until the trace is checked by CI and the emitted opcodes are reviewed.
- [x] Compose group-08 pitch command decoding through the 1C61 lookup/accumulator projection, while requiring explicit snapshots for XDATA `0x2067`, the 1D2F address inputs and the direct-RAM bypass. The real factory stream runtime test executes a group-08 record; the still-unknown snapshot producers are not guessed.
- [x] Add the 1E4B slot-correction projection with explicit guard/register snapshots and a diagnostic comparison against the earlier 1C61 accumulator. The Note 66 downstream vector `D5:1F:A2` remains distinct from the pre-correction `D5:1E:02`; the R3 delta's producer remains open.
- [x] Compose the 09B9 interpolation arithmetic through the 0A4F register tuple and 0DB7 packet/mask/phase result. The Note 65 and Note 72 captured vectors are regression-tested; this does not imply a SAM9407 register map.
- [ ] Review the new Note 72 real-ROM snapshots at `0x1D2F/0x1D95/0x1D9D/0x1DC8/0x1E04` and XDATA `0x2067`; use the observed values to replace manually supplied 1C61 snapshot fields and resolve the Note 66 R3 correction delta. CI confirmation is pending.
- [ ] Trace the writers of velocity-sensitive R2 and the path through `0x1848 -> 0x2089–0x208E -> 0x210F/0x211F -> 0x0DB7`.
- [ ] Trace the origins and consumers of XDATA `0x2018/0x2019` and the alternative `0x2092–0x210F` route.
- [ ] Connect event IDs, 12-byte per-event records, nested IC51 descriptors, allocated Part pages, pitch interpolation records, and SoundHost writes into one end-to-end data-flow document.
- [ ] Separate values originating from real factory Performance/MIDI input from synthetic test overrides. Replace overrides with real producers where possible.

Exit condition: a deterministic Note On/Off/velocity/pitch/Part differential produces a fully documented, repeatable firmware data-flow trace up to the external-device boundary.

### B. Identify and model the external sound device

Status: **partially mapped at firmware level; physical device/protocol still unconfirmed**

- [x] Static IC4 provenance: reachable writer at `0x3829` sets IRAM bit `0x1B` iff SFR `0x8E == 1` at the check; `0xAF05` conditionally switches SFR `0x8E` from 1 to 2 for one `MOVX @R0,A), then restores 1.
- [x] Identify P3 control activity: P3.2 is cleared at `0x34EB`, `0xAF1A`, and `0xB34D`, and set at `0xAF73`; P3.1/P3.4/P3.5 are also actively sequenced around external reads. These are firmware-level observations, not confirmed pin functions.
- [x] Decode the host-port helper structure: `0x0DB7` writes R5 selector + R2/R3/R4 payload; `0x0DCE` synchronizes its internal phase bit to R4.7, emits `0x40/0xC0` on offset 3 only when the phase changes, then reads back offsets 1–3 into R2/R3/R4; `0x0E21` is a separate phase-code + selector operation.
- [x] Add P3 write provenance: port writes now retain PC, previous/new latch, changed mask, P2 and SFR `0x8E`; separate P3.2 control writes are retained even when the bit does not change. The real-ROM boot test asserts P3.2 write sites `0xAF1A` and `0xAF73`. Full CTest/unit-test workflow passed 29/29 steps.
- [ ] Establish the external semantics of the phase-triggered offset-2 read. Its return value is discarded by IC4 immediately, so it may be a read-side-effect handshake, but this must be verified rather than labelled ACK.

- [ ] Validate the offline merged trace collector and real-ROM fixture. Each retained IC51, host-window, FF10, external-sound-memory, P3 and SFR `0x8E` event now carries a machine-wide sequence ID; the merger sorts by `(machineCycle,eventSequence)`, and duplicate classifications of one physical write share the same ID (P3/P3.2 and mode-2 FF10/external-memory). The regression asserts nondecreasing cycle/sequence order. CI/build confirmation is still pending.
- [x] Add a firmware-side IC51 command-to-packet planner and connect it to the per-voice command cursor. Recovered branches return exact four-byte packet sequences, pass through the `0x0DB7` `R4 & 0xBF` mask, and expose per-packet/final bit-`0x0C` latch state; non-emitting branches preserve the latch. Pitch and context-dependent branches remain explicit. All 512 factory entries are classified under both `firmwareFlag0A` variants; CI validation is pending.
- [ ] Extend the report with focused boot/upload/Note On/Note Off/controller/FX windows and retain explicit labels for synthetic test responses. Same-cycle ordering is now recoverable from shared sequence IDs; the current fixture prints only the latest 256 merged entries.
- [ ] Separate boot/configuration, external-memory load, Note On, Note Off, pitch, controller, and FX traffic using isolated stimuli.
- [ ] Determine the four host-register meanings only from repeatable evidence; retain neutral register names until then.
- [ ] Establish ready/busy/ack behavior, read semantics, control-phase timing, and device selection. The `0x40/0xC0` writes to offset 3 are observed control-phase values, not yet decoded as ACK/BUSY. Current open-bus and synthetic-ready responses are test fixtures, not hardware behavior.
- [ ] Resolve P3.2 polarity/function. The C++ `soundDeviceResetReleased` label is a working compatibility hypothesis only: firmware clears P3.2 for the bulk transfer and memory test, and existing SoundHost contexts include P3.2-low states.
- [ ] Identify the fitted synthesis device from readable TechnoX board markings, schematics or a hardware capture, then map the 8032 host aperture/glue logic to its actual host interface. SAM9407 remains only one candidate until this is proven.
- [ ] Determine the external memory topology and the path to the advertised multi-megabyte waveform store. Do not treat the 128-KiB IC51 dump or the 16-KiB startup image as the complete sample ROM.

Exit condition: documented host protocol and memory topology with enough evidence to implement a device model without guessed register meanings.

### C. Recover waveform/sample representation

Status: **not implemented**

- [ ] Locate or obtain the actual waveform/sample memory dump(s), board memory map, or equivalent verified source.
- [ ] Determine sample encoding/codec, packing, endianness, address units, bank mapping, sample rate/clock, loop start/end, interpolation, and any compression.
- [ ] Decode a small set of known resources first and validate decoded waveform boundaries against hardware or a trusted reference.
- [ ] Map IC51 descriptors/command streams to waveform identifiers and per-voice playback parameters; distinguish descriptor bytes from waveform bytes.
- [ ] Build an offline decoder and golden test vectors before connecting it to real-time playback.

Exit condition: reproducible decoding of real waveform data with validated address/loop/rate metadata.

### D. Implement the original voice engine

Status: **architecture exists; original synthesis absent**

- [ ] Specify the actual voice/slot allocation rules and how the 21 firmware slots relate to sound-device voices.
- [ ] Implement sample address/start/stop, pitch increment, interpolation, loop, per-voice gain, velocity, envelopes, modulation and controller behavior from recovered evidence.
- [ ] Reconstruct original filter and modulation algorithms only when their coefficients, tables, and arithmetic are supported by firmware or hardware evidence.
- [ ] Replace `BasicMASSVoiceBackend` incrementally with a resource-backed backend behind `IMASSVoiceBackend`; retain the test oscillator only for diagnostics.
- [ ] Ensure deterministic rendering, bounded work, no allocation/locks in the audio callback, and correct voice stealing/retrigger behavior.

Exit condition: original sample-backed notes render with matching pitch, velocity, envelope, modulation, and polyphony behavior against a hardware/reference capture.

### E. Implement original FX1/FX2

Status: **resource upload metadata implemented; DSP absent**

- [ ] Trace real Performance/controller producers into the FX event dispatcher and prove how FX1/FX2 values reach the firmware handlers (remove synthetic selector assumptions).
- [ ] Identify the receiving device and determine whether FX streams configure the same processor or a separate DSP.
- [ ] Decode FX1/FX2 parameter streams and map each algorithm's parameters from firmware/UI state to the device.
- [ ] Implement the original algorithms or emulate the confirmed DSP behavior. Do not substitute generic JUCE Reverb/Chorus.
- [ ] Validate all 21 entries per family, including the `no Effect` index, with known reference settings.

Exit condition: FX1 and FX2 produce independently validated original behavior, not merely correct resource uploads.

### F. Integration and release validation

Status: **test backend builds; authentic engine not release-ready**

- [ ] Add golden tests for decoded waveform data, voice commands, playback timing, envelopes, pitch, modulation and FX.
- [ ] Run the full CTest/Python regression suite after CPU, bus, resource, or decoder changes.
- [ ] Build x86_64 Intel AU, VST3 and Standalone for macOS Monterey-compatible deployment; confirm the actual artifacts and load them in Logic.
- [ ] Compare controlled notes/Performances against a physical TechnoX or trusted recordings. Record what is and is not matched.
- [ ] Only then mark the sound engine authentic/complete.

## Immediate next sprint

1. Finish Section A's `0x09DA–0x0A55` arithmetic and `RAM[0x36/0x37]` producer trace.
2. In parallel, make a single canonical SoundHost transaction report that groups each four-byte packet with its complete per-byte bus context and the immediately preceding IC51/FF10 events.
3. Use that report to classify traffic by controlled MIDI stimulus; do not implement a guessed DSP protocol.
4. Audit available board photographs/manual evidence for chip markings and waveform-memory devices. If no readable markings or memory dump exists, record the exact missing physical evidence rather than filling the gap with a speculative chip model.
5. Once the command and hardware boundary is understood, define the smallest testable resource-backed playback milestone (one known waveform, one voice, one Note On/Off) before scaling to all programs.

## Evidence / documentation index

- `research/ORIGINAL_AUDIO_ENGINE_STATUS.md` — detailed verified checkpoint and trace history.
- `docs/research/VELOCITY_TO_COMMAND_BYTE_PATH.md` — velocity-sensitive arithmetic to packet-byte path.
- `docs/research/PITCH_INTERPOLATION_SLOT_TRACE.md` — pitch interpolation source triplets and arithmetic.
- `docs/research/NOTE65_TWO_PART_EVENT_TRACE.md` — event expansion and per-event records.
- `docs/research/MASS_RESOURCE_TO_VOICE_DISPATCH.md` — nested IC51 record transfer.
- `docs/research/SINE_SLOT1_SOUNDHOST_WRITER_TRACE.md` and `docs/research/SINE_SLOT_RECORD_ACCUMULATOR_TRACE.md` — slot-state to SoundHost writer evidence.
- `docs/research/FX_SELECTOR_DISPATCH_AND_UPLOAD_PLANS.md` — FX dispatch and upload-plan evidence.
- `docs/research/SOUND_DEVICE_BUS_SIGNAL_AUDIT.md` — SFR 0x8E/bit 0x1B provenance, P3 control-line sites, SoundHost/FF10/external-memory separation and Section B experiments.
- `tests/test_technox_8032_machine.cpp` — real-ROM firmware/MIDI/bus integration fixtures.
- `tests/test_mass_resource_rom.cpp` — IC51 descriptors, streams, catalogs, arithmetic and backend hand-off tests.

## Update protocol

For every new research session:
1. Read this checklist and the last section of `ORIGINAL_AUDIO_ENGINE_STATUS.md`.
2. Pick one unchecked item from the earliest incomplete dependency section.
3. State the evidence source and expected test before changing code.
4. Update the relevant detailed trace document, then this checklist.
5. Mark an item complete only when the test/evidence exists in the branch. Never count a planned investigation as implemented work.
