# Original audio engine reconstruction — verified checkpoint

Branch: `research/original-technox-audio-engine`

## What is now established

- IC4 v1.05 runs in the in-project Intel 8032/MCS-51 machine model.
- The CPU model accepts timed MIDI bytes through the MC6850-compatible ACIA path.
- IC51 is selectable through the modeled P3 bus signals and can be read in two banks.
- The IC51 resource resolver can follow the firmware-derived bank/program pointer table, parse a descriptor, and decode the command stream into structured resource commands.
- The IC4 firmware accesses an undocumented external audio register window at `0x8000–0x8003`. The emulator captures contiguous four-register writes as packets and captures paired bytes as 16-bit burst words. Each byte now carries its own P2/P3, device-index, bus-mode, IC51-select, reset, PC, and cycle snapshot; host reads capture the corresponding bus context and caller return address.
- The existing firmware integration fixture drives a MIDI note stimulus and asserts a repeatable change from 548 startup packets to 553 additional packets. It checks the two 256-write sweeps and the later 32-iteration/table-write sequence by call-site/return-PC and packet shape.

## Trace retention correctness

- SoundHost write/read traces are bounded rings (4096 writes, 2048 reads). Their accessors take absolute event indices modulo capacity, so only indices in the newest retained window are valid after wraparound.
- The MIDI-stage report now clamps each stage's output to that retained window and explicitly reports total, retained, and overwritten events. This prevents wrapped indices from silently printing newer events as if they belonged to an earlier part of the stage.
- This is a trace-reporting correctness fix; it does not alter firmware execution or the emulated audio-device behavior.

## Latest tracing improvement

- The real-ROM MIDI fixture already separates Performance-mode Program Change, Note On, and Note Off, then exercises Multi-mode SysEx setup and a synthetic-ready Note On/Off path.
- The stage report now also emits every captured SoundHost response read made during that stage, including register index, returned byte, instruction PC, caller return PC, P2/P3, bus mode, IC51-select, reset state, and machine cycle. This makes read-side handshake behavior directly comparable between MIDI stages.
- SoundHost write events now retain the matching per-write context as well: selected device index, instruction/caller PCs, P2/P3, bus mode, IC51-select, reset state, and cycle. The MIDI-stage report prints these writes alongside response reads, enabling transaction-level comparison without assigning undocumented register meanings.
- This is additional observability only. It does not change the open-bus model or claim that the returned bytes represent real DSP responses.

## MIDI-stage write classification

- Each stage's retained SoundHost write window is summarized by register index and selected device index, in addition to the raw chronological event list.
- Register-3 writes of `0x40` and `0xC0` are counted separately as the two already-observed handshake-strobe values. The report does not infer meanings for other values or for the device-index latch.
- The summary is a comparison aid for Program Change, Note On, and Note Off traffic; it is not a decoded command protocol.

## Unknown 7Cxx peripheral trace

- The firmware fixture already captures unmodeled external-bus accesses in `0x7C00–0x7FFF` with address, read/write direction, value, instruction PC, caller return PC, P3, bus mode, overlay flag, and cycle.
- MIDI-stage reporting now prints the trace-window total, retained count, and overwritten count before listing the 7Cxx events. The existing event list is restricted to the current stage and retained ring-buffer window.
- This improves the evidence around the repeatedly polled `0x7D95` status location and initialization writes in `0x7C00–0x7C1F`. It does not identify the peripheral or assign register meanings.

## Program identity now reaches the voice layer

- `VoiceSlot` captures the selected Part's IC51 bank and sound number at note-on.
- `MASSAudioEngine` reads the active Part's bank/sound parameters and passes them into newly allocated and mono/lead-retargeted voices.
- This closes a real integration gap: a future resource-backed DSP backend can now identify the exact IC51 program for each active voice without consulting UI/editor state.
- This is program routing metadata only. The current `BasicMASSVoiceBackend` remains a generic test synthesizer; no PCM/sample playback or original DSP behavior is claimed.

## What is not established

- The four external register meanings are not decoded. The current names `register0..register3` are intentionally neutral.
- A captured firmware packet is not yet a sample-playback command specification. Do not assign sample address, pitch, loop, envelope, or voice semantics based only on byte patterns.
- `MASSResourceProgramResolver` recovers IC51 metadata and command streams; it explicitly does not decode PCM.
- `BasicMASSVoiceBackend` is still a generic oscillator test voice. It is not original TechnoX audio.
- The physical sound processor / DSP identity, its host-port handshake, sample-memory mapping, sample encoding, voice architecture, filters and effects remain unverified. Period advertising describes 6 MB of sampled waveforms, so the 128 KB IC51 image cannot by itself be treated as the complete waveform store; the larger sample-memory path still has to be located and mapped.
- The modeled C000–FFFF sound-memory backing store is an emulation aid, not proof of the physical memory topology or a decoded audio ROM.

## Next reverse-engineering milestone

1. Use the new per-byte write and read context to correlate every 8032 access to `0x8000–0x8003` with caller/return address, P2/P3 state, sound-bus mode, reset state, and preceding IC51 reads. Identify any control-line transitions that occur between bytes of one packet.
2. Separate initialization/configuration bursts from note-on, note-off, pitch, controller, and resource-load traffic using controlled MIDI stimuli.
3. Determine the exact host-port protocol, including ready/busy/ack timing and the meaning of reads versus writes. The present open-bus response must not be mistaken for hardware behavior.
4. Trace the external sample-memory address/data path and establish whether IC51 holds descriptors/parameter streams while a separate memory holds PCM waveforms.
5. Only after the above is supported by repeatable traces or hardware documentation, implement a packet interpreter and a sample-accurate backend. Keep the generic oscillator clearly labeled as a test backend until replacement.

## Regression anchors

The current `tests/test_technox_8032_machine.cpp` contains the firmware/MIDI fixture and packet-shape assertions. The current `tests/test_mass_resource_rom.cpp` contains the IC51 directory, descriptor, command-stream and firmware arithmetic tests. Run the full CTest suite after every change to the CPU, bus, ROM mapping, or resource decoder.
