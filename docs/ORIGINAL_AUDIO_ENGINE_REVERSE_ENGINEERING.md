# Original TechnoX audio-engine reverse engineering

Status: research branch opened; no claim of original DSP emulation yet.

## Evidence already in the repository

- Verified v1.05 IC4 program image: 128 KiB, CRC32 `4d393cd9`, SHA-1 `9ee853f1f2e88fbfc86589c4d0f91003d0ff146f`.
- Verified v1.05 IC51 image: 128 KiB, CRC32 `d669570e`, SHA-1 `0645a1b022ed25a836152933e6df32da4448f49a`.
- MAME configures an I8032/MCS-51-family CPU and applies a 256-byte-block address descrambler to the **program** region only.
- The current MAME driver maps CPU program addresses `0x0000–0xFFFF` to the **first 64 KiB** of the 128 KiB IC4 region and explicitly leaves banking as `TODO`. It maps external data memory separately: RAM at `0x0000–0x38FF`, ACIA at `0xA000`, and LCD at `0xB000`. These are emulator mappings, not proof that the physical board has no additional bank logic.
- The exact MAME external-data map is: `0xA000–0xA001` ACIA writes, `0xA002–0xA003` ACIA reads; `0xB000–0xB001` LCD writes, `0xB002–0xB003` LCD reads. MAME also marks `0x3900–0x3903`, `0x390B`, `0x7C00–0x7FFF`, `0x8000–0x8003`, `0xC000–0xC0FF`, and `0xFF10` as no-op/unimplemented accesses. These latter windows are worth checking against firmware before assigning any hardware function.
- The TechnoX IC4 dump is identified by MAME as version `1.05b`; IC51 is a 128 KiB `nm27c010_v105` sample-region dump. The driver applies the same program-ROM descrambler to TechnoX and Quasar, but does not transform IC51.
- MAME marks TechnoX `NO_SOUND | NOT_WORKING`; it does not document or emulate the sound device, sample codec, or playback path.
- IC51 is declared as a samples region, but its 128 KiB dump does not reconcile with period descriptions of a much larger sample memory. It is not safe to treat it as the complete waveform library.
- The current raw IC51 scan finds 1,055 non-`0xFF` spans of at least 32 bytes; the first is `0x00000–0x023B5` (9,142 bytes) and the last is `0x1AAFC–0x1AE03` (776 bytes). These are occupancy runs only—not sample boundaries, codec blocks, or evidence of a specific waveform format.
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

## MCS-51 disassembler (first implementation)

`tools/disassemble_technox_8051.py` now performs reachability-guided decoding from reset entry `0xEB32` plus the interrupt-vector entries that MAME maps into the first 64 KiB. It follows direct LJMP/AJMP, LCALL/ACALL and relative conditional branches, records call/branch targets, and lists decoded MOVX instructions. The current run is static analysis: computed jumps (`JMP @A+DPTR`), jump tables, indirect calls, runtime bank switching and data/code overlap remain unresolved. An apparent MOVX instruction is not by itself proof that it accesses the MIDI ACIA or sound hardware; the DPTR/Ri value and surrounding control flow must be traced.

CI writes a text listing artifact named `technox-ic4-mcs51-disassembly`. The next analysis pass must annotate direct/indirect DPTR setup reaching each MOVX site, inspect ACIA access paths around `0xA000`, and correlate call graphs with MIDI status handling. The sound-chip address map remains unknown until code evidence or hardware documentation establishes it.

### Confirmed MIDI and LCD access paths

The reachable code now gives instruction-level confirmation of the MAME device map:

- ACIA initialization at code `0xA7A0` loads DPTR with `0xA000` and performs two writes (`0xFF`, then `0x96`). A nearby routine at `0xA7B3` reads ACIA status at `0xA002`, polls bit `0xE1`, then writes the byte in R7 to `0xA001`. A separate routine at `0xA7C0` reads `0xA003` into R7. This is consistent with serial transmit/receive through the mapped 6850 ACIA. The called helper routines still need semantic analysis before interpreting the control bytes.
- LCD initialization at `0xA601` writes command `0x38` to `0xB000`. Other routines write through `0xB000`/`0xB001`; the status routine reads `0xB002` and polls bit `0xE7`. This matches a command/data/busy-flag LCD interface.

### Candidate undocumented external hardware window

Firmware also performs actual `MOVX` reads/writes in the range MAME leaves as no-op:

- At `0x7CC1`, it reads from `0x7C00`, then reads `0x7C07`; values are copied into internal RAM locations `0x309D` and `0x309B`.
- At `0xB630`, it writes `0x01` to `0x7C05`, increments DPTR, then writes `0x00` to `0x7C06`.
- At `0x877C`, it reads `0x7C06`.

This `0x7C00–0x7FFF` window is a stronger hardware-reverse-engineering lead than raw byte matches because the firmware performs real external-memory transactions there. It could be a peripheral/register interface, but its function is **not identified** and must not yet be called the sound chip. The separate `0xC000` routine writes `0xAA` and reads it back, apparently probing an external location; it also remains unexplained.

A computed jump at code `0xC0B3` loads DPTR with `0xC0BA` and executes `JMP @A+DPTR`. This is a **program-code jump table**, not an external-data access. The analysis distinguishes such code pointers from DPTR values followed by `MOVX`, avoiding false hardware-register classifications.

### First external-memory / I/O cross-reference

The first reachable-code pass finds an immediate DPTR setup to `0xA000` at code address `0xA7A0`, and three setups to `0xB000` at `0xA601`, `0xA654`, and `0xA669`. These addresses match the MAME driver's ACIA and LCD mappings respectively. This is strong evidence that reachable firmware code references both mapped devices. It does **not yet** prove which MOVX instructions use those values: the next pass prints the local instruction context and must follow DPTR through each basic block and any calls before assigning reads/writes to the ACIA. The sound-generation interface is still unidentified.

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
