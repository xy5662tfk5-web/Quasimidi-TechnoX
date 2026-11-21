# TechnoX Performance Manager

## Added foundation

- tools/parse_technox_mid.py extracts complete SysEx frames from a Standard MIDI File, validates the observed TechnoX message layout, groups 50 records, and exports lossless JSON.
- tests/test_parse_technox_mid.py tests complete-frame extraction and rejection of incomplete SysEx.
- Source/TechnoXPerformanceBank.h provides the C++ UI-facing bank model: count, current record, lookup, and selection by list index or original source index.

## Observed record structure

Each of the 50 records contains six messages in this order:

1. Type 0x05: Performance name (16-byte complete message).
2. Type 0x00: common Performance block (73-byte complete message).
3. Type 0x01: Part 1 (32-byte complete message).
4. Type 0x02: Part 2 (32-byte complete message).
5. Type 0x03: Part 3 (32-byte complete message).
6. Type 0x04: Part 4 (32-byte complete message).

The dump contains 300 messages and source indexes 5 through 54. The observed SysEx header is F0 3F 00 22 44.

## Lossless policy

Every complete original frame is retained including F0 and F7. Unknown bytes are not decoded, masked, reordered, or discarded. JSON stores every frame as hex. No undocumented parameter meanings or checksum rules are assumed.

## Use

    python3 tools/parse_technox_mid.py TECHNOX.MID --output build/technox_performances.json
    python3 -m unittest discover -s tests -v

The C++ bank class is the interface-facing data model, not yet a complete JUCE MIDI-file loader or a sound engine. Next, connect a tested C++ SMF/SysEx decoder to this model and bind PerformanceBank::select() to the AU editor selector. Selection currently changes the selected data record; it does not yet synthesize audio or send hardware SysEx.
