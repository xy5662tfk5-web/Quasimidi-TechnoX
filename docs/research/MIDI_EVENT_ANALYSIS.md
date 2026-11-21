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

## 6. General Note On/Off dispatch: 0x57A8 -> 0x5671

The queue-1 parser in `0x5B92` assembles status plus two data bytes and calls `0x57A8` with `R7=status`, `R5=data1`, `R3=data2`.

- `0x57A8` splits the status byte: low nibble (MIDI channel) is saved at direct RAM `0x63`; high nibble (message class) at `0x60`.
- Message class `0x09` is Note On. Nonzero data2 increments XDATA `0x3039`; zero velocity follows the release/decrement branch.
- Message class `0x08` is Note Off and follows the decrement/release branch.
- After state handling, `0x57A8` reaches `0x593D`, which interprets XDATA `0x7BD5` and conditionally dispatches event IDs `0x0C–0x0F` to `0x5671`.
- `0x5671` receives the original status/data values, branches on data1 values, and in several paths calls `0x4256`, `0x51B6`, `0x3ECE`, or `0x9968`.

This is the broader channel Note On/Off path, separate from the dedicated channel-10 byte-pair handling above. It is the strongest current route to follow for the synth Parts.

## 7. Part/parameter staging and data-copy helpers

- `0x4256` stores a two-byte selector/address derived from its first argument into XDATA `0x2B98–0x2B99`; it also reads channel-indexed values from the `0x7Cxx` external window, uses `0x3700`, and performs range/offset arithmetic. It is a staging/translation routine, not proven audio output.
- `0x4100` maintains XDATA state `0x3039`, `0x304B`, `0x303A`, and `0x3050`; its active branch calls `0xB9C2`.
- `0xB9C2` iterates over indexed blocks and calls `0xB437`.
- `0xB437` uses a base at XDATA `0x0200`, computes an indexed source pointer, then copies structured blocks into XDATA windows beginning around `0x62CD`, `0x7BD4`, and `0x7D35`. This looks like indexed parameter/resource staging, but the copied block formats and consumer remain to be identified.

## Updated next targets

1. Trace `0x5671 -> 0x4256/0x51B6/0x3ECE` to map MIDI note data to per-Part staging.
2. Trace the `0x0200` source table and `0xB437` copy destinations; determine whether the source is a voice/part descriptor table.
3. Follow the `0x7BD4`, `0x7Dxx`, and `0x62xx` blocks to their readers and writes into the `0x7Cxx` peripheral window.

## 8. Corrected interpretation of 0x5671 and the four internal event slots

The caller at `0x597E–0x59B7` does not pass a raw MIDI status as `R7`. It synthesizes event IDs `0x0C, 0x0D, 0x0E, 0x0F` in `R7`, passes the decoded MIDI message class from `RAM[0x60]` in `R5`, the first data byte from `RAM[0x61]` in `R3`, and saves the second data byte in `RAM[0x67]`.

Inside `0x5671`:
- `R5=0x09` selects the Note On branch (velocity zero is redirected to the release/decrement logic).
- `R5=0x08` selects the Note Off branch.
- Other message classes have distinct branches (including control/other message handling); therefore this routine is an event dispatcher, not itself a raw MIDI parser.
- Event ID `R7` selects per-event state slots. It is used in address arithmetic for per-slot XDATA at `0x2FC7 + eventID` and `0x7C15 + 0x18*eventID`.
- The routine writes two values to per-slot XDATA offsets `+0x15` and `+0x16`, calls `0x418E`, then calls `0x3ECE`. `0x3ECE` clears fields at `0x2040+eventID`, `0x2020+eventID`, `0x2FF9+eventID`, and `0x303B+eventID`.

This is strong evidence for four event/part-like slots, but the numeric IDs `0x0C–0x0F` alone do not prove that they correspond one-to-one with the four user-visible Parts. That mapping still needs confirmation against parameter setup and Performance/Part selection code.

## 9. What 0x418E does for Note On

When `0x418E` receives `R7=0x09` (message class Note On), it:
- checks and clamps `R3` to at most `0x1F`;
- calls `0xBC3F` with the clamped value;
- writes `0x06` to XDATA `0x2975`;
- reads XDATA `0x3719`, transforms it, and calls `0xB3BC`;
- writes the returned pair into `0x2971–0x2972`.

This is a parameter translation/staging step associated with Note On, not proof of direct sample playback. The companion non-Note-On branch clamps a different input and updates a per-event XDATA field.

## 10. Revised next targets

1. Decode `0xBC3F`, `0xB3BC`, and their table inputs to identify what Note On values are transformed into.
2. Trace readers of `0x2971–0x2975` and writes to the `0x7C15 + 0x18*eventID` blocks.
3. Compare event IDs `0x0C–0x0F` with the firmware's Part-selection and Performance parameter mapping before claiming a four-Part correspondence.

## 11. Note On staging is a parameter conversion, not a sample trigger

Further inspection of `0x418E`, `0xBC3F`, and `0xB3BC` narrows the interpretation:

- `0x418E` receives the event-slot ID in `R7`, MIDI message class in `R5`, and the first data byte in `R3`. On Note On (`R7` at entry is the message class value `0x09`), it clamps `R3` to `0x1F`, calls `0xBC3F`, and then writes derived values to `0x2971–0x2972` plus a selector `0x06` to `0x2975`.
- `0xBC3F` is a bulk table-copy routine. It selects ROM table data using the argument, copies a block into XDATA beginning at `0x3710`, and for indices at/above `0x18` uses a second table region beginning around code address `0x3CAB`. It is not itself a voice allocator.
- `0xB3BC` reads a two-byte value from a table via `0xB385` and `0xB395`; `0x418E` uses it to derive the pair at `0x2971–0x2972`.
- The only reachable readers of `0x2971` in this disassembly are in `0x446B` (which reads `0x2971–0x2972` and `0x2975`, transforms them, and submits descriptor `09 29 6C` to `0x1494`) and adjacent parameter-update code. This is a descriptor/parameter transaction chain, not proof of audio waveform access.

Consequently, the best-established current chain is:
`MIDI Note On/Off -> 0x57A8 -> 0x593D -> event slots 0x0C..0x0F -> 0x5671 -> 0x418E -> lookup/table conversion -> 0x446B -> 0x1494`.
The final hardware voice trigger and sample/DSP interface remain unresolved. This is now the next reverse-engineering boundary.
