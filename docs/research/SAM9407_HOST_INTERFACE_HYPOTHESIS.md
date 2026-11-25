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

## New comparison after reconstructing the IC51 boot transfer

A complete 16-KiB XDATA image can now be reconstructed from IC51 bank 0, table selector R7=`0x48` (source `0x3C64`, transfer length 13,647 bytes), using the IC4 byte-remap table at `0xADDD`. The reconstructed C000-FFFF window matches the image produced by executing IC4, byte-for-byte. This validates the 8032-side external-memory copy and its address permutation.

That result does **not** identify the image as a waveform bank. A contemporary April 1995 review of the TechnoX describes its sample memory as six megabytes and lists 21-note polyphony, 16 Parts, 512 ROM patches and 24 ROM drum sets. The 16-KiB C000-FFFF transfer is therefore much smaller than the advertised sample-memory capacity and should be treated as a boot-time control/program/resource image until a later device-side read path proves otherwise. Source: *The Mix*, “Revolt into style”, April 1995, https://www.muzines.co.uk/articles/revolt-into-style/12193?theme=2 (see the Technox “Overall features” and “Spec check” sections).

The SAM9407 data sheet gives a useful *discriminator*, not a confirmation:
- Its P16 processor executes from external memory; the synthesis DSP shares external ROM/RAM via an MMU. Sample data and delay lines reside in external ROM/SRAM/DRAM.
- Its host interface is a 16-bit data bus with three address inputs A0-A2. Registers 0/1 are MPU-401 UART, 2/3 are a 16-bit burst-data port, and 4-7 are auxiliary/game-compatible registers.
- In BOOT mode, the internal bootstrap expects 256 words (512 bytes) over the 16-bit burst port, stores them at external address 0x0100-0x01FF, then transfers execution to 0x0100.

The TechnoX 8032 trace currently shows a different-looking set of operations: a 13,647-byte IC51-to-C000 paged memory copy and 544 four-byte command-port transactions at XDATA 0x8000-0x8003. The documented SAM9407 interface has not yet been mapped pin-for-pin to the 8032 P2/P3/SFR 0x8E bus controls. In particular, the 512-word bootstrap sequence in the data sheet must not be conflated with the 544 captured four-byte transactions. The IC4 C000 transfer might be shared P16/DSP program or data memory, or another resource buffer; the current trace alone cannot distinguish those roles.

The next discriminating firmware/hardware test is to map the 8032 port and P3 strobes to candidate SAM9407 pins (CS/, RD/, WR/, A0-A2, RESET/, BOOT, S0-S1 and WA/WD buses), then establish whether the external C000-FFFF image is read by the candidate chip after the upload. Until that is done, SAM9407 remains an interface hypothesis rather than an identified TechnoX component.

## Comparison with current firmware evidence

- IC4 exposes an undocumented four-byte external register window at 0x8000-0x8003. The project currently records sequential writes as neutral bytes and paired 16-bit words; it does not assign SAM9407 register meanings.
- The firmware trace captures 544 four-byte transactions / paired words: 512 from caller 0x0E6A and 32 from caller 0x0E1D. The first 512 contain two 256-entry sweeps, with 16-bit values 0x0000 and 0x8000. The final 32 target indices 0x0F, 0x1F, ... 0xFF; in the current open-bus fixture (readback defaults to 0xFF), the first 16 words are 0x07FF and the next 16 are 0x87FF. These 32 values are not confirmed physical-device responses.
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

## Protocol-layer audit after the FX stream runtime trace

The current firmware evidence is best treated as three distinct external-bus paths. They must not be collapsed into one assumed SAM9407 interface:

1. **Four-byte command window (0x8000..0x8003).** Firmware helper 0x0DB7 writes [R5, R2, R3, R4] after selecting P2 page 0x80. Helper 0x0DCE conditionally toggles bit 0x0C, writes 0x40 or 0xC0 to offset 3, then reads offset 2. The machine model records these events and their P2/P3, SFR 0x8E, PC, return-PC and cycle context. The first byte is retained as an opaque command/latch byte; no SAM9407 register number is assigned.

