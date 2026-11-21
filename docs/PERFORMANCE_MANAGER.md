# TechnoX Performance Manager

## Current JUCE integration

- TECHNOX.MID is embedded into the plugin binary as a JUCE BinaryData resource.
- The processor parses the Standard MIDI File during construction and validates the observed 300 SysEx frames.
- 50 programs are exposed through JUCE's program API, with the original source indexes 5..54 retained in each record.
- The editor now has a Performance ComboBox populated from the 50 names; selecting an item updates the processor's selected Performance and LCD overlay.
- The selected Performance index is stored/restored with the plugin state.
- Complete SysEx messages are retained as opaque byte vectors, including F0 and F7.

## Observed record structure

Each Performance has six messages: type 0x05 (name, 16 bytes), type 0x00 (common block, 73 bytes), then types 0x01..0x04 (four 32-byte Part blocks). The observed header is F0 3F 00 22 44. Source record indexes run from 5 through 54.

## Validation

The Python parser tests now include a fixture test that rebuilds the 300 extracted frames from the 50 grouped records and checks byte-for-byte equality against the source file. GitHub Actions runs these tests before configuring and building the Intel macOS AU/VST3/Standalone targets.

## Limits

This stage provides real file parsing and UI selection, not synthesis. Unknown parameter bytes are preserved but not interpreted. Selecting a Performance currently selects its stored data record; it does not yet map parameters to voices, produce sound, or transmit a hardware SysEx dump.
