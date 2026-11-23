# SAM9407 host-interface hypothesis for TechnoX

Status: strong research lead; not yet confirmed by a TechnoX PCB marking or a pin-level hardware capture.

## Evidence

1. The official Dream SAM9407 data sheet describes a single-chip sound system with a RISC synthesis/DSP engine, a 16-bit CISC P16 control processor, an MMU, external sample/program memory, MIDI I/O, and a host interface. It specifies 16-bit samples, 44.1 kHz wavetable synthesis, interpolation, alternate looping, and a 24 dB filter per voice.
2. The data sheet's host interface has an 8-register address space selected by A0-A2. Registers 0-1 are MPU-401-style UART registers, 2-3 are a 16-bit burst data port, and 4-7 are auxiliary/game-compatible registers. The burst port can upload program or sound-bank data.
3. The data sheet states that P16 program code runs from external memory. The MMU allows the DSP and P16 processor to share external ROM/RAM; sample data and delay lines also reside in external memory.
4. The data sheet's bootstrap mode accepts 256 words through the 16-bit burst port before P16 execution continues at external address 0x0100.
5. Public hardware references associate SAM9407 with Quasimidi's Quasar, Technox, and Raven families. A 1996 Sound On Sound Raven review says the Raven's basic sound building blocks are held in the same way as the Quasar and Technox. This supports investigation, but is not a substitute for identifying the actual IC fitted to a TechnoX board.

Primary technical reference:
- Dream SAM9407 data sheet (February 1996): https://dosdays.co.uk/media/dream/SAM9407.PDF

Corroborating product references:
- Sound On Sound, Quasimidi Raven (March 1996): https://www.soundonsound.com/reviews/quasimidi-raven
- Quasimidi chip-family association: https://en.wikipedia.org/wiki/List_of_sound_chips
- Recent Quasimidi architecture discussion: https://gearspace.com/threads/quasimidi-architecture-custom-firmware-jorg-schaaf-emulation-community-sam9407-dev-kits-prototypes.1468196/

## Comparison with current firmware evidence

- IC4 exposes an undocumented four-byte external register window at 0x8000-0x8003. The project currently records sequential writes as neutral bytes and paired 16-bit words; it does not assign SAM9407 register meanings.
- The firmware trace captures 544 four-byte transactions / paired words: 512 from caller 0x0E6A and 32 from caller 0x0E1D. The first 512 contain two 256-entry sweeps, with 16-bit values 0x0000 and 0x8000. The final 32 target indices 0x0F, 0x1F, ... 0xFF; their first 16 words are 0x07FF, the next 16 are 0x87FF.
- All 544 transactions share the same observed context: P2=0x80, P3=0xFA, SFR 0x8E=0x00, and IC51 deselected. The trace's soundDeviceResetReleased=false flag is derived from P3.2 being low; the physical connection and function of P3.2 are not established.
- This sequence is not yet a proven match for the SAM9407's documented 256-word bootstrap.
- IC4 also copies a 13,647-byte IC51 block into paged XDATA starting at 0xC000. The physical write mapping is now verified byte-for-byte. The role of this block is unknown: it could be code, tables, parameters, or another resource. It must not be labelled PCM without evidence.
- The firmware polls for 0xAC after the transfer. In current tests, the 0xAC response is injected synthetically; it is not a captured response from a physical SAM9407.

## Next tests

1. Obtain a clear photograph or board inspection showing the sound IC marking and its connections. Confirm or reject SAM9407 before naming the emulated device.
2. Map the four 8032 addresses, P2/P3 selects, bus-mode toggles, and reset line to the SAM9407 pins A0-A2, CS, RD, WR, SBHE, and RESET. The present 0x8000-0x8003 window alone is insufficient to infer this mapping.
3. Compare the exact 544 transaction sequence with the SAM9407 bootstrap and host-interface timing/protocol. Determine whether the two 256-word sweeps represent separate address banks or another initialization structure.
4. Determine whether the 0xC000 paged XDATA region is shared external P16/DSP memory or a separate buffer. Trace all subsequent reads/writes and the relevant control pins.
5. Only after the interface is matched, implement a SAM9407-compatible host/burst/MMU model. DSP synthesis and sample decoding remain separate tasks.

## Important limitation

This note is a testable hardware hypothesis, not a chip identification. The generic oscillator backend remains a test backend; no original TechnoX audio is produced yet.
