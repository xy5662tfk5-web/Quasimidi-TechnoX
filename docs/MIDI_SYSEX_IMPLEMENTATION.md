# MIDI / SysEx protocol implementation notes

Based on the TechnoX Owner's Manual, sections 17–19 (printed pages 32–39).

## Implemented protocol primitives

- Manufacturer ID: `3F`; TechnoX model ID: `22`.
- Request packet: `F0 3F dv 22 52 ah am al dh dl F7`.
- Dump packet: `F0 3F dv 22 44 ah am al data... F7`.
- Address and count bytes are 7-bit values; request count is represented as two 7-bit bytes.
- Dump payload is validated as 7-bit data. The documented request/dump packets have no checksum field.
- Parser rejects malformed framing, wrong IDs/opcodes and high-bit bytes in the data section.

Header-only helpers are in `Source/TechnoXSysEx.h`: `parseRequest`, `parseDump`, `makeRequest`, `makeDump`.

## MIDI state model

`Source/TechnoXMidi.h` captures documented Bank Select (CC0), Program Change, Modulation (CC1), Volume (CC7), Pan (CC10), Hold Pedal (CC64), NRPN/RPN selector bytes (CC98–101), Data Entry (CC6), and Pitch Bend for all 16 channels.

The manual says there are 512 single sounds in four banks, selected by Bank Select 0–3 followed by Program Change 0–127. Part 10 is the normal drum channel. This state model is not yet connected to an audio voice engine or the complete NRPN parameter map.

## Not yet implemented / requires verification

- Full SysEx memory-address map dispatch and data model for every performance, multisetup, drumset and temporary block.
- Identity reply: the manual's printed byte numbering around the firmware-version field appears internally inconsistent; verify against a captured hardware reply before emitting one.
- Full NRPN/RPN parameter write-through and persistence.
- Audio synthesis and sample decoding.

## Source

TechnoX Owner's Manual, PDF sections 17–19, especially printed pages 32–38:
https://www.bn1studio.co.uk/wp-content/uploads/2022/05/quasimidi-technox-manual-478196.pdf
