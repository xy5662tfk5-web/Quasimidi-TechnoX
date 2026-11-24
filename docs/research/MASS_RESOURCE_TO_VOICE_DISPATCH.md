# M.A.S.S. resource setup and firmware voice-command dispatch

## Scope and key distinction

The IC4 firmware uses internal RAM byte 0x2D as the selector for a P2-addressed XDATA workspace while preparing M.A.S.S. resource commands. It is not itself an XDATA page number or an address range 0x2D00–0x2DFF: routine 0x16C3 loads P2 from RAM[0x2D], and the observed Note 65 trace selects pages 0x01 and 0x02 for the two runtime Part events. The per-page offsets 0x60–0xFF hold staging/slot data. These are CPU external-memory addresses, not identified physical DSP register addresses. The separate 0x8000–0x8003 command port remains the observed external command interface, and its device identity and payload semantics remain unknown.

This note consolidates the earlier instruction-level trace of routines 0x1494, 0x16C3, 0x189D and 0x1009. The new machine trace records 0x2D00-0x2DFF reads/writes with instruction PC, stack-top return snapshot, P2/P3 and SFR 0x8E so these paths can be rechecked under isolated MIDI stimuli.

## 1. Descriptor seed at 0x1494

The routine reads the selected resource pointer from the XDATA address formed by internal RAM bytes 0x29:0x2A:

- P2 is loaded from internal RAM 0x29.
- R0 is loaded from internal RAM 0x2A.
- The first MOVX byte is masked with 0x7F and stored in internal RAM 0x2F.
- R0 is incremented; the next MOVX byte is stored in internal RAM 0x30.

Thus, before later transformations:
`RAM[0x2F] = XDATA[RAM[0x29]:RAM[0x2A]] & 0x7F`
and
`RAM[0x30] = XDATA[RAM[0x29]:RAM[0x2A] + 1]`.

The values may be changed by the intervening command-stream preparation path; the later 0x16C3 stores should not automatically be interpreted as the original source bytes.

## 2. Routine 0x16C3: staged resource/voice command preparation

The routine selects P2 from internal RAM byte 0x2D (the selected page number), then operates on these offsets within that page:

| Offset | Observed firmware operation | Interpretation status |
|---|---|---|
| 0xFB | Read, then write 0x80 / 0xC0 depending on ACC.7; alternate path writes 0x00 | Control/handshake field in the XDATA staging page; bit semantics incomplete |
| 0xFC | Write | Meaning unknown |
| 0xFE-0xFF | Write | Meaning unknown |
| 0x61-0x62 | Write a two-byte block pointer | Pointer role supported by surrounding stream setup |
| 0xF8 | Write RAM[0x2F] | Staged field; not a physical chip register |
| 0x60 | Write RAM[0x30] | Staged field; not a physical chip register |
| 0xFD | Read-modify-write | Flag field; exact hardware meaning unknown |

Instruction-level sequence recorded in the earlier trace:

- 0x16C6-0x16C8: select R0=0xFB and load RAM[0x3F].
- 0x16CA onward: test ACC.7; read 0xFB and conditionally write 0x80 or 0xC0. The alternate branch writes zero.
- 0x16D8-0x16DF: test the prior 0xFB bit 3; one branch clears internal flag 0x06 and sets RAM[0x3F]=0x80.
- 0x16E6-0x16EC: write offset 0xFC.
- 0x16ED-0x16F5: increment the 16-bit pointer in RAM[0x36:0x37] by two.
- 0x16F7-0x16FF: write offsets 0xFE and 0xFF.
- 0x1714-0x171C: write the block pointer to offsets 0x61 and 0x62.
- 0x171D: call 0x189D, the resource command-stream interpreter.
- 0x1723-0x172C: write RAM[0x2F] to offset 0xF8 and RAM[0x30] to offset 0x60.
- 0x172D-0x1740: read offset 0xFD, copy internal flags 0x00-0x02 into bits 0-2, clear bits 6-7, and write the resulting byte back. Bits other than 0-2 and 6-7 are preserved.

