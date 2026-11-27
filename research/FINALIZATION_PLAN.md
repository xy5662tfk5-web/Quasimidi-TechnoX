# TechnoX audio-core finalization plan

Branch: `research/original-technox-audio-engine`
Working mode: **finish the authentic playback path; do not return to UI work or reopen already-verified controller research unless it blocks playback.**

## Current baseline

- IC4 v1.05 executes in the project's Intel 8032/MCS-51 model. Timed MIDI, Performance/Multi/SysEx paths and external bus traces are retained.
- IC51 descriptor and command-stream resolution covers all 512 bank/program pairs, including both firmware flag-0A decoder outcomes.
- Program identity reaches each voice; immutable IC51 program, external-memory and FX1/FX2 resource catalogs are handed to the active backend.
- Firmware-derived command packet builders and the 1C61 pitch pipeline cover their captured handler snapshots; IC4-to-unknown-audio-device register semantics remain deliberately unmapped.
- The processor currently selects `SAM9407VoiceBackend`, not the diagnostic oscillator. This is a candidate 16-bit sample renderer with interpolation, pitch stepping, one-shot and loop modes. SAM9407 has not been physically confirmed in the TechnoX board.
- The external waveform image loader and sample-map parser are in place. The sample map now supports multiple regions per program by MIDI key and velocity; mono/legato changes reselect the matching zone.
- Current generic playback supports only explicitly supplied signed 16-bit sample data and hand-authored/sample-builder maps. It does not decode factory IC51 resources into waveform addresses or factory key/velocity regions.

## Hard blocker: original waveform memory

The 16-KiB C000–FFFF image reconstructed from IC51 is a verified firmware transfer result, not a PCM bank. The public MAME TechnoX set listing contains only IC4 and IC51 controller ROMs and no waveform image. The contemporary hardware review reports 6 MB sample memory, while later secondary listings claim higher capacity; actual board population and layout must be verified before setting an expected dump size.

Publicly shared 94-hit Technox WAV collections are recorded audio references, not raw memory. They can support dry-hit comparison once a renderer works, but cannot substitute for the missing waveform image or factory instrument map. The KB6 archive states personal-use limitations; do not bundle or redistribute those files in the plugin.

## Next completion gates

1. **Identify the hardware path.** Readable markings on the fitted sound/DSP and sample-memory devices, plus board-side tracing of IC4 P2/P3, XDATA, and host strobes. Do not assume SAM9407 mappings from address resemblance.
2. **Acquire one real waveform image.** Capture actual memory using a documented programmer or bus method; record per-device images, exact sizes, repeat-read CRC32/SHA-256 and address/lane topology.
3. **Decode one factory waveform.** Establish encoding, byte order, address units, sample rate/clock, start/end and loop bounds from evidence. Add a byte-exact golden vector.
4. **Bind one factory program.** Trace one IC51 program/command sequence through the proven host bridge to one real sample region. Do not infer the association from sample names or a third-party WAV pack.
5. **Render and compare.** Verify one note and one factory Performance against a dry physical TechnoX capture. Only then expand mapping and the original per-voice filter/envelope/FX processing.

## Release validation

- CI must pass Linux/C++ and Python regressions and build x86_64 AU, VST3 and Standalone bundles for macOS. The actual AU must then be loaded in Logic on Monterey.
- Preserve current separation: controller firmware, unknown-device bridge, candidate SAM9407 host interface, external waveform memory and FX device are distinct until hardware evidence connects them.
- Keep every unverified component explicitly labelled. Do not describe the emulator as authentic-sounding until a factory waveform is rendered through a verified playback/DSP path.

## Completion gates

| Gate | State |
|---|---|
| IC4/IC51 firmware and resource foundation | Implemented; extensive regressions present |
| IC4-to-audio-device host protocol | Captured, not semantically decoded |
| Candidate 16-bit sample voice renderer | Implemented for explicit external maps; not factory-compatible yet |
| Key/velocity zones and mono/legato rebinding | Implemented; latest CI pending |
| One factory waveform + byte-exact decoder vector | Blocked on actual waveform-memory acquisition |
| Factory program-to-waveform mapping | Blocked on host bridge/sample-map evidence |
| Original DSP filters and FX algorithms | Not implemented/verified |
| Intel AU/VST3/Standalone release + Monterey Logic test | Previous Intel build passed; latest changes need CI and local Logic validation |

## Practical stopping rule

The next major milestone is **one verified factory waveform sounding from a factory program**, not more isolated address documentation or cosmetic work. Without raw waveform memory and a proven bridge/map, the project can be packaged as a strong firmware/resource research build, but must not be called an authentic TechnoX sound emulator.
