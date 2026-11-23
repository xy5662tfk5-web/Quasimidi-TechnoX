# M.A.S.S. sound-engine architecture proposal

Status: architecture proposal only. No audio synthesis is implemented by this document.

## 1. Evidence boundary

### Confirmed from the TechnoX manual and current project data

- M.A.S.S. is expanded as **Multi Algorithm Sound Synthesis**.
- The instrument is 16-part multitimbral and has a stated 21-voice polyphony.
- It exposes 512 single sounds in four banks of 128, plus 32 named Channel 10 drum kits and 24 drum-note maps in the supplied instrument definition.
- The Performance dump contains 50 records. Each record has a common block and four stored Part blocks; the manual's address map identifies those blocks as Parts 13–16.
- The manual documents 24 Part parameters, FX1/FX2 sends and type controls, arpeggiator controls, modulation, and CC/RPN/NRPN control paths.
- The current ROM evidence is incomplete for audio reconstruction. MAME lists a 128 KiB program ROM and a 128 KiB samples-region ROM, while period references describe a much larger sample-memory capacity. MAME does not emulate the sound engine.

### Not yet established

- The internal meaning and implementation of each M.A.S.S. synthesis algorithm.
- Whether every sound uses samples, generated waveforms, or a hybrid signal path.
- The IC51 sample codec, sample directory, sample boundaries, rates, loop points, and complete sample inventory.
- Exact filter models, envelope curves, modulation routing defaults, voice-stealing policy, effect algorithms, and arpeggiator timing.
- Whether all 21 voices are identical or have resource-specific restrictions.

The engine must not hard-code guesses for these unknowns.

## 2. Proposed module boundaries

    MASSAudioEngine
    ├── MidiEventRouter
    │   ├── Channel / Part routing
    │   ├── CC / RPN / NRPN decoder
    │   └── SysEx state commands
    ├── PerformanceState
    │   ├── 16 PartState objects
    │   ├── CommonState
    │   └── DrumState
    ├── SoundCatalog
    │   ├── 4 x 128 single-sound names
    │   ├── SoundDefinition (future verified data)
    │   └── DrumKit / DrumMap catalog
    ├── VoiceManager (21 voice slots)
    │   ├── VoiceAllocator
    │   └── Voice instances
    ├── SynthesisBackend
    │   ├── AlgorithmFactory (algorithm IDs not yet known)
    │   ├── SampleSource (codec not yet known)
    │   ├── ModulationMatrix
    │   ├── FilterStage
    │   └── Envelope / amplifier stages
    ├── Arpeggiator
    ├── EffectRack
    │   ├── FX1
    │   └── FX2
    └── MasterMixer

This is a proposed software decomposition, not a claim about the original hardware's exact internal circuit or firmware structure.

## 3. Realtime audio path

1. Receive timestamped MIDI events from the JUCE wrapper.
2. Route notes and controls to the appropriate Part using the documented channel/Part configuration.
3. Apply Program Change, Bank Select, CC, RPN/NRPN and Performance state changes outside the inner sample loop where possible.
4. Allocate or release one of the 21 voice slots.
5. Ask the selected synthesis backend to render the active voice.
6. Apply per-Part level, pan, modulation and FX sends.
7. Sum all active voices and drum events into the master bus.
8. Process FX1/FX2 and produce the output buffer.

The realtime callback must not allocate memory, perform file I/O, block on a mutex, or parse JSON/SysEx. All catalogs, ROM/sample data and DSP state are prepared before audio starts. Control-thread changes should reach the audio thread through bounded lock-free event queues or atomically published immutable state.

## 4. State model

- **PerformanceState** owns the selected factory/user Performance and its common block.
- **PartState[16]** owns the documented 24 parameter values plus the original opaque SysEx bytes for lossless round-trip.
- **SoundCatalog** resolves the pair (bank 0..3, program 0..127) to the 512 names. Actual sound definitions remain a separate resource from names.
- **DrumState** resolves the drum program and key map; do not conflate the 32 kit names with the 24 note maps.
- **VoiceState** stores note, velocity, channel/Part, allocation age, envelope state, pitch and backend-specific state.