The earlier trace also found that FB.3 gates the response-read path around 0x15CD-0x15D8: offsets 0xF8 and 0x60 are read when FB.3 is clear. The meaning of the resulting values and of FD bits 6-7 is not yet proven.

## 3. Command-stream interpreter 0x189D

The interpreter consumes the selected descriptor's byte stream and dispatches record types into firmware arithmetic/parameter handlers. The existing `MASS_RESOURCE_STREAM_AND_EXTERNAL_COMMAND_PORT.md` documents the verified byte groups, record lengths, lookup tables, fixed-point arithmetic and output-slot construction.

The interpreter is a meaningful software-side stage between resource selection and the external command writer. It does not, by itself, decode IC51 bytes into PCM or identify the audio hardware.

### 0x0A is transient interpreter state, not a Part/Performance parameter

The reachable IC4 disassembly shows several distinct producers/consumers of this bit:

- `0x018D` selects the current XDATA page from RAM[0x2D] and calls `0x0211`. `0x0211` reads page offset `0xFB`; if ACC.5 is set, it executes `SETB 0x0A`, then enters `0x0DF7`. This is part of page/slot initialization.
- `0x0F25` calls `0x047E` when the prior E7 state is clear. `0x047E` first clears `0x0A`, then reads offset `0xFB`; it sets `0x0A` again when ACC.5 is set. This is a separate synchronization path from the selected page's FB.5.
- `0x1494` clears `0x0A` on entry to per-resource selection/preparation.
- In the descriptor-table path, `0x24A1` sets `0x0A` immediately before calling the middle entry `0x192D` of the resource-stream interpreter; `0x24E5` clears it immediately after that call returns.

The bit is therefore not established as a persistent setting attached to a sound, Part, or Performance. It changes with page-status handling and is temporarily scoped around a specific descriptor/interpreter call. The two cached streams below preserve both parser outcomes, but selecting one whole-program variant for every operation would be premature. The correct choice must follow the matching firmware call context, not a guessed MIDI or Part parameter.

## 4. Endpoint scan at 0x1009

The earlier trace of 0x1009 shows it selecting page 0x2D and scanning offsets 0x80 through 0xF8 in steps of eight:

- 0xFF entries are skipped.
- A candidate is accepted when `(value & 0x7E) == 0x16`.
- On a match, P2 is changed to 0x80 and the routine calls 0x0DB7, the four-byte writer for XDATA 0x8000-0x8003.
- R4 is set to zero before the writer call.
- A separate flag at 0x0C derives from the E7 state set by the earlier 0x0D path; it is not inferred from bit 7 of the fourth packet byte.

This makes 0x2D80-0x2DF8 a firmware-side table/slot scan that can feed the 0x8000-0x8003 writer. The signature test does not yet establish the meaning of the matched records or the external device's response.

## 5. New instrumentation and differential stimulus

The machine model now retains reads/writes in the actual P2-selected XDATA windows 0x0100-0x02FF, plus the existing 0x7BD4-0x7BD7 watch window. The byte RAM[0x2D] is sampled as the P2 page selector; it is not itself an XDATA address. Each event includes:

- address, value and read/write direction;
- instruction PC and stack-top return-address snapshot;
- P2 and P3 pin states;
- SFR 0x8E mode and machine-cycle timestamp.

The firmware regression now runs Program Change, Note On and Note Off as separate ACIA/MIDI stimuli after the same factory Performance initialization. For each stage it reports deltas for calls to 0x0DB7, 0x4256, 0x5671, 0x57A8, 0x16C3, 0x189D, 0x1009, 0x018D and adjacent helpers, plus selected-page 0x01xx/0x02xx and 0x7Cxx bus traces.

These are diagnostic probes. Until the updated CI run passes and the emitted trace is inspected, no per-event packet count or note-trigger semantics are claimed.

## 6. What this enables next

