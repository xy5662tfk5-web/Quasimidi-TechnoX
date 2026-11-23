# M.A.S.S. resource setup and firmware voice-command dispatch

## Scope and key distinction

The IC4 firmware uses XDATA page 0x2D as a staging/control structure while preparing M.A.S.S. resource commands. These 0x2Dxx addresses are CPU external-memory addresses in the current machine map; they are **not** identified physical DSP register addresses. The separate 0x8000-0x8003 command port remains the observed external command interface, and its device identity and payload semantics remain unknown.

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

The routine selects XDATA page 0x2D through P2, then operates on the following offsets:

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

## 4. Endpoint scan at 0x1009

The earlier trace of 0x1009 shows it selecting page 0x2D and scanning offsets 0x80 through 0xF8 in steps of eight:

- 0xFF entries are skipped.
- A candidate is accepted when `(value & 0x7E) == 0x16`.
- On a match, P2 is changed to 0x80 and the routine calls 0x0DB7, the four-byte writer for XDATA 0x8000-0x8003.
- R4 is set to zero before the writer call.
- A separate flag at 0x0C derives from the E7 state set by the earlier 0x0D path; it is not inferred from bit 7 of the fourth packet byte.

This makes 0x2D80-0x2DF8 a firmware-side table/slot scan that can feed the 0x8000-0x8003 writer. The signature test does not yet establish the meaning of the matched records or the external device's response.

## 5. New instrumentation and differential stimulus

The machine model now retains reads/writes to 0x2D00-0x2DFF and the existing 0x7BD4-0x7BD7 watch window. Each event includes:

- address, value and read/write direction;
- instruction PC and stack-top return-address snapshot;
- P2 and P3 pin states;
- SFR 0x8E mode and machine-cycle timestamp.

The firmware regression now runs Program Change, Note On and Note Off as separate ACIA/MIDI stimuli after the same factory Performance initialization. For each stage it reports deltas for calls to 0x0DB7, 0x4256, 0x5671, 0x57A8, 0x16C3, 0x189D, 0x1009, 0x018D and adjacent helpers, plus 0x2Dxx and 0x7Cxx bus traces.

These are diagnostic probes. Until the updated CI run passes and the emitted trace is inspected, no per-event packet count or note-trigger semantics are claimed.

## 6. What this enables next

1. Identify which isolated MIDI event enters 0x16C3, 0x189D and 0x1009.
2. Compare the 0x2Dxx staging writes and the 0x8000-0x8003 packet stream for Program Change, Note On and Note Off.
3. Find the first packet field that changes with note number, velocity, channel/Part or Note Off.
4. Model the firmware's command preparation and packet transport separately from the still-unknown external sound device.
5. Decode the IC51/resource bytes only when a concrete descriptor-to-waveform address path is established.

No authentic sample playback, DSP model, sample codec, or complete original audio engine is implemented yet.
