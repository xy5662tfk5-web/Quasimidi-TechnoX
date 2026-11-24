# IC4 v1.05 front-panel GPIO scan — verified code-level evidence

Status: code-level scan routines identified; physical button/encoder assignment is not yet decoded.

This note records the reachable IC4 v1.05 instructions that sample 8032 port pins and queue input events. It intentionally does not assign front-panel labels or invent encoder direction semantics.

## 1. Four-pattern P1/P3 scan: 0xA6FF

The routine at `0xA6FF–0xA761` performs a four-iteration scan:

- `R0` begins at internal RAM `0xA0`; `R2=4`.
- `DPTR=0xA6A0`; the four table bytes are `E7 EB ED EE`.
- For each table byte, the firmware saves the byte in `F0`, rotates it right through carry, copies the outgoing bit to P3.0 (bit address `0xB0`), ORs the scan pattern with `0xF8`, and writes it to P1 (SFR `0x90`).
- It then samples P1.3 (bit address `0x93`). The per-row state is held in internal RAM `0xA0–0xA3`.
- When the sampled level changes, the routine queues the complementary pattern byte through `0xA6D1`; this gives distinct transition events rather than repeatedly enqueueing a held level.
- After the four rows it samples P1.7 (bit address `0x97`) and compares/toggles internal flags `0x25` and `0x26`. Depending on the previous state it can queue `0xF0` or `0xF1`.

The row pattern table values and exact pin operations are byte-for-byte visible in the descrambled ROM. They are consistent with a multiplexed GPIO input matrix. The electrical net names and which panel legends correspond to the rows are not established yet.

## 2. Additional P1/P3 input scan: 0xA762

The routine at `0xA762–0xA79F` is a two-channel quadrature-style encoder scan:

- saves P1 and samples P3.0 into internal bit `0x27`;
- scans two independent state bytes at internal RAM `0xA4` and `0xA5`;
- uses P1.5 (bit address `0x95`) as the strobe/phase input: a low transition from the released state emits one event and sets the per-channel state byte; a held-low phase does not repeat; a subsequent high phase clears the state and rearms the next edge;
- samples P1.6 (bit address `0x96`) at the low-phase edge to select one of two direction codes from that channel's table pair;
- queues the selected byte with `0xA6D1`, then scans the second channel;
- restores P1 before returning.

The table bytes at `0xA6A4–0xA6A7` give an exact code mapping:

| Encoder scan state | P1.6 low | P1.6 high |
|---|---:|---:|
| First channel (state `0xA4`, table `0xA6A4`) | `BE` | `7E` |
| Second channel (state `0xA5`, table `0xA6A6`) | `BD` | `7D` |

This is strong firmware evidence for **two independent rotary/encoder inputs with a phase/strobe and a direction sample**. The source does not yet identify which physical encoder is labelled PARAMETER versus VALUE, or whether the two values are increment/decrement codes in the eventual consumer; that remains a separate call-path correlation task.

## 3. Input event FIFO

The event queue helpers are separate from the scan logic:

- `0xA6A8` initializes a 16-byte queue at internal RAM `0xA6A6–0xA6B5`, clears the associated row state at `0xA0–0xA5`, and initializes the event pointers/count.
- `0xA6D1` appends the accumulator byte to the queue, advances the write pointer, and wraps at `0xA6B6`.
- `0xA6E7` returns zero when the queue is empty; otherwise it removes the next queued byte and advances/wraps the read pointer.
- `0xA6A8` is called at `0x384F`; `0xA6FF` is called from `0x31D4`, `0x3303`, `0x352E`, `0x37DB`, and `0x3855`; `0xA762` is called from the Timer 1 interrupt handler at `0x2857`.

## 4. What this proves and what remains open

**Proven from the reachable IC4 disassembly:**
- The firmware actively drives P1 and P3.0 with scan patterns.
- The main scan samples P1.3/P1.7; the timer scan samples P1.5/P1.6 and uses P1.5 as an edge/strobe with P1.6 selecting direction.
- The timer scan contains two independent state bytes and emits the exact pairs `BE/7E` and `BD/7D`.
- The scan results are converted into queued byte events.
- The event queue is a 16-byte FIFO in upper internal RAM.

**Not proven yet:**
- which of the two encoder channels is labelled PARAMETER and which is VALUE on the physical panel;
- the complete meaning of each direction code and its downstream update/selection semantics;
- the exact meaning of queued codes `F0/F1` and the other table-derived bytes at every caller;
- debounce timing, detent scaling, acceleration, and direction;
- the physical board wiring or whether external latches/shift registers sit between the CPU pins and panel controls.

Do not connect UI gestures to guessed pins. First correlate one known control action at a time with the port patterns and queued bytes, ideally using a board capture or a controlled firmware fixture.

## 5. Emulator input seam

`TechnoX8032Machine::setExternalPortInputs(portAddress, mask, value)` already supplies external pin levels independently of the output latch. Its `readInternal()` path combines the emulated port pins with the configured external input mask. This is the correct low-level seam for later panel simulation:

```
UI gesture -> typed panel event -> audio-thread handoff
           -> setExternalPortInputs(P1/P3, mask, value)
           -> run IC4 scan routine -> firmware event FIFO
           -> decode the verified consumer path
```

The project should keep the pin-to-control map data-driven until each input line and event code has been traced.
