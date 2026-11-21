# TechnoX Part and controller research

## Confirmed instrument-definition facts

The extracted metadata is checked into `data/technox_ins.json`: four 128-entry single-sound banks (512 names), 32 Channel 10 drum kits, 24 drum maps, and 26 controller labels. The same file includes the manual-confirmed CC, NRPN, and RPN control paths used by the future editor.

\`Technox.ins\` declares four single-sound banks A–D with 128 entries each (512 total), a separate 32-entry Channel 10 drum-kit table, 24 named drum-note maps, 40 bank/program-to-drum-map assignments, and 26 named MIDI controllers.

Program numbers are zero-based in the instrument file. The \`BankSelMethod=1\` field is preserved as authored; do not infer the actual MIDI Bank Select MSB/LSB values from this field alone.

## Controller interpretation

The structured metadata separates standard MIDI controller meanings from TechnoX-specific labels. The owner manual confirms the functions of CC 80–83 and 91/93. The separate NRPN/RPN table documents the parameter-selection paths; not every controller is a direct write to a Part SysEx byte.

RPN/NRPN selectors (98–101) and Data Entry (6) are represented as MIDI control paths. The manual explicitly lists the NRPN/RPN parameter numbers and data-entry sequence; these are represented in the structured metadata.

## Part SysEx observations

The supplied bank dump contains 200 Part frames: four 33-byte complete frames for each of 50 Performances. The manual-confirmed frame envelope is:

- bytes 0–4: \`F0 3F 00 22 44\`
- byte 5: Performance source/address high byte (0x05..0x36, Performance 1–50)
- byte 6: block selector (0x01..0x04 for Parts 13–16)
- byte 7: reserved zero byte in this bulk dump
- bytes 8–31: 24 Part parameter bytes (offsets 0x00–0x17)
- byte 32: \`F7\`

The manual supplies the official 24-byte Part parameter map. The four Performance dump blocks are Parts 13–16. In Performance mode the manual specifies MIDI Omni: the same incoming note information is received on all 16 channels. The editor therefore defaults to Performance Omni and triggers the active layers selected by the Performance Common mode. An optional Part Channels mode follows the Sequencer Multi mapping (Part N ↔ MIDI channel N), defaulting the four stored blocks to channels 13–16. The parser retains every complete frame unchanged. Source: [Quasimidi TechnoX Owner’s Manual](https://www.deepsonic.ch/deep/docs_manuals/quasimidi_technox_manual.pdf), printed pages 32–38.


## Official Part parameter map (manual, printed page 37)

The manual maps the 24 data bytes directly to Part parameter offsets 0x00–0x17. In the supplied 33-byte complete bulk-dump frame these are bytes 8–31; byte 7 is a reserved zero byte.

| Offset | Parameter | Raw range | Display conversion |
|---:|---|---:|---|
| 00 | Bank number | 0–3 | 0=A, 1=B, 2=C, 3=D |
| 01 | Sound number | 0–127 | Zero-based program |
| 02 | Track mode | 0–3 | 0=muted, 1=poly, 2=mono, 3=lead |
| 03 | Level | 0–127 | — |
| 04 | Panorama | 0–20 | Off, L/R, random, key/velocity-follow |
| 05 | FX1 send | 0–63 | — |
| 06 | FX2 send | 0–63 | — |
| 07 | Transpose | 0–48 | raw − 24 semitones |
| 08 | Tune | 0–127 | raw − 64 |
| 09 | Cutoff frequency | 0–127 | raw − 64 |
| 0A | Resonance | 0–127 | raw − 64 |
| 0B | EG attack | 0–127 | raw − 64 |
| 0C | EG decay | 0–127 | raw − 64 |
| 0D | EG release | 0–127 | raw − 64 |
| 0E | Vibrato rate | 0–127 | raw − 64 |
| 0F | Vibrato depth | 0–127 | raw − 64 |
| 10 | Vibrato delay | 0–127 | raw − 64 |
| 11 | Velocity curve | 0–14 | Curve number |
| 12 | Hold pedal | 0–1 | 0=off, 1=on |
| 13 | Modulation depth | 0–127 | — |
| 14 | Pitch sensitivity | 0–24 | raw − 12 semitones |
| 15 | Volume modulation sensitivity | 0–127 | raw − 64 |
| 16 | Tone modulation sensitivity | 0–127 | raw − 64 |
| 17 | Portamento time | 0–127 | — |

The manual's address map identifies the four stored Performance Part blocks as Parts 13, 14, 15, and 16 (block selectors 0x01–0x04). The Performance index is byte 5 and the block selector is byte 6. Selector 0x00 is the common block; selector 0x05 is the Performance name. The Performance address high byte runs from 0x05 through 0x36 for the 50 factory RAM Performances.

## Documented MIDI controller paths

- CC0 Bank Select: values 0–3 select banks A–D; follow with Program Change 0–127.
- CC1 Modulation: routed through the modulation matrix; the manual describes the default route as tone/cutoff control.
- CC5 Portamento time, CC6 Data Entry, CC7 Volume, CC10 Panorama, CC64 Hold pedal, CC65 Portamento on/off, CC67 Soft Pedal.
- CC80/81 select FX1/FX2 type; CC82/83 control arpeggiator resolution/speed; CC91/93 are FX1/FX2 sends.
- NRPN selection uses CC99 (MSB), CC98 (LSB), then CC6 (Data Entry). Documented NRPN pairs include MSB 0x01 with LSB 0x08 pitch-mod speed, 0x09 pitch-mod depth, 0x10 pitch-mod delay, 0x20 cutoff, 0x21 resonance, 0x63 EG attack, 0x64 EG decay, and 0x66 EG release.
- Drum NRPNs use MSB 0x18 (pitch), 0x1A (level), 0x1C (pan), 0x1D (FX1 send), and 0x1E (FX2 send), with the note number as the LSB.
- RPN selection uses CC101 (MSB), CC100 (LSB), then CC6. Documented RPNs: 0x0000 pitch-bend range, 0x0001 fine tune, 0x0002 coarse tune.

These controller messages are control paths, not a promise that each CC is a direct write to a specific Part byte. The manual separately documents NRPN/RPN behavior and the Part SysEx parameter offsets.

## Editing model

\`Source/TechnoXPartModel.h\` introduces a conservative editable Part model. It validates the observed 33-byte envelope, skips the reserved byte at offset 7, exposes 24 named parameters with documented ranges and signed display offsets, permits bounded edits, preserves the original complete SysEx frame, and supports revert. Raw-byte access remains available so original bytes can be preserved and any future unknown extensions can be round-tripped. The plugin now embeds the generated four-bank catalog, resolves each Part's Bank Number and Sound Number, and displays the current Part's resolved single-sound name in the LCD area.

## Required next capture set

Hardware captures are still useful to validate how the front-panel editor emits parameter changes, which SysEx address bytes are sent for live edits, and whether the supplied factory dump uses any device-specific encoding not shown in the manual.


## MIDI control routing implemented in the editor/engine layer

- Part CC writes: CC0 Bank Select (0–3), CC5 Portamento Time, CC7 Level, CC10 Panorama (scaled to the manual's 0–20 range), CC64 Hold Pedal, CC91/93 FX sends, CC126 Mono and CC127 Poly Track Mode.
- Runtime Part controls: CC1 Mod Wheel source, CC65 Portamento On/Off, CC67 Soft Pedal, CC121 Reset All Controllers.
- Common Performance controls: CC80/81 FX1/FX2 type, CC82 Arpeggiator resolution, CC83 Arpeggiator speed. Factory values are extracted from the Common SysEx frame at offsets 0x2A, 0x32, 0x3C (low two bits) and 0x3D respectively.
- Part NRPN: MSB 0x01 with LSB values 0x08/0x09/0x10/0x20/0x21/0x63/0x64/0x66 for pitch modulation speed/depth/delay, cutoff, resonance and EG attack/decay/release.
- Drum NRPN: MSB 0x18/0x1A/0x1C/0x1D/0x1E, with NRPN LSB as MIDI key number, for pitch/level/pan/FX1 send/FX2 send.
- RPN: MSB 0x00 and LSB 0x00/0x01/0x02 for pitch-bend range/fine tune/coarse tune.
- CC120 is treated as All Sounds Off; CC123 as All Notes Off.

CC124 (Omni Off) and CC125 (Omni On) switch the plugin between Part Channels and Performance Omni routing. CC126/127 select Mono/Poly Track Mode. The DSP backend remains silent; these routes update bounded control state only.
