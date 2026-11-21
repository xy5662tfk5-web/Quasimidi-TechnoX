# TechnoX instrument-definition metadata

Generated from \`Technox.ins\` by \`tools/parse_technox_ins.py\`.

- Four single-sound banks: A, B, C, D; 128 program slots each (512 total).
- Program numbering is zero-based (0–127).
- Channel 10 has a separate 32-entry drum-kit table.
- The \`[Technox]\` section maps bank/program pairs to named drum-note maps.
- \`[Technox Controllers]\` lists controller names; names alone do not establish undocumented parameter ranges or SysEx offsets.

The generated JSON preserves the names and routing declarations in the instrument definition. It does not claim that a Performance Part's raw program bytes have been decoded. This requires correlating controlled SysEx captures with actual device state.