The current PartEditModel is a control/data model. The processor now keeps editable values for all 50 × 4 factory Performance Part blocks, can rebuild an edited Part SysEx frame while preserving its original opaque bytes, and mirrors the selected Performance's four stored Parts into engine Part slots 13–16. The plugin defaults to Performance Omni, as specified by the manual; the Common Performance mode selects the active Parts 13–16. An optional Part Channels mode follows the Sequencer Multi mapping, where Part N corresponds to MIDI channel N.

## 5. Synthesis backend strategy

The predecoded IC51 catalog is now passed to the active voice backend as an immutable read-only resource. Each voice already captures its bank/program identity at Note On, so a future backend can resolve the exact firmware-parsed descriptor and command stream without parsing ROM data or allocating in the audio callback. Backend replacement preserves this catalog connection. This closes the resource-data handoff only; it does not decode waveform samples or establish the M.A.S.S. synthesis algorithm.


The first infrastructure is now present: a 21-slot VoiceManager, a lock-free 16-Part parameter store, per-key Drum parameter storage, per-Part Mod Wheel/Portamento/Soft Pedal/RPN state, Performance Common FX/arpeggiator controls, a generic ModulationMatrix, and an IMASSVoiceBackend interface. Documented CC/NRPN/RPN routes now update these stores, including separate Part NRPN, per-key Drum NRPN and Common FX/arpeggiator CC paths. Factory FX/arpeggiator defaults are extracted from the Common SysEx frame using the manual's offsets. The current NullMASSVoiceBackend intentionally emits silence. Use this interface so verified sound-generation models can be added without changing MIDI routing, Part editing, voice allocation, or the JUCE wrapper.

A future backend should support:
- preparation from verified sound/sample resources;
- note-on, note-off and per-voice controller updates;
- deterministic rendering for a fixed sample rate and block size;
- voice reset/release and state serialization;
- optional per-voice modulation and filter stages.

Do not select a codec or synthesis algorithm from ROM entropy alone. First identify the complete ROM set, decode candidate sample structures, and compare dry reference recordings from a real TechnoX.

## 6. Effects and modulation

The manual confirms FX1/FX2 selection and send controls, modulation and arpeggiator CC paths, and a set of NRPN parameters. The first implementation should therefore separate:
- per-Part send levels and effect-type selection;
- effect algorithms and their parameter state;
- modulation sources and destinations;
- arpeggiator clock, resolution and speed.

Exact effect algorithms and modulation defaults remain data to be verified. Use neutral placeholders rather than assigning arbitrary sound to unknown effect IDs.

## 7. JUCE integration

Keep the DSP engine independent of JUCE UI classes. The JUCE processor should:
- own one MASSAudioEngine;
- convert host MIDI events to internal timestamped events;
- pass the host sample rate and block size to prepare;
- render audio through a single processBlock entry point;
- expose the 50 Performances and 16 Parts to the editor;
- serialize plugin state without serializing runtime pointers or sample buffers.

The Standalone, AU and VST3 wrappers should share the same engine and state model.

## 8. Implementation milestones

1. **Data contract:** finish tests for Performance, Common and Part import/export; verify all 50 records round-trip byte-for-byte.
2. **Catalog:** bind the 512 names to the Part model's bank/program fields and expose names to the editor.
3. **MIDI controls:** implement the documented CC/RPN/NRPN event decoder and unit-test value conversions.
4. **Hardware evidence:** collect single-control SysEx captures and dry audio/MIDI references.
5. **ROM/sample format:** establish the complete sample inventory and decoder before writing a playback engine.
6. **Single voice:** implement one verified synthesis path and compare it against the hardware.
7. **Polyphony and multitimbrality:** add 21-voice allocation and 16-Part mixing.
8. **Effects and arpeggiator:** implement only after their algorithms and timing are supported by the manual or captures.
9. **Parity testing:** render repeatable test notes and compare levels, pitch, envelopes, modulation, effects and voice stealing against the real instrument.

## 9. Immediate next engineering task

The first Catalog integration is now in place: the plugin embeds data/technox_ins.json, resolves the Bank Number and Sound Number fields through SoundCatalog, and displays the selected Part's single-sound name on the LCD. The next task is to expose all four Part states in the editor and connect parameter edits to the documented CC/RPN/NRPN paths. This remains control-layer work; the audio engine is still not implemented.
