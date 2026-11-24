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
- the table byte's bit 0 is shifted into P3.0, so the first pass selects P3.0 low and the second pass selects P3.0 high; external P1.5/P1.6 levels therefore belong to the currently selected row;
- queues the selected byte with `0xA6D1`, then scans the second channel;
- restores P1 before returning.

The table bytes at `0xA6A4–0xA6A7` give an exact code mapping:

| Encoder scan state | P1.6 low | P1.6 high |
|---|---:|---:|
| First channel (state `0xA4`, P3.0 low, table `0xA6A4`) | `BE` | `7E` |
| Second channel (state `0xA5`, P3.0 high, table `0xA6A6`) | `BD` | `7D` |

This is strong firmware evidence for **two independent rotary/encoder inputs with a phase/strobe and a direction sample**. The source does not yet identify which physical encoder is labelled PARAMETER versus VALUE, or whether the two values are increment/decrement codes in the eventual consumer; that remains a separate call-path correlation task.

## 3. Input event FIFO

The event queue helpers are separate from the scan logic:

- `0xA6A8` initializes a 16-byte queue at internal RAM `0xA6A6–0xA6B5`, clears the associated row state at `0xA0–0xA5`, and initializes the event pointers/count.
- `0xA6D1` appends the accumulator byte to the queue, advances the write pointer, and wraps at `0xA6B6`.
- `0xA6E7` returns zero when the queue is empty; otherwise it removes the next queued byte and advances/wraps the read pointer.
- `0xA6A8` is called at `0x384F`; `0xA6FF` is called from `0x31D4`, `0x3303`, `0x352E`, `0x37DB`, and `0x3855`; `0xA762` is called from the Timer 1 interrupt handler at `0x2857`.

## 4. Downstream consumer correlation

The event FIFO is read by several main-state routines. One concrete consumer path at `0x3303–0x3316` is:

```
0x3303  call 0xA6FF (refresh matrix scan)
0x3306  call 0xA6E7 (pop one FIFO event into R7)
0x3309  XDATA[0x2B8F] = R7
0x3310  call 0x2FD4 (dispatch event code)
0x3313  call 0x30DC (advance/decrement associated state)
0x3316  call 0x2892
```

This confirms that event codes from the FIFO are staged at XDATA `0x2B8F` and passed to an event dispatcher. It does not alone assign a UI label to a code.

A separate reachable handler at `0x2D6D–0x2DC5` reads:
- `0x307A` as a selected-item/parameter index;
- `XDATA[0x3063 + index]` as the current value used by the conversion;
- event code `XDATA[0x2B8F]`, with explicit comparisons against `0xBE` and `0x7E`.

For the `BE/7E` branch, the handler converts the selected value using `0xEC30`, builds a record pointer from the converted result, and issues `EDCE` to page `0x33`, offset `0xD8`; it then sets `XDATA[0x2B94]=3`. The alternate branch calls `0x29AA`.

This is a concrete correlation between the first encoder's two codes and a selected-parameter/value transaction path. However, the reachable listing does not yet prove that this branch is directly invoked by every `A762` event, nor does it tell whether the physical first encoder is labelled PARAMETER or VALUE. The second encoder's `BD/7D` handling also needs its downstream consumer traced before assigning its UI role.

## 5. What this proves and what remains open

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

## 6. Emulator input seam

`TechnoX8032Machine::setExternalPortInputs(portAddress, mask, value)` supplies fixed external pin levels independently of the output latch. For the multiplexed encoder scan, the test fixture now also has a P3.0-selected P1 input seam: `setExternalPort1InputsByP3Bit0ForTesting()`. This lets the ROM test provide different P1.5/P1.6 levels for the two P3.0-selected rows without pretending that the physical board wiring is already known.

The regression test loads the raw IC4 image, runs the actual `0xA6A8` FIFO initializer and `0xA762` scan code, and checks that the firmware queues `7E/BE` for the P3.0-low channel and `7D/BD` for the P3.0-high channel. It also verifies no repeated event while P1.5 remains low. This tests the firmware routine and queue, not just the host pin-injection helper.

```
UI gesture -> typed panel event -> audio-thread handoff
           -> P1.5/P1.6 values for selected P3.0 scan row
           -> run IC4 scan routine -> firmware event FIFO
           -> decode the verified consumer path
```

The project should keep the PARAMETER/VALUE label mapping data-driven until a firmware consumer trace or hardware capture identifies the two physical encoder channels.
