# TechnoX audio-core finalization plan

Branch: `research/original-technox-audio-engine`  
Working mode: **finish and validate; do not expand low-level reverse engineering unless it blocks playback.**

## Current baseline

The project already has a substantial firmware/resource foundation:

- IC4 v1.05 executes in the in-project Intel 8032/MCS-51 model, with the MIDI receive path and bus/event tracing.
- IC51 program descriptors and command streams are decoded for all 512 bank/program entries, including both firmware flag variants.
- Program identity reaches each allocated voice; immutable program, external-memory and FX upload catalogs are passed to the audio backend.
- Firmware-side command planning and pitch/slot arithmetic projections produce evidence-backed packet bytes for covered paths.
- FX1/FX2 directories and upload byte streams are reconstructed separately.

**The original sound generator is not complete.** `BasicMASSVoiceBackend` still renders a diagnostic oscillator. The 16-KiB C000–FFFF image is a verified firmware memory-transfer result, not a decoded waveform library. The fitted synthesis chip/host-port bridge, larger waveform store, sample format and original DSP playback behavior are not yet verified. Recent pitch-probe changes also have no confirmed CI result at this checkpoint.

## Short completion path

### 1. Freeze the firmware investigation

Keep the recovered IC4, IC51, voice-routing, pitch and FX work as the baseline. Do not spend more time decoding isolated addresses unless the result directly unlocks sample playback, the host-device bridge or a required regression.

### 2. Establish one real waveform

A bounded-memory raw-image inspector is now in `tools/inspect_technox_waveform_dump.py`; its four unit tests are registered with CTest. It checks exact size when explicitly requested, CRC32/SHA-256 and byte distribution without making PCM or address-map assumptions. Acquisition requirements are documented in [`docs/research/WAVEFORM_MEMORY_ACQUISITION.md`](../docs/research/WAVEFORM_MEMORY_ACQUISITION.md).

Obtain a verified TechnoX waveform-memory dump or a trustworthy board/memory map and identify the actual sample device. Decode **one** known sample with its address, encoding, rate and loop boundaries. Add a small golden test before attempting the full library.

The IC51 descriptors and command streams are inputs to this work, not proof that their bytes are PCM.

### 3. Replace the diagnostic voice path incrementally

Keep the existing `IMASSVoiceBackend` boundary. Implement a separate resource-backed renderer for one voice and one verified waveform first; then add pitch, loop, velocity/envelope and controller behavior only where the firmware or hardware evidence supports it. Do not map the IC4 four-byte packets to SAM9407 registers by guesswork.

FX1/FX2 remain a separate completion item: upload resources are reconstructed, but their receiving device and original effect algorithms are not.

### 4. Release validation

- Run the current full CTest and Python regression suites against the final source revision.
- Produce x86_64 Intel macOS builds for AU, VST3 and Standalone.
- Verify the actual plugin artifacts on macOS Monterey and load the AU in Logic.
- Compare controlled notes and a small set of factory Performances against a physical TechnoX or a trusted reference capture.
- Keep any unmatched behavior explicitly listed; do not label the emulator authentic until sample playback and the original processing path are demonstrated.

## Completion gates

| Gate | Required evidence | State |
|---|---|---|
| Firmware/resource foundation | IC4, IC51 catalogs, voice routing and covered packet paths | Implemented in the branch; latest changes need final regression |
| One original waveform | Verified source memory plus decoded sample and golden vector | Blocked on waveform dump / memory evidence |
| Original voice renderer | One voice plays that waveform through a verified playback model | Not implemented |
| Original FX | Confirmed receiver/protocol and effect processing | Resource uploads only |
| Intel release | Passing final tests, AU/VST3/Standalone artifacts and Logic check | Must be revalidated on final revision |

## Practical stopping rule

The next major milestone is **one verified waveform sounding through a resource-backed voice**, not another round of address-by-address documentation. If the waveform store or board-level DSP interface is unavailable, the project can still be packaged as a working firmware/resource research build, but it must not be presented as an authentic-sounding TechnoX emulator.
