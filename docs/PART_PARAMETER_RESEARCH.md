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

The manual supplies the official 24-byte Part parameter map. The four Performance dump blocks are Parts 13–16. The parser retains every complete frame unchanged. Source: [Quasimidi TechnoX Owner’s Manual](https://www.deepsonic.ch/deep/docs_manuals/quasimidi_technox_manual.pdf), printed pages 32–38.


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
- NRPN selection uses CC99 (MSB), CC98 (LSB), then CC6 (Data Entry). Documented NRPN pairs include MSB 0x01 with LSB 0x08 pitch-mod speed, 0x09 pitch-mod depth, 0x0A pitch-mod delay, 0x20 cutoff, 0x21 resonance, 0x63 EG attack, 0x64 EG decay, and 0x66 EG release.
- Drum NRPNs use MSB 0x18 (pitch), 0x1A (level), 0x1C (pan), 0x1D (FX1 send), and 0x1E (FX2 send), with the note number as the LSB.
- RPN selection uses CC101 (MSB), CC100 (LSB), then CC6. Documented RPNs: 0x0000 pitch-bend range, 0x0001 fine tune, 0x0002 coarse tune.

These controller messages are control paths, not a promise that each CC is a direct write to a specific Part byte. The manual separately documents NRPN/RPN behavior and the Part SysEx parameter offsets.

## Editing model

\`Source/TechnoXPartModel.h\` introduces a conservative editable Part model. It validates the observed 33-byte envelope, skips the reserved byte at offset 7, exposes 24 named parameters with documented ranges and signed display offsets, permits bounded edits, preserves the original complete SysEx frame, and supports revert. Raw-byte access remains available so original bytes can be preserved and any future unknown extensions can be round-tripped. The plugin now embeds the generated four-bank catalog, resolves each Part's Bank Number and Sound Number, and displays the current Part's resolved single-sound name in the LCD area.

## Required next capture set

Hardware captures are still useful to validate how the front-panel editor emits parameter changes, which SysEx address bytes are sent for live edits, and whether the supplied factory dump uses any device-specific encoding not shown in the manual.
