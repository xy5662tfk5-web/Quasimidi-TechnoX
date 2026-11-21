# TechnoX Part and controller research

## Confirmed instrument-definition facts

\`Technox.ins\` declares four single-sound banks A–D with 128 entries each (512 total), a separate 32-entry Channel 10 drum-kit table, 24 named drum-note maps, 40 bank/program-to-drum-map assignments, and 26 named MIDI controllers.

Program numbers are zero-based in the instrument file. The \`BankSelMethod=1\` field is preserved as authored; do not infer the actual MIDI Bank Select MSB/LSB values from this field alone.

## Controller interpretation

The structured metadata separates standard MIDI controller meanings from TechnoX-specific labels. CC 80–83 and 91/93 are vendor-labelled by the .ins file. The labels are evidence of intended functions, but their exact value tables, channel scope, and relationship to SysEx Part bytes remain unverified.

RPN/NRPN selectors (98–101) and Data Entry (6) are represented as MIDI control paths. A parameter can only be assigned after the corresponding RPN/NRPN number and value behavior have been established.

## Part SysEx observations

The supplied bank dump contains 200 Part frames: four 32-byte frames for each of 50 Performances. The empirical frame envelope is:

- bytes 0–6: \`F0 3F 00 22 44 <type 01..04> <performance index>\`
- bytes 7–30: 24 opaque 7-bit data bytes
- byte 31: \`F7\`

This is a structural observation from the supplied dump, not a complete official parameter map. The raw bytes must be retained exactly.

## Editing model

\`Source/TechnoXPartModel.h\` introduces a conservative editable Part model. It validates the observed envelope, exposes 24 raw byte slots, permits bounded 7-bit edits, preserves the original complete SysEx frame, and supports revert. It deliberately does not label any byte as oscillator, filter, envelope, effect, bank, or program until controlled device captures establish those semantics.

## Required next capture set

To identify fields safely, capture the same Part repeatedly while changing exactly one hardware control at a time. Keep the MIDI channel, Performance, selected Part, and all other controls fixed. For each pair, compare the full SysEx frame and incoming/outgoing CC/RPN/NRPN events. Repeat each change at minimum, midpoint, and maximum. A byte mapping is promoted to confirmed only when the change is repeatable and reversible.
