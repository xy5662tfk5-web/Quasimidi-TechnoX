# Quasimidi TechnoX / M.A.S.S. engine architecture research

## Status

This is an evidence-based architecture plan, not an implemented synthesizer. The current JUCE processor still intentionally clears its output. No audio engine, ROM sample decoder, or hardware-verified DSP implementation is present yet.

## Evidence hierarchy

### Confirmed for TechnoX by its owner manual and data files

- 16-part multitimbral operation.
- 512 single sounds arranged as four banks of 128 (A–D).
- A dedicated drum part/channel 10, 24 ROM drum maps in the instrument definition, and editable user drum-set locations.
- Two independent effect processors, FX1 and FX2, plus an arpeggiator.
- Performance mode stores four editable Part blocks per Performance. The SysEx address map identifies those four blocks as Parts 13–16.
- Each Part has 24 documented parameters, offsets 0x00–0x17.
- Each Performance common block has 65 documented bytes, offsets 0x00–0x40, including a 4-source × 8-destination modulation matrix, FX1/FX2 setup, and arpeggiator state.
- The repository contains two 128 KiB ROM dumps: IC4 and IC51. Their identities/checksums are documented in the ROM audit work. The existence of these files does not yet reveal the complete sound data format.

### M.A.S.S. family-level evidence (not a byte-accurate TechnoX DSP specification)

A 1994 Quasimidi Quasar review describes M.A.S.S. (Multi-Algorithm Sound Synthesis) as using selected samples processed through PCM, FM, and additive-synthesis techniques, with the design goal of producing analogue-style sounds without a conventional oscillator. A Sound On Sound review of the later Raven says its basic sound building blocks are held in the same way as the Quasar and TechnoX. This is good evidence for a shared synthesis family, but it does not disclose the exact TechnoX algorithm graph, coefficients, or ROM format.

Sources:
- The Mix, “Rhythm is a Quasar” (October 1994): https://www.muzines.co.uk/articles/rhythm-is-a-quasar/8203?theme=1
- Sound On Sound, “Quasimidi Raven”: https://www.soundonsound.com/reviews/quasimidi-raven
- Quasimidi TechnoX Owner’s Manual: https://www.deepsonic.ch/deep/docs_manuals/quasimidi_technox_manual.pdf

## Proposed software architecture

1. **Program and Performance data**
   - 512 named single-sound entries, bank/program lookup, 24 drum maps.
   - 50 factory RAM Performance records from TECHNOX.MID.
   - Four Part states (Parts 13–16) and one 65-byte common state per Performance.
   - Lossless original SysEx storage alongside typed, range-checked parameter values.

2. **ROM/sample subsystem**
   - Identify code vs. sample/data regions for each ROM.
   - Establish sample headers, address tables, sample format, rate, loop points, root key, and mapping to the 512 sound definitions.
   - Do not assume PCM bit depth, signedness, compression, endian order, sample rate, or sample boundaries until confirmed by ROM disassembly and statistical/aural validation.

3. **Voice engine**
   - A 21-voice allocator is the current reported TechnoX polyphony figure; confirm against primary hardware documentation or captures before hard-coding it.
   - Each active voice should own a sample source, playback cursor, pitch/tuning state, envelope state, modulation state, and routing to its Part.
   - Voice stealing, note priority, release-tail handling, interpolation, and pitch bend behavior remain unknown.

4. **M.A.S.S. processing layer**
   - Candidate functional blocks: PCM playback, pitch/time processing, FM/additive operators or partial generators, subtractive filtering, modulation, and amplitude shaping.
   - These are research targets based on family-level descriptions, not a claim that the exact order or implementation is known.
   - Part parameters expose cutoff, resonance, EG attack/decay/release, vibrato, modulation depth, and sensitivities. The common block supplies the modulation matrix.

5. **Part mixer and effects**
   - Per-Part level, pan, FX1 send, FX2 send.
   - Two independent effect engines. The manual enumerates effect algorithms and parameter pages; implement them as separate DSP units after the dry voice path is understood.
   - Performance common state stores FX activity/type/parameter pages and arpeggiator settings.

6. **MIDI and control**
   - MIDI note/velocity, pitch bend, modulation wheel, CC, NRPN/RPN, and SysEx.
   - Preserve MIDI event ordering and sample offsets in the JUCE process block.
   - Hardware SysEx output must remain disabled until device ID, address format, and round-trip behavior are verified.

## Unknowns that block faithful audio emulation

- Exact CPU/DSP chips and firmware execution model.
- Whether IC51 is a complete sample ROM, a partial region, or a data device with additional external/undumped content.
- Sample codec and sample rate(s), address tables, loop metadata, and multisample key/velocity zones.
- Exact PCM/FM/additive algorithms and their interaction.
- Filter topology, coefficient behavior, saturation, envelope curves, LFO shapes, modulation routing details.
- 21-voice allocation and stealing policy.
- Exact effect algorithms, internal sample rate, bit depth, and signal routing.
- Hardware output gain, DAC characteristics, noise, and nonlinearities.

## Implementation sequence

1. Finish and test the typed Part/common parameter models against the manual and the 50-Performance dump.
2. Disassemble and annotate IC4; separate executable code, tables, and references into IC51.
3. Determine whether IC51 contains raw samples or compressed/encoded data; build a reproducible extraction report.
4. Identify at least several known sounds by comparing extracted waveforms against controlled hardware recordings or trusted audio examples.
5. Implement one sample-playback voice and one dry Part path before adding effects.
6. Add voice allocation, envelopes, modulation matrix, filtering, FM/additive processing, and effects in independently testable stages.
7. Compare level, pitch, envelope, spectrum, and modulation behavior against the hardware across a small golden test set.

The aim is a faithful reconstruction supported by measurements, not a generic subtractive synth with similarly named controls.
