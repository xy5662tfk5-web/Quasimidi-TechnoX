# Analysis of newly uploaded TechnoX files

Repository inputs inspected on 2026-11-21:
- `TECHNOX.MID` (Git blob SHA: `570b44dd0f98bcdf4ea7d662a5495b42ba294efe`)
- `Technox.ins` (Git blob SHA: `c698ac0ad295037bc1c61c2308f438c6ab82bd17`)

## TECHNOX.MID

This is a Standard MIDI File, format 0, one track, 384 ticks per quarter note, 11,803 bytes total. The track contains SysEx events and no channel voice/note events; it is a SysEx data transfer file, not a musical sequence.

Parsed contents:
- 300 complete SysEx frames.
- Exactly 50 performance records, indexed by the first record byte from 0x05 through 0x36 (decimal 5–54).
- Every record has six frames, with address-middle values in this order: 0x05 (8-character performance name), 0x00 (73-byte global/performance block), 0x01, 0x02, 0x03, 0x04 (four 32-byte part blocks). The first address byte identifies the Performance (0x05..0x36); the second address byte selects the block.
- Frame lengths including the final F7 byte: 16 bytes × 50, 73 bytes × 50, 32 bytes × 200.
- All SysEx payload bytes are 7-bit values; each frame ends with F7.
- The common SysEx prefix is `F0 3F 00 22 44`. The next two bytes are the Performance address (0x05..0x36) and block selector (0x00..0x05); the low address offset is implicitly zero in these bulk-dump frames, so the 24-byte Part payload begins at byte 7.

Performance names, records 5–54:
ArpPad, Fantasy, FatTekno, Obi_Pad, Passport, Killer, Saege, Spectize, Snowbird, Crunch, Taurin, RedBull, Natural, Koffein, SyntVoic, AnaStrng, OverPads, Padding, Piano!, HardCore, Strictly, FatSolo, Computer, ArpOdyx, Arounder, D_Mode, Blubber, BodyMoog, Sequenz, Syncers, Beltram, DeepSea, Aliens, Dionysos, DarkSide, Sweepoid, Foggy, OverDriv, Texture, Frusty, Arktis, Sirena, VeryDeep, ResoArp, Floating, ChillOut, Bonita, BanaBrss, Happy, Friday13.

This is strong evidence of a 50-performance SysEx bank dump with four part blocks per performance. It is suitable as a golden test fixture for the SysEx parser and future performance/part editor.

The owner manual independently documents the Performance address map and the 24 Part parameter offsets (printed pages 35–37). The observed 32-byte Part frames contain the 24 parameter values at bytes 7–30; the four selectors 0x01–0x04 represent Parts 13–16. Hardware round-trip testing is still required before enabling SysEx writes.

## Technox.ins

This is a text-based Cakewalk/Sonar-style instrument definition, not audio data or firmware. It describes:
- Four named program banks, A–D, each with 128 program-name entries: 512 single-sound names total.
- A separate channel-10 bank with 32 drum-kit names.
- 24 drum-map/key-map definitions, including Standard, 808/909, Analog, 606, CR-78, Linn, Rock, Modular, Kicks & Snares, Vintage, Dance, Noisy SFX, Old Box, Effects, Pan Effects, Beat Box, etc.
- A controller-name table including Bank Select, Modulation, Portamento, Data Entry, Volume, Pan, Hold Pedal, FX1/FX2 controls, NRPN/RPN and mode controls.
- The instrument definition selects banks via `BankSelMethod=1`, and maps bank/program/key definitions in the `[Technox]` section.

This independently corroborates the manual's advertised 512 single sounds and 24 drum sets. The file is useful as source data for a future Logic Pro MIDI device definition or a custom preset browser, but it does not itself contain the sound samples or synth engine.

## Recommended implementation order

1. Add a binary-safe SMF/SysEx fixture test for the 300 frames and 50 performance groups.
2. Implement a conservative decoder for the observed six-frame record structure, preserving unknown bytes.
3. Convert the four 128-entry program banks and drum maps from `Technox.ins` into structured metadata.
4. Only after this, wire preset selection and parameter editing into the audio engine.

The official owner manual maps the Part parameter offsets and MIDI control paths; see [PART_PARAMETER_RESEARCH.md](PART_PARAMETER_RESEARCH.md). No sample codec, waveform data, or full synthesis engine is identified by these two files.
