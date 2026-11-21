# TechnoX data model: program banks, Part bytes, and MIDI controls

## Single-sound names

`data/technox_ins.json` is generated from `Technox.ins` and contains four banks of 128 names (A-D, program numbers 0-127), 32 channel-10 drum-kit names, and 26 controller labels. These are metadata only, not sound data. The 512 single-sound names are independent of the 50 Performance names in `TECHNOX.MID`.

## Observed SysEx wire layout

Each Performance contains six messages in this order: name, common, Part 1, Part 2, Part 3, Part 4. In the complete message:

| Byte | Observed meaning |
|---:|---|
| 0 | F0 |
| 1–4 | Header: 3F 00 22 44 |
| 5 | Performance source index, 5–54 |
| 6 | Block type: 05 name, 00 common, 01–04 Part |
| 7 onward | Block data |
| Last | F7 |

The SMF event payload includes F7 but omits F0. Complete message sizes are 17 bytes (name), 74 bytes (common), and 33 bytes (each Part). JUCE `getSysExDataSize()` excludes both F0 and F7, so reconstructing with F0 + body + F7 produces those lengths.

The name block has a reserved zero at byte 7, followed by an 8-byte ASCII name at bytes 8–15.

## Part block observations

Each Part message contains 25 data bytes. Across 200 Part messages:
- data byte 0 is always zero in this fixture;
- data byte 1 takes values 0–3;
- data byte 2 takes values 0–127;
- the latter two values are consistent with selecting one of four single-sound banks (A-D) and a 0–127 program number from `Technox.ins`;
- remaining bytes stay opaque until their meanings are corroborated.

The bank/program interpretation is a strong structural hypothesis, not a verified hardware protocol mapping. No checksum or write command is inferred.

## MIDI controller labels

The .ins file lists: CC0 Bank Select; CC1 Modulation; CC5 Portamento Time; CC6 Data Entry; CC7 Volume; CC10 Pan; CC64 Hold Pedal; CC65 Portamento On/Off; CC67 Soft Pedal; CC80 FX1-Type; CC81 FX2-Type; CC82 Arp. Resolution; CC83 Arp. Speed; CC91 FX1-Send; CC93 FX2-Send; CC98-99 NRPN LSB/MSB (spelled NPRN in source); CC100-101 RPN LSB/MSB; and CC120-127 sound-off, controller reset, notes-off and Omni/Mono/Poly mode controls.

These are controller labels, not a complete parameter map. The .ins file does not specify NRPN parameter numbers or prove which internal synthesis parameter each controller changes. Do not bind them to Part-byte offsets without hardware captures or authoritative documentation.

## Editing model

`Source/TechnoXPartModel.h` retains the complete original Part SysEx message, exposes the candidate bank/program bytes, allows those two bytes to be edited with range checks, and serializes by replacing only those bytes. All other bytes remain untouched. The model does not transmit SysEx and does not implement sound synthesis.

## Parser correction

The parser uses the observed byte order (source index before block type), corrected complete-message lengths, and the reserved name byte. This is required to decode the original fixture and verify all 300 frames / 50 Performance groups losslessly.
