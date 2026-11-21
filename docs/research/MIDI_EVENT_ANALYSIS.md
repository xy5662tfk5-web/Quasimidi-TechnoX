# IC4 MIDI event path — confirmed static findings

Source: descrambled TechnoX IC4 v1.05 reachable-code disassembly. The disassembler is reachability-guided; computed jumps, ROM banking and indirect dataflow remain unresolved. Addresses below are firmware code or XDATA addresses, not assumed DSP register names.

## 1. MIDI RX and byte queues

- RX interrupt handler: `0x3DEE–0x3E76`; received byte is held in direct RAM `0x5C`.
- Status/data routing at `0x3E42–0x3E69`: status `0x99` sets internal flag `0x1C`; status bytes at or above `0x80` other than that clear it.
- If flag `0x1C` is set and XDATA `0x7D95 == 0`, the byte is appended through `0xA831` to queue page `0x33`; otherwise it is appended through `0xA7DE` to queue page `0x32`.
- This is byte routing only. Queue semantics are established by their consumers, not by the queue names.

## 2. Channel-10 Note On path (status 0x99)

Queue page `0x33` is consumed by `0x5AE3` via `0xA84F`.

- Bytes >= `0x80` are saved as running status in XDATA `0x304D`.
- Data bytes are paired using internal flag `0x1F`; the first data byte is saved to XDATA `0x2B9E`.
- On the second data byte, the special path is taken only when XDATA `0x7CEF != 0` and saved status `0x304D == 0x99`.
- The second data byte is mapped using the value at XDATA `0x370B` and helper `0xABBF`; the first data byte from `0x2B9E` and mapped second byte are passed to `0x44BA` in `R7` and `R5`, respectively.

For MIDI status `0x99`, these two data bytes are note number and velocity. This confirms a channel-10 Note On data-pair handler, but does not establish that it is the melodic voice-start path.

## 3. What 0x44BA does

Routine `0x44BA–0x453B`:

- Saves `R7` and `R5` to direct RAM `0x5F` and `0x60`.
- Rejects values below `0x02` in `R7`.
- Derives a nibble from `R5` and writes it to XDATA `0x2B80`.
- For `R7 == 0x2A` or `0x2C`, calls `0x446B` with `R7=0x2E`; for `R7 == 0x47`, calls it with `R7=0x48`.
- Calls `0x7861`, reads XDATA `0x7CFE`, and calls `0xABBF` for a second table transformation.
- Writes `R7` to XDATA `0x296C`, transformed value to `0x296D`.
- If saved `R7` is `0x2E` or `0x48`, prepares `RAM[0x28..0x2A] = 09,29,6C`; otherwise prepares `49,29,6C`. Calls `0x1494`, then clears XDATA `0x2067`.

This is a parameter/descriptor transaction path. It is not yet proven to be the final oscillator or sample trigger.

## 4. General channel note event parser

Routine `0x57A8` receives three values in `R7,R5,R3`, saves them, splits the status byte into message type and channel, and has explicit branches for message nibbles `0x09` and `0x08`.

- For message type `0x09` (Note On), nonzero velocity increments XDATA counter `0x3039`; zero velocity follows the release/decrement path.
- For message type `0x08` (Note Off), the same per-channel counter is decremented.
- When the counter reaches zero, and XDATA `0x3050` indicates a pending/active state, it calls `0x4100` using values held at `0x304B` and `0x303A`.
- `0x4100` clamps its two inputs, updates XDATA state `0x304B`, `0x303A`, and on the active path calls `0xB9C2`; the latter walks/copies structured data through XDATA before returning.

This identifies a distinct general Note On/Note Off state-management path. The role of the counter and exact meaning of the copied XDATA blocks still require dataflow tracing.

## 5. Hardware/audio boundary — not yet proven

`0x7861` accesses external XDATA addresses `0x7CF0`, `0x7CF1`, and `0x7CF4`, uses ROM tables `0x3693–0x3697`, and writes results including `0x296E`, `0x2067`. `0x44BA` also reads `0x7CFE`. The physical device behind the `0x7Cxx` window is not identified by this firmware listing alone. Do not label these addresses as DSP registers until a hardware map or stronger cross-reference is found.

## Next reverse-engineering targets

1. Trace `0x4100 -> 0xB9C2 -> 0xB437` and identify what structured data is copied and where it is consumed.
2. Trace `0x5671` and its callees (`0x4256`, `0x51B6`, `0x3ECE`, `0x9968`) to determine whether note events become per-part voice commands.
3. Cross-reference all writes and reads around XDATA `0x7C00–0x7CFF` and establish the actual hardware boundary.
