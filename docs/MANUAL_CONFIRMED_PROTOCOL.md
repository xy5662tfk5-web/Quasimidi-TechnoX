# TechnoX manual-confirmed MIDI and SysEx protocol

Source: Quasimidi TechnoX Owner's Manual, English edition, 53 pages.
Relevant manual pages: MIDI controllers pp. 32-33; SysEx format and address map pp. 34-37; MIDI implementation p. 39; technical specifications p. 51.

This document records facts stated in the manual and distinguishes them from observations in the uploaded SysEx dump and firmware reverse engineering.

## 1. Hardware capabilities stated by the manual

- Synthesis system: MASS (Multi Algorithm Sound Synthesis).
- Polyphony: 21 voices.
- Multitimbrality: 16 Parts.
- LCD: 2 rows x 16 characters.
- Three MIDI DIN connectors: In, Out, Thru.
- The manual does not identify the synthesis ASIC/DSP, sample codec, sample-ROM inventory, or internal voice-register protocol.

The 21-voice and 16-Part figures are official product specifications. They do not reveal the internal voice-allocation algorithm.

## 2. SysEx framing

The documented TechnoX manufacturer/device bytes are:

- Manufacturer ID: `3F` (Quasimidi).
- Device ID: `22` (TechnoX).
- Request-data command: `52`.
- Dump-data command: `44`.
- Address: three 7-bit bytes `ah am al`.
- Dump payload bytes are 7-bit data and the message ends with `F7`.

Documented dump shape:

```
F0 3F dv 22 44 ah am al dt... F7
```

Request shape:

```
F0 3F dv 22 52 ah am al dh dl F7
```

The identity request/reply uses the universal non-real-time identity message; the reply contains Quasimidi ID `3F`, TechnoX ID `22`, and a four-character ASCII firmware version.

## 3. Address map and the uploaded 50-Performance dump

The manual's address map (p. 35) documents:

- `00 00 00`: system parameters.
- `01 00 00`: temporary Common block.
- `01 01 00` through `01 10 00`: temporary Part 1–16 blocks.
- `01 11 00`: temporary Performance name.
- `02 00 00` through `02 3D 00`: temporary drum-instrument parameters.
- `02 7E 00`: drumset number; `02 7F 00`: drumset name.
- Performance memories occupy high address values `05` through `36`, each with Common, four stored Part blocks (Parts 13–16), and a name.
- Multisetups occupy `37` through `44`.
- User drumsets occupy `45` through `4C`.
- Sound-name banks occupy `69` through `6C`; the manual marks these name entries as request-only.

This matches the independently parsed `TECHNOX.MID` dump at the structural level: 50 records indexed `0x05–0x36`, each with six frames in the order name, Common, Part 13, Part 14, Part 15, Part 16. The dump contains 300 complete SysEx frames in total.

Important: the dump validates the observed record grouping and ordering. It does not prove every undocumented byte meaning, checksum rule, or safe write behavior. Preserve unknown bytes and do not send experimental writes to hardware.

## 4. Part parameter offsets (manual p. 37)

| Offset | Parameter | Documented range |
|---|---|---|
| `00` | Bank number | 0–3 |
| `01` | Sound number | 0–127 |
| `02` | Track mode | 0 muted, 1 poly, 2 mono, 3 lead |
| `03` | Level | 0–127 |
| `04` | Panorama | 0–20 |
| `05` | FX1 send | 0–63 |
| `06` | FX2 send | 0–63 |
| `07` | Transpose | 0–48, centred at 24 (-24…+24) |
| `08` | Fine tune | 0–127, centred at 64 (-64…+63) |
| `09` | Cutoff frequency | 0–127, centred at 64 |
| `0A` | Resonance | 0–127, centred at 64 |
| `0B` | EG attack | 0–127, centred at 64 |
| `0C` | EG decay | 0–127, centred at 64 |
| `0D` | EG release | 0–127, centred at 64 |
| `0E` | Vibrato rate | 0–127, centred at 64 |
| `0F` | Vibrato depth | 0–127, centred at 64 |
| `10` | Vibrato delay | 0–127, centred at 64 |
| `11` | Velocity curve | 0–14 |
| `12` | Hold pedal | 0–1 |
| `13` | Modulation depth | 0–127 |
| `14` | Pitch sensitivity | 0–24, centred at 12 (-12…+12) |
| `15` | Volume modulation sensitivity | 0–127, centred at 64 |
| `16` | Tone modulation sensitivity | 0–127, centred at 64 |
| `17` | Portamento time | 0–127 |

The exact display labels and curve names should be taken from the relevant manual tables when implementing the UI.

## 5. MIDI controllers and NRPN/RPN

Documented standard controllers include Bank Select (CC0), Modulation (CC1), Portamento Time (CC5), Data Entry (CC6), Volume (CC7), Pan (CC10), Hold Pedal (CC64), Portamento On/Off (CC65), Soft Pedal (CC67), FX1/FX2 type (CC80/81), arpeggiator resonance/speed (CC82/83), FX sends (CC91/93), and NRPN/RPN selectors (CC98–101). The manual also lists All Sounds Off, Reset Controllers, All Notes Off, Omni Off/On, Mono and Poly mode messages.

NRPN mappings documented by the manual include pitch modulation speed/depth/delay, cutoff, resonance, EG attack/decay/release, and per-drum pitch, level, pan and FX sends. RPN mappings include pitch-bend range, fine tune and coarse tune.

## 6. Consequences for the emulator

1. The manual now supplies an authoritative control/protocol specification. The manual should no longer be treated as a missing prerequisite.
2. The parser can be made address-aware: decode record address + offset into typed Common/Part/Drum/System models, while preserving unrecognized bytes.
3. The four Performance Part blocks are specifically Parts 13–16; this is not a generic four-Part subset.
4. MIDI handling should implement the manual's CC/NRPN/RPN behavior and distinguish Performance mode from 16-channel Sequencer Multi mode.
5. The manual confirms the MASS name and advertised 21-voice architecture, but not the internal synthesis algorithms or chip-level register interface. Firmware tracing and PCB/IC identification remain necessary for an authentic sound engine.
6. Do not mistake the 128 KiB IC51 MAME sample-region dump for the complete waveform library. The manual does not resolve the larger sample-memory-capacity discrepancy.

## 7. Evidence boundaries

- **Manual-confirmed:** advertised architecture, MIDI commands, SysEx framing, address map, parameter offsets and ranges.
- **Dump-confirmed:** the uploaded file contains 50 six-frame Performance records, with four stored Part blocks per record.
- **Firmware-confirmed:** MCS-51 code accesses the undocumented `0x7C00–0x7FFF` external window; its device identity and register meanings remain unknown.
- **Not established:** DSP/ASIC identity, PCM/ADPCM format, waveform address map, exact 21-voice allocation, sample memory completeness, and the meaning of the `0x7Cxx` registers.