2. **Mode-dependent byte aperture (0xFF10).** In the current 8032 bus model, reads/writes outside bus mode 2 are treated as device status/data traffic; in mode 2, the same address is the external sound-memory aperture. The IC51 FX upload routines 0xAF93 and 0xB015 send their decoded resource bytes through this path. A startup read value of 0xAC is injected by the current regression fixture; it is not an observed hardware response.

3. **Paged external memory (0xC000..0xFFFF).** IC4 copies a 13,647-byte IC51 payload into this 16-KiB window. The mapping is verified byte-for-byte against execution of the firmware, but the destination's physical memory owner and the copied block's role are not yet known.

### What the FX runtime regression now proves

The IC4 FX2 loader at 0x88D1 is executed against the real ROM for Equalizr1 (index 14). The test verifies that the Stream-A data sent to 0xFF10 in mode 2 matches the IC51 resolver byte-for-byte: selector 0x42, source address 0x4F02, count 0x3A, preamble 15 00, and 174 payload bytes (177 writes including the count and preamble). Stream B selector 0x43 skips leading byte 0x74 and terminates on zero count at 0x4D6D, so it emits no upload bytes for this algorithm. This confirms the firmware-to-resource transport path, not the receiver's interpretation or the audible Equalizer algorithm.

The two stream encodings are different:
- 0xAF93: count byte, two preamble bytes, then count * 3 payload bytes.
- 0xB015: circular-right-rotated count, three preamble bytes, then count payload bytes; its pointer walk skips the leading/inter-block separator bytes.

### SAM9407 pin-level discriminator

The February 1996 Dream SAM9407 data sheet describes a 144-pin device with a host interface that uses A0-A2, CS/, RD/, WR/, SBHE/ and a 16-bit D0-D15 bus. Its register map is eight locations: 0/1 are MPU-401 UART, 2/3 are the 16-bit burst port, and 4-7 are auxiliary/game-compatible registers. The datasheet pinout gives the following inspection targets:

| Signal | SAM9407 pin |
|---|---:|
| A0 / A1 / A2 | 89 / 101 / 102 |
| RD/ / WR/ / CS/ | 99 / 97 / 134 |
| RESET/ / BOOT | 19 / 37 |
| S0 / S1 / RUN | 136 / 139 / 26 |
| P0 / P1 / P2 / P3 | 57 / 58 / 59 / 60 |

The TechnoX trace does not yet map the 8032 pins or external latches to these SAM9407 pins. A four-byte XDATA window is not itself evidence of an eight-register SAM9407 host interface: external address decoding or glue logic could alter the mapping, but that possibility must be demonstrated from P2/P3/SFR 0x8E transitions or a board-level connection trace.

### Required compatibility checks before implementing a SAM9407 model

- Identify the marking/package on the TechnoX sound IC. A SAM9407 hypothesis must be confirmed by the actual part marking or an unambiguous board-level trace.
- Reconstruct the active-low CS/, RD/, WR/, RESET/ and BOOT waveforms from every relevant P2/P3/SFR 0x8E transition, not just the byte written to XDATA.
- Determine whether the two 256-entry sweeps at caller 0x0E6A are host register initialization, memory address/data phases, or something else. Do not equate them with the datasheet's 256-word bootstrap transfer.
- Trace the 0xFF10 status-read loop and the origin of the expected 0xAC without the synthetic test override.
- Follow subsequent reads of the C000-FFFF image and establish which device drives that memory window.

**Implementation decision:** keep the current receiver model open-bus/conservative and retain neutral signal names until these checks establish the hardware mapping. A SAM9407 P16/DSP/MMU/FX implementation must not be added merely because the chip family is plausible. Once identified, the firmware-derived FX upload streams can be connected to the confirmed host/memory interface, then FX1/FX2 DSP behavior can be reconstructed against the manuals and the IC51 resource data.
