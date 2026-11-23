# Undocumented 0x7Cxx peripheral-bus trace

## Purpose

The IC4 v1.05 firmware accesses the external XDATA window 0x7C00-0x7FFF, which the current MAME skeleton leaves unimplemented. This window is a stronger hardware-research lead than an isolated byte-pattern match, but the device and register meanings remain unknown.

This change adds observability only. It does not assign a sound-chip identity, synthesize register responses, or change the existing open-bus behavior.

## Firmware facts already established

- Code at 0xB61B clears 0x7BFF-0x7C04, writes 0x01 to 0x7C05 and 0x00 to 0x7C06, then clears 0x7C07-0x7C0F.
- It then writes 02 47 40 06 00 to 0x7C10-0x7C14.
- Code at 0x7CC1 reads 0x7C00 and 0x7C07; values are copied to internal RAM 0x309D and 0x309B.
- Code at 0x877C reads 0x7C06.
- Multiple routines poll 0x7D95. The meaning of this status byte is not established.
- The routine at 0xB61B has no direct LCALL/ACALL caller in the current static cross-reference; an indirect entry or dispatch remains possible.

## New machine trace

`TechnoX8032Machine::UnmappedBusEvent` now retains:

- XDATA address, value and read/write direction;
- instruction PC of the MOVX access;
- stack-top return address snapshot (diagnostic context, not guaranteed to be the outer caller);
- P3 pin state and SFR 0x8E bus-mode value;
- emulated machine-cycle timestamp.

The ring buffer retains 4,096 unmapped accesses. Existing 0x7000-0x7FFF read/write histograms remain intact. The firmware machine test prints retained accesses in 0x7C00-0x7FFF after the initial peripheral initialization phase, before enabling the synthetic test RAM overlay.

## Interpretation limits

- An event at 0x7Cxx proves only that the emulated CPU issued an external bus access at that address under the recorded P3 and 0x8E state.
- The stack-top return address is a raw snapshot and may refer to a nested helper rather than the higher-level operation.
- The trace does not prove that the physical board maps these addresses to an audio DSP. The IC51 selection decode and other bus modes must be considered.
- The trace does not emulate a peripheral response. Unmodeled reads remain 0xFF and writes remain ignored unless the existing explicitly enabled test overlay is used.
- No sound-chip register names or behavior should be inferred from the values alone.

## Next analysis

1. Group events by instruction PC, address, P3, and 0x8E mode.
2. Correlate each access with the instruction-level disassembly and surrounding call path.
3. Compare cold boot, completed initialization, Program Change, Note On, Note Off, and Performance changes using identically initialized machine states.
4. Determine whether 0x7Cxx accesses correlate with the separate 0x8000-0x8003 command port or 0xC000-0xFFFF memory interface.
5. Only then propose a peripheral model, and keep any unverified response behavior behind an explicit test fixture.

## Current sound-engine status

This trace is reverse-engineering instrumentation, not a working audio engine. The IC51 transfer into the modeled 0xC000-0xFFFF window is verified byte-for-byte, but sample encoding, sample boundaries, voice commands, physical device identity, and playback timing remain unresolved.