1. Identify which isolated MIDI event enters 0x16C3, 0x189D and 0x1009.
2. Compare the 0x2Dxx staging writes and the 0x8000-0x8003 packet stream for Program Change, Note On and Note Off.
3. Find the first packet field that changes with note number, velocity, channel/Part or Note Off.
4. Model the firmware's command preparation and packet transport separately from the still-unknown external sound device.
5. Decode the IC51/resource bytes only when a concrete descriptor-to-waveform address path is established.

No authentic sample playback, DSP model, sample codec, or complete original audio engine is implemented yet.

## 7. Correction: RAM[0x2D] is a P2 page selector, not page 0x2D

An expanded Note 65 trace for selector values 2 and 3 captures the 8032 state at 0x16A4–0x171D. This corrects the earlier shorthand in this file:

- 0x16C3 begins with P2 pointing at 0x29, the per-Part descriptor-selection record area.
- The preparation path then writes RAM[0x2D] to P2. For the first active event (runtime ID 0x0C) the observed selected page is 0x01; for the second event (0x0D) it is 0x02.
- At 0x16CA, 0x16E4, and 0x16E6, P2 is 0x01 for event 0x0C and 0x02 for event 0x0D. The code is addressing offsets such as 0xFB, 0xFC, and 0xFE/0xFF on those pages. The stream interpreter then uses that same page with offsets 0x80..0xF8.
- The queue routine 0x1742 does return in both traces. In the second event it reads the queue counter from RAM[0x52] (0x13), returns via 0x177E with RAM[0x52] decremented to 0x12, then execution continues into 0x16C3. This rules out the queue drain as the immediate reason why the selector-2/3 probe records only event IDs 0x0C and 0x0D.
- The second event reaches 0x171D (call to 0x189D) with DPTR=0x274B, A=0x03, and R5=0x63; unlike the first event's interpreter entry (DPTR=0x26E2, A=0x16, R5=0x7A), the second event has not yet been observed returning to 0x1720 in that capture. This moves the next investigation into the second resource-stream interpreter context. The selected XDATA page should be traced using full addresses 0x0100–0x02FF, not the erroneous range 0x2D00–0x2DFF.

The physical identity and meaning of the page contents are still open. This address correction does not identify the receiving IC or imply that the selected page is physically implemented as ordinary RAM in every P3/SFR 0x8E bus mode.

## 8. Nested resource-list branch in the second Part stream

The expanded Note 65 trace distinguishes the first and second active event streams. Event 0x0D enters 0x189D with descriptor pointer 0x274B, and after the stream header is consumed at 0x18D7–0x18DF the data cursor is 0x03AD. The first nested pointer pair read at 0x1906/0x190A is `0x06, 0x38`, producing the indirect table pointer `0x3806` used by the branch at 0x1A01.

The current branch map is confirmed by disassembly:

- 0x19FB tests internal bit address 0x06. If set, it jumps back to 0x18E1; if clear, it enters the indirect-table path at 0x1A01.
- 0x1A01 loads DPTR from internal RAM[0x3A:0x3B], then 0x1A16 reads a table byte. At 0x1A17 the interpreter compares R3 with zero; for the zero case, 0x1A1A tests ACC.6 and may branch to 0x19E9.
- The 0x19E9 cleanup path clears two selected-page staging bytes, then 0x19F8 returns to the loop at 0x18E1.
- A non-empty nested record sets ACC.6 at 0x1A1D, fetches a four-byte tuple, and writes those bytes into the selected P2 page at 0x1A31–0x1A3D. The subsequent per-slot housekeeping path reaches 0x1AA6, which also returns to 0x18E1.
- The common loop at 0x18E1 advances RAM[0x2E] by eight before testing its accumulator-bit boundary and loading the next stream cursor. Thus the 0x19FB/0x1A01 route is a nested descriptor/slot expansion path, not a second MIDI event or a second sound-host command protocol.

The PC/register watch now includes the loop, empty-slot cleanup, nested-table read, and four-byte tuple writer. The exact purpose of the nested table values and their physical consumer remains unproven; no sample address or waveform interpretation is assigned here.
