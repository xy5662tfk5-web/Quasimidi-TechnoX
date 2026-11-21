# Original TechnoX audio-engine reverse engineering

Status: research branch opened; no claim of original DSP emulation yet.

## Evidence already in the repository

- Verified v1.05 IC4 program image: 128 KiB, CRC32 `4d393cd9`, SHA-1 `9ee853f1e2f88fbfc86589c4d0f91003d0ff146f`.
- Verified v1.05 IC51 image: 128 KiB, CRC32 `d669570e`, SHA-1 `0645a1b022ed25a836152933e6df32da4448f49a`.
- MAME configures an I8032/MCS-51-family CPU and applies a 256-byte-block address descrambler to the **program** region only.
- The current MAME driver maps CPU program addresses `0x0000–0xFFFF` to the **first 64 KiB** of the 128 KiB IC4 region and explicitly leaves banking as `TODO`. It maps external data memory separately: RAM at `0x0000–0x38FF`, ACIA at `0xA000`, and LCD at `0xB000`. These are emulator mappings, not proof that the physical board has no additional bank logic.
- The TechnoX IC4 dump is identified by MAME as version `1.05b`; IC51 is a 128 KiB `nm27c010_v105` sample-region dump. The driver applies the same program-ROM descrambler to TechnoX and Quasar, but does not transform IC51.
- MAME marks TechnoX `NO_SOUND | NOT_WORKING`; it does not document or emulate the sound device, sample codec, or playback path.
- IC51 is declared as a samples region, but its 128 KiB dump does not reconcile with period descriptions of a much larger sample memory. It is not safe to treat it as the complete waveform library.
- The project already has the 50 factory Performance records, four Part blocks per Performance, 512 program names, the documented 24 Part parameters, and a working MIDI/audio prototype. Those control-layer assets do not identify the synthesis algorithms.

## First reverse-engineering pass

Run:

```bash
python3 tools/analyze_technox_rom.py
```

The utility:
1. Verifies the raw IC4/IC51 identities against known hashes.
2. Reproduces MAME's IC4 descrambling on a separate in-memory working image.
3. Prints the 8051 interrupt-vector bytes and candidate call/jump/DPTR references. These byte scans are only reconnaissance; the next task is instruction-boundary-aware control-flow disassembly from reset and interrupt vectors, with explicit treatment of the unresolved upper 64 KiB/banking question.
4. Reports IC51 raw non-0xFF spans without assigning them sample boundaries.
5. Can optionally write a descrambled IC4 working copy with `--write-descrambled-ic4 PATH`.

The byte-pattern scan is deliberately labelled as candidate data, not disassembly: code/data separation and instruction boundaries still need validation.

## Work sequence toward an authentic engine

### A. Identify the sound hardware
- Obtain readable PCB photos of both sides and every IC marking.
- Identify the sound-generation ASIC/DSP, sample ROM/RAM, DAC, clock and memory buses from chip markings and board traces.
- Reconcile the advertised sample capacity with the complete physical ROM inventory. Do not substitute Quasar/Raven ROMs for TechnoX.

### B. Reconstruct firmware access to the sound subsystem
- Build a validated MCS-51 disassembly of descrambled IC4, including bank/window handling.
- Trace reset and interrupt paths, ACIA/MIDI receive routines, program/bank changes, note-on/off, controller handling, and all external-memory or I/O accesses.
- Identify sound-chip register writes, sample address setup, voice allocation, and memory banking.
- Cross-check each inferred register with PCB traces, chip datasheets, or captures. Opcode-byte matches alone are not proof.

### C. Decode waveform storage
- Establish the complete set of sample ROMs and their address mapping.
- Determine codec, signedness, bit depth, sample rates, loop points, interpolation and any compression from code plus known-note captures.
- Build a read-only sample extractor and test it against short, dry recordings from real hardware.

### D. Implement the original signal path
- Replace the temporary generic oscillator backend with a documented sample/algorithm backend.
- Implement voice allocation (manual specifies 21-voice polyphony), four Performance Parts 13–16, per-Part pitch/level/pan/filter/envelopes/modulation, then effects and arpeggiator only when their behavior is established.
- Keep uncertain algorithms behind explicit interfaces and label approximations; never silently substitute generic DSP and call it original.

### E. Validate against hardware
For each reference capture, preserve the exact MIDI sequence, velocity, CC/NRPN/RPN, Performance, sample rate and dry/wet state. Compare pitch, waveform, envelope, loop, filter response, modulation, stereo position, FX and voice stealing.

## Immediate blockers / evidence needed

The most valuable missing evidence is a complete, sharp photo set of the TechnoX main PCB (front and back, full board plus close-ups of every IC marking) and any additional sample ROM dumps not represented by IC4/IC51. If the user has no physical unit, continue with IC4 firmware analysis and treat the IC51 decode as unresolved.

## Non-goals for this phase

- Do not apply the IC4 descrambler to IC51.
- Do not infer PCM/ADPCM format from entropy or byte histograms alone.
- Do not assume the 128 KiB IC51 image is the complete factory sound library.
- Do not call the current sine-wave test backend an authentic TechnoX sound engine.
- Do not merge research or DSP branches into the user's main working branch without approval.
