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


## Adjacent 0x7BD9–0x7BDC FX event mailbox

The FX event path exposes a second, adjacent external-input group immediately below the original 0x7Cxx trace window. It is not an IC51 stream address and it is not the FX selector destination:

- `0x306A` reads `XDATA[0x7BDC]`; its value selects one of two event branches.
- When the value is zero, IC4 enters the `0x3070` branch. When nonzero, it enters `0x307A`.
- Both branches call `0x40C4`, which reads the command byte from `0x7BD9` and selects a value byte from `0x7BDA` or `0x7BDB`.
- The selected event fields are forwarded to `0x57A8` as the three-register tuple R7/R5/R3. The subsequent Part fan-out and subcommand dispatcher can route FX commands `0x50` and `0x51` to their separate store handlers `0x53F3` and `0x5429`.
- Those terminal handlers require `XDATA[0x3702] == IRAM[0x68]` and a **nonzero** `XDATA[0x3708]` gate before storing the clamped effect selector at `0x7BFE` or `0x7C06`.

Instruction anchors for the mailbox reads are `0x306D` (7BDC), `0x40CF` (7BD9), `0x40D6` (7BDA), and `0x40DE` (7BDB). The reachable disassembly contains explicit read constructions for these addresses; it does not show a corresponding explicit IC4 MOVX write to the four-byte mailbox. Its physical writer/device is therefore still unidentified. Indirectly constructed writes or another processor remain possible and must not be ruled out by the static scan alone.

The regression harness now includes explicit test-overlay bytes and checks the real IC4 execution from `0x306A` through the branch, `0x40C4`, `0x57A8`, Part fan-out and FX handler. It also checks the recorded read PCs/address/value for 7BDC → 7BD9 → the selected 7BDA/7BDB source. These are deterministic firmware-path tests, not a capture from a physical TechnoX board.

Keep this region separate from:
- `0x7BFE/0x7C06`: IC4-written FX selector slots;
- `0x7C00–0x7FFF`: status/resource peripheral window under investigation;
- `0x8000–0x8003`: four-byte SoundHost command/readback port;
- `0xFF10`: mode-dependent IC51 FX upload/status port.
