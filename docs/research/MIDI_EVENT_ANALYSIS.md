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

## 12. Correction: 0x418E has two call contexts; do not label its 0x09 branch as the general MIDI Note On path

There are two reachable direct call sites for `0x418E`:

- `0x3624`: caller supplies `R5=0x02`, `R3=0x38`, and `R7` from the preceding setup routine. This is the context in which `0x418E` can take its internal `R7==0x09` branch.
- `0x572F`: the general MIDI event dispatcher supplies `R7=eventID` (`0x0C–0x0F`), `R5` from a per-event value, and `R3=RAM[0x06]`. This call takes the non-`0x09` branch in `0x418E`.

Therefore, the previous section's description of the `0x418E` `R7==0x09` branch as a direct Note On staging path was too strong. It is a separate setup/event-type branch. The general MIDI route is still confirmed through `0x57A8 -> 0x593D -> 0x5671`, but the exact function of its `0x572F -> 0x418E` staging call must be described from its non-`0x09` branch and downstream consumers.

The safe current conclusion: four event-slot IDs are dispatched and written into per-slot structures; their exact mapping to four Parts and the final hardware voice command are not yet proven.

## 13. Exact non-0x09 branch of 0x418E used by MIDI event slots

The `0x572F` caller enters `0x418E` with event ID `0x0C–0x0F`, so the relevant branch is `0x41D6–0x4255`, not the `R7==0x09` branch.

That branch:
- clamps its second argument (`R5`) to `0x03`;
- calls `0x6AA6` with event ID, clamped selector, and third argument;
- uses ROM table at `0x5C3D` to obtain a mapped value;
- writes that mapped value to XDATA address `0x2909 + 0x0C*eventID`;
- uses the mapped value and a flag bit from the selector to form an index/value pair, calls `0xB3BC`, and writes the resulting pair to `0x2905 + 0x0C*eventID`.

This is a concrete per-event-slot parameter mapping operation. It does not yet show a hardware voice trigger; next trace is `0x6AA6`, table `0x5C3D`, and readers of the `0x2905/0x2909` per-slot records.

## 14. Important correction: the general MIDI Note On path is 0x5671 -> 0x4256, not 0x418E

Re-reading the full dispatcher shows that `0x5671` first classifies the MIDI message class in `R5`. For Note On (`R5=0x09`), it reaches `0x5697`. If the per-slot enable/selection checks pass, it calls `0x4256` with:
- `R7 = event slot ID (0x0C–0x0F)`
- `R5 = first MIDI data byte (note number)`
- `R3 = second MIDI data byte (velocity)`

This is the directly relevant Note On route. `0x418E` is called later from a different event/setup branch around `0x572F`; it must not be presented as the main Note On handler.

### What 0x4256 does with a Note On

- Saves the event ID, note number and velocity.
- Computes the per-slot record base `0x2900 + 0x0C*eventID`, stores it in XDATA `0x2B98–0x2B99`.
- Reads a per-slot value from `0x7C00 + 0x18*eventID + 0x1C`, combines it with XDATA `0x3700`, and normalizes the resulting 16-bit value into a bounded address/range before storing it in the per-slot record.
- Reads another per-slot field at `0x7C00 + 0x18*eventID + 0x26`, passes it through `0xABBF`, and stores the mapped result at `0x2901 + 0x0C*eventID`.
- Reads further per-slot control fields at offsets `0x17`, `0x19`, `0xD7`, and `0xA2`. Depending on their values, it calls `0xAC00`, `0xAC88`, `0x695A`, and fills the 12-byte per-event resource-selection record at `0x2900 + 0x0C*eventID`; the pointer to that record is stored at `0x2B98–0x2B99` before `0x1494` consumes it.

This proves that Note On is converted through per-slot parameter records and descriptor transactions. It still does not prove that the final descriptor is a direct sample/DSP start command; that requires tracing the descriptor's downstream consumer and physical interface.

## 15. 0x6AA6 and the threshold table at 0x73FA

The separate helper `0x6AA6` is reached from the non-`0x09` branch of `0x418E`. It receives event ID, a selector (clamped to 0–3), and a value. It scans 14 ascending 16-bit thresholds in code memory at `0x73FA`:

`0000, 005F, 0100, 0135, 0149, 0157, 0200, 022A, 023B, 0247, 025E, 0300, 030C, 036F`.

The first threshold greater than or equal to the input selects a bucket; the routine writes the preceding bucket index to XDATA `0x3063 + eventID`. If no threshold matches, it returns without that write. This is a quantization/range lookup for an event-slot parameter, not a MIDI Note On or a sample trigger.

The four-entry lookup table at `0x5C3D` begins `07, 07, 06, 06`; `0x418E` uses it to map the clamped selector before writing the mapped value to `0x2909 + 0x0C*eventID`.

## 16. Confirmed next boundary

The most promising chain to follow now is:
`0x5671 (Note On) -> 0x4256 -> per-slot XDATA 0x7Cxx -> 0xABBF / 0xAC00 / 0xAC88 -> descriptor 0x1494 -> 0x16C3 / 0x189D`.

The key unresolved question is what the per-slot `0x7Cxx` fields, the 12-byte resource-selection record, and the IC51 descriptor/command-stream data mean at the hardware boundary.

## 17. Downstream helpers used by the Note On descriptor path

The helpers reached from `0x4256` have now been classified at the level supported by the disassembly:

- `0xABBF` is a value remapper. It handles zero specially; maps selector `R5=7` to constant `0x55`; for values above 7 it reflects the input around `0x80` and clears selected internal flags; otherwise it indexes a code-memory lookup table beginning at `0xA8BF`, with 128-byte selector pages. It is a translation function, not a hardware write.
- `0xAC00` computes a per-event byte index as `16*eventID + floor(selector/8)`, selects XDATA page `0x35`, then dispatches through a 14-way computed jump table. It is a bit/field update helper for an event-slot data structure.
- `0xAC88` uses the same per-event indexing and XDATA page `0x35`, but its computed jump table has 17 cases and it also receives an extra mask/value in `R3`. It is a companion field/bit update helper.
- `0x695A` reads per-slot mode `XDATA[0x7C00 + 0x18*eventID + 0x19]` and chooses a translated value using table `0x72DA` and event/selector fields. This feeds the descriptor path.

These helpers manipulate and translate structured per-slot state. The `0x1494` transaction consumes a 12-byte per-event resource-selection record from XDATA page `0x29`, follows its IC51 descriptor pointer, and stages the downstream page-selected resource exchange. Keep this record distinct from the variable-length descriptor stored in IC51. The exact physical target of the XDATA page `0x35` and the `0x7Cxx` windows remains unidentified without a board schematic or trace.

## 18. 0x1494 -> 0x16C3: concrete four-register exchange sequence

The descriptor path has a much more specific endpoint than a generic "DSP write". After the descriptor is decoded and its slot/page selected, `0x16C3` performs a byte-level exchange through the selected XDATA page using indirect offsets:

| Offset | Operation in 0x16C3 | Confirmed meaning |
|---|---|---|
| `FB` | Read old status; write `0x80` or `0xC0` when flag E7 is set; otherwise write `0` | Status/command handshake byte |
| `FC` | Write high nibble of `RAM[0x28]` | Mode/command field |
| `FE` | Write low byte of pointer `RAM[0x3C]` | Descriptor/data pointer low |
| `FF` | Write high byte of pointer `RAM[0x3D]` | Descriptor/data pointer high |

Before the exchange, `RAM[0x36:0x37]` is incremented by two. The pointer is also written to selected-page offsets `0x61–0x62`, then `0x189D` is called to read a descriptor/record from page `0x28` and the selected XDATA pointer. After that, the routine samples status at `FD`, updates flags E0–E2 from the returned byte, clears E7/E6 and writes the final value back to `FD`.

This establishes a mailbox-like register protocol with status, mode, and 16-bit pointer fields. It does **not** identify the physical chip or prove that these are audio sample/voice registers. The selected XDATA page is derived from `RAM[0x2D]`; the hardware address decode is still unknown.

## 19. What 0x189D actually stages

`0x189D` reads four fields from page `0x28` at offsets based on `0x2868 + RAM[0x28]*0x80`: offsets `0x00`, `0x10`, `0x20`, and `0x40` go to internal RAM `0x41`, `0x43`, `R3`, and `R4`. It then reads a variable record from the 16-bit pointer in `RAM[0x36:0x37]`, copying fields into `RAM[0x38–0x3B]` while updating internal flags B5/B4/B1. The routine therefore stages a structured record and its mode fields; it is not a PCM decoder.

## 20. Critical consequence for the emulator

The firmware evidence currently supports implementing the host-side transaction/mailbox protocol and descriptor staging, but not the audio chip's internal synthesis algorithm. Keep the existing oscillator backend explicitly marked as a test sound source. Do not label it as the original TechnoX/M.A.S.S. engine until the peripheral identity, command semantics, sample/voice memory map, and waveform decoding are established.

## 21. A second, distinct external interface: P2=0x80 exposes a four-byte command window

The earlier `0x16C3` sequence uses a page selected by `RAM[0x2D]`. It must be kept separate from the fixed page-`0x80` interface.

The routine `0x0DB7` is now fully confirmed as a four-byte transaction:
- caller selects `P2=0x80` (SFR `0xA0`);
- writes `R5` to `MOVX @R0` at offset `0x00` (absolute XDATA `0x8000`);
- writes `R2` at offset `0x01` (`0x8001`);
- writes `R3` at offset `0x02` (`0x8002`);
- writes `R4` at offset `0x03` (`0x8003`);
- updates internal flag `0x0C` to mirror flag `E7`.

The companion `0x0DCE` conditionally changes bit 7/6 of register `0x8003`: it writes `0xC0` when E7 becomes set and `0x40` when E7 becomes clear, then reads back `0x8002`. It avoids a redundant write when the mirrored state already matches.

The transaction is invoked from multiple subsystems (`0x02C0`, `0x0A4F`, `0x0B53`, `0x0D07`, `0x0E1A`, `0x1045`, `0x110A`, `0x26C9`, `0x26DE`, `0x2767`, `0x277E`). This makes it a shared low-level device command interface, not a one-off MIDI path.

### What can and cannot be concluded

- Confirmed: four byte-wide write registers at external address window `0x8000–0x8003`, selected by 8051 port 2 = `0x80`.
- Confirmed: register 3 contains a handshake/state field using masks `0xC0`; register 2 is read immediately after state transitions.
- Not confirmed: physical chip identity, whether the target is the M.A.S.S. voice processor, a sample-memory controller, a DMA/mailbox controller, or another peripheral.
- Not confirmed: the semantics of the four command bytes. The call sites use different values, so the next step is to classify call-site tuples and correlate them with the selected slot, descriptor pointer, and status returned by register 2.

This fixed `0x8000–0x8003` window is distinct from the variable page-`0x7Cxx` parameter records and from the page-selected `FB/FC/FE/FF` mailbox sequence at `0x16C3`. Treating all three as one DSP register map would be incorrect.

## 22. 0x8000 command call-site classification

Call-site inspection separates the fixed-window writes into three families:

1. **Slot/page-derived commands** (`0x0A4F`, `0x0B53`, `0x0D07`): callers compute a 24-bit-ish value from a per-slot record, split it into `R2:R3:R4`, use `RAM[0x2B]` as `R5`, and write the four-byte tuple. The low three bits of the final byte are retained; flag E7 may be set to signal a state transition.
2. **Queue/descriptor scan commands** (`0x1045`, `0x110A`): callers scan a page/slot list and issue tuples with `R3=1`, `R2=0x80` or a computed mask, and a selector in `R5`. These appear to update or acknowledge entries, but the target's semantic role remains unconfirmed.
3. **Bulk initialization/script commands** (`0x26C9`, `0x26DE`, `0x2767`, `0x277E`): callers fetch successive 3-byte records using `MOVC`, transform the bytes, and issue repeated four-byte writes. The routine at `0x26A2` temporarily writes `0x00` to SFR `0xA8`, selects P2=`0x80`, and starts reading code memory from DPTR=`0x0000`. Because the static disassembly does not resolve the apparent bank/overlay behavior at this point, these bytes must not yet be interpreted as ordinary command-table values. This is a high-priority banked-ROM/boot-script investigation.

### Strong conclusion

The fixed `0x8000–0x8003` block is a shared command/data port used by initialization, slot/descriptor processing, and state synchronization. It is not merely the MIDI note trigger. The values sent are structured tuples, while register 3 carries the E7 handshake state. The physical peripheral and command meanings are still unknown.

## 23. Correction: 0x26A2 does not switch ROM banks

The earlier "possible bank switching" interpretation was incorrect. SFR `0xA8` is the 8051 `IE` (Interrupt Enable) register, not a ROM-bank register. In `0x26A2`, the firmware:
1. pushes `IE` to preserve it;
2. writes `0x00` to `IE`, disabling interrupts during the transfer;
3. calls `0x0E3A` to select/prepare the external interface;
4. selects `P2=0x80`;
5. begins reading code memory at `DPTR=0x0000` with `MOVC`;
6. restores the original `IE` value at `0x27F4`.

Thus this is an interrupt-protected bulk transfer from the program code space to the fixed external command window, not evidence of ROM banking. Address `0x0000` is the actual code image start (the reset vector bytes are `02 EB 32`), so the first bytes sent are literally the firmware image bytes interpreted in 3-byte records.

The first transfer loop consumes code bytes in 3-byte groups, masks the third byte to its low three bits, adds a group selector (`0x00` or `0x80`) and sends the resulting tuple through `0x0DB7`. A second phase resets DPTR to zero and repeatedly compares values read by `MOVC` with status/data read through `0x0DCE`; it advances in 3-byte record steps and has a complementary-data pass. This is consistent with a boot/program-load or command-table synchronization protocol for an external peripheral. It does not yet establish whether the peripheral executes M.A.S.S. synthesis or merely stores/relays program data.

## 24. Updated interpretation of the fixed 0x8000 interface

The fixed `0x8000–0x8003` interface is now more strongly characterized as a low-level serial/parallel mailbox or peripheral transfer port: firmware disables interrupts, selects P2 page `0x80`, sends 3-byte records encoded into the four register writes, and polls for matching/acknowledgment values. This bulk path is distinct from the per-note descriptor path, although both ultimately use the same `0x0DB7` primitive. The exact peripheral and the payload format remain unknown.

## 25. Exact transfer size and loop structure of 0x26A2 (corrected)

A cycle-by-cycle re-evaluation corrects the earlier size estimate. The routine does **not** stream 0xC00 bytes. It processes exactly 256 indexed entries, with two bank/plane selections per index:

- `R5` is initialized to `0x00` and decremented once per entry by `DJNZ R5`. Starting at zero gives 256 iterations (0x00, 0xFF ... 0x01).
- For each index, it reads six consecutive code bytes from `DPTR` and issues two four-register writes:
  - first triplet: `R2=code[n]`, `R3=code[n+1]`, `R4=(code[n+2] & 7) + 0x00`;
  - second triplet: `R2=code[n+3]`, `R3=code[n+4]`, `R4=(code[n+5] & 7) + 0x80`.
  - `R5` supplies the index in both writes.
- Six code bytes are consumed per index, so the first pass covers exactly `0x0000–0x05FF` (1536 bytes), not 0x0000–0x0BFF.
- The readback phase resets DPTR to zero and checks both selections (`R4=0x00` and `R4=0x80`) against the original code bytes. The compare loops are tight self-loops on mismatch, so a mismatch stalls the routine rather than returning a failure code.
- A second write phase again covers `0x0000–0x05FF`, but bitwise-complements each source byte before sending it; the third byte is then masked to its low three bits and combined with the bank selector.
- The final readback phase compares against the complemented source bytes, again stalling at the mismatch compare if a byte differs.

Therefore the routine performs two 1536-byte write passes (normal and complemented) and two corresponding verification passes. It is best described as an external-memory/peripheral transfer-and-integrity test or initialization handshake. It is **not yet proven** to upload executable DSP firmware. The exact meaning of the 256 index values, the two `0x00/0x80` selections, and the low-three-bit field still requires hardware evidence.

## 26. Register-level shape of each indexed transfer

The primitive `0x0DB7` writes:
- `0x8000 = R5` — index (0..255 in decrementing order);
- `0x8001 = R2` — first payload byte;
- `0x8002 = R3` — second payload byte;
- `0x8003 = R4` — selector in bit 7 plus three payload/control bits in bits 0..2.

The companion readback helper `0x0DCE` uses the E7/E0C handshake state, writes `0x40` or `0xC0` to `0x8003` when the handshake state changes, and reads back registers `0x8000–0x8002`. The verification loops compare the returned index/data bytes and the low three bits of the selector against the source ROM bytes.

This structure resembles an indexed two-plane external memory test: 256 indices × two selector values, with two full data bytes and three low-order bits per record. That is a structural description only; do not call it a specific RAM chip or assume a 19-bit physical word without schematic/chip evidence.

## 27. Startup context of 0x26A2: likely peripheral memory self-test, not DSP firmware upload

The only direct call site found for `0x26A2` is at `0x374E`. It is part of the startup/initialization sequence:
- optional interrupt configuration at `0x3748–0x374B`;
- call `0x26A2`;
- restore/configure interrupt register at `0x3751–0x3757`;
- initialize pointer/state at `0x3759–0x375F`;
- continue into `0x00F8`, `0xB56C`, `0x5EF9`, `0xB5A9`, `0x6433`, and later subsystem initialization.

The source bytes sent by `0x26A2` are literally the first `0x600` bytes of IC4 program ROM. This region includes reset/interrupt vectors, executable 8051 instructions, and a conspicuous 256-byte waveform-like table beginning at `0x001E`; it is not a separately identified DSP firmware image. The routine writes this ROM-derived pattern to 256 indices in each of two selector planes, verifies it, writes the bitwise complement, and verifies again. A mismatch causes an infinite wait at the compare instruction.

This startup position and complement/readback pattern make **external memory/interface self-test** the strongest interpretation. It may validate memory attached to a coprocessor or another peripheral, but it is not evidence that IC4 uploads executable M.A.S.S. DSP code. The physical device and purpose of the two selector planes remain unknown.

## 28. IC4 ROM contains a confirmed 64-point signed sine lookup table

A fresh byte-level inspection of the descrambled IC4 image corrects the earlier broad description of the first 0x600 bytes as a "waveform-like table". The actual layout is mixed code and data:

- `0x0000–0x001D`: reset/interrupt vectors and jump stubs.
- `0x001E–0x005D`: exactly 64 bytes forming a signed sine cycle.
- `0x005E` onward: additional mixed lookup/control data and executable code; it is not one continuous 256-byte waveform table.

The 64-byte sequence at `0x001E–0x005D` is:

`00 0C 19 25 31 3C 47 51 5A 62 6A 70 75 7A 7D 7E 7F 7E 7D 7A 75 70 6A 62 5A 51 47 3C 31 25 19 0C 00 F4 E7 DB CF C4 B9 AF A6 9E 96 90 8B 86 83 82 81 82 83 86 8B 90 96 9E A6 AF B9 C4 CF DB E7 F4`

This is a 64-step sine wave encoded as signed 8-bit samples (approximately -127..+127, with the positive and negative half-cycles meeting at zero). It is not PCM content from IC51 and is not evidence of a factory multisample bank.

### Code references to the sine table

The descrambled code contains direct `MOV DPTR,#0x001E` / `MOVC A,@A+DPTR` lookups at:
- `0x0677` and `0x0686`, in a shared calculation path around `0x0670–0x06AA`;
- `0x0C52`, immediately followed by `INC R1` and `MOVX @R1,A` (`F3`), then `RET`.

These instruction sequences prove the table is actively read by executable firmware and, in the `0x0C52` path, the resulting value is written through an indirect external-data pointer. The exact consumer of that output is not yet established: it may be a modulation/shape/control table or another peripheral-side data structure. Do not yet label it as the TechnoX audio oscillator's actual wavetable.

### Consequence for reverse engineering

We now have a real, compact mathematical waveform embedded in IC4, plus executable references to it. Next trace the callers of `0x0C52` and the surrounding `0x0670` calculation to identify the destination structure and whether this is used by modulation, voice synthesis, startup diagnostics, or peripheral initialization. Keep this separate from the unresolved 6 MB sample-memory architecture and the 128 KB IC51 dump.
## 29. Corrected control-flow interpretation of the sine-table reads

Follow-up control-flow inspection narrows the interpretation of section 28:

- `0x0C52` is **not** a standalone callable routine. It is an instruction inside a sequential code path at `0x0C18–0x0C58`: mask the computed phase with `0x3F`, read the signed sine byte from `0x001E + phase`, increment `R1`, and write the result through `MOVX @R1,A` (`F3`).
- The only direct `LCALL` xref found to the following entry `0x0C59` is at `0x08B2`; it calls the code after the sine lookup/return, not the sine lookup itself.
- The `0x0677` / `0x0686` reads sit inside a computed-dispatch region around `0x0648–0x06AA`. The dispatch table at `0x0652` contains eight 16-bit destinations (`0x0682`, `0x068D`, `0x0691`, `0x0697`, `0x06A0`, `0x0671`, `0x0682`, `0x0682`). Therefore the two table reads are branch bodies in a multi-case handler, not independent oscillator functions.
- The `0x0670` area itself is reached through this computed dispatch, and there are no direct `LCALL` xrefs to `0x0670` or `0x06AA` in the descrambled image.

**What is now proven:** the firmware computes a 6-bit phase/index, obtains a signed sine value from the 64-byte table, and writes that value to external XDATA via an indirect pointer. A second computed-dispatch handler also reads the same sine table.

**What is not proven:** the target XDATA page/device, whether the generated values are audio-rate samples, and whether this code belongs to the main M.A.S.S. oscillator rather than modulation, envelope/LFO, or peripheral setup. Keep the wavetable finding as evidence of firmware-side waveform/math generation, not as proof that the complete sound engine has been recovered.
## 30. Sine generator is gated by XDATA[0x2050]

Tracing the `0x0C1A–0x0C58` path reveals an important guard:

- `0x0C1A` sets P2=`0x20`, R1=`0x50`, then reads `XDATA[0x2050]` using `MOVX A,@R1`.
- If the byte is nonzero (`JNZ` at `0x0C20`), the routine returns immediately.
- Only when `XDATA[0x2050] == 0` does it continue through the phase/shape calculation, read `CODE[0x001E + (A & 0x3F)]`, increment R1, and write the sine value to XDATA through `MOVX @R1,A`.
- Startup routine around `0x0130` explicitly initializes `XDATA[0x2050]` to `0x3C` (`MOV DPTR,#0x2050; MOV A,#0x3C; MOVX @DPTR,A`).
- Static scan found no other direct `MOV DPTR,#0x2050` or direct immediate reference to `0x2051`; the generator uses the indirect P2/R1 addressing path.

This means the sine-writing path is disabled by its initial state and requires some later state transition to zero the gate byte. That is evidence against casually identifying it as the continuously running main audio oscillator. The routine may be an explicitly enabled modulation/diagnostic/peripheral initialization path; its caller/state transition still needs to be identified.

### Revised conclusion

Confirmed: IC4 contains a 64-entry signed sine lookup, and executable firmware can write indexed sine values into external XDATA. Not confirmed: that this path runs during normal note playback, its exact destination beyond the indirect XDATA pointer, or its role in M.A.S.S. Continue by finding all writes to page `0x20`, offset `0x50`, including indirect page/R1 accesses, and by mapping the dispatch entry that reaches `0x0C1A`.

## 31. Firmware Performance image layout: 0xB475

The full copy routine at `0xB475` reads a 16-bit source pointer from the external table at `0x0200 + 2*R7` (high byte, then low byte), then copies a fixed 169-byte record into the working XDATA areas:

- 8 name bytes to `0x62CD–0x62D4`; it writes a zero terminator at `0x62D5`.
- 65 Common bytes to `0x7BD4–0x7C14`.
- 4 groups of 24 Part bytes to `0x7D35–0x7D94`.

The name interpretation is confirmed independently: routine `0xB537` copies eight bytes from ROM `0xC509` to `0x62CD`; those ROM bytes are ASCII `Untitled` (`55 6E 74 69 74 6C 65 64`). The 17-byte name SysEx frame in `TECHNOX.MID` stores its eight name characters at frame offsets 8–15. Thus the full record is exactly `8 name + 65 Common + 4×24 Part = 169 bytes`.

The address arithmetic in the Part handlers establishes the exact Part-record base as `0x7C15 + 24*part`. For the four active Part IDs 12–15 this yields `0x7D35`, `0x7D4D`, `0x7D65`, and `0x7D7D`. Several independent accesses agree with the 24-byte `PartEditModel` layout: `0x7C00 + 24*part + 0x18` reads Part byte 3 (Level), `+0x19` reads byte 4 (Panorama), `+0x1C` reads byte 7 (Transpose), and `+0x26` reads byte 17 (Velocity curve).

The 65-byte Common block occupies `0x7BD4–0x7C14` immediately before the first Part record at `0x7C15`. This matches the 65-byte payload of the existing 74-byte Common SysEx frame (8-byte SysEx header + 65 data bytes + `F7`); each 33-byte Part SysEx frame similarly contains an 8-byte header and 24 data bytes. The 8-byte block copied to `0x62CD` is the eight-character Performance name from the name SysEx frame.

`0xB9C2` is not an eight-iteration copier: it makes one call to `0xB437`, which copies only the 8-byte name to `0x62CD`. The full 169-byte copy is `0xB475`, reached through `0xB8D3` in the initialization path of `0x4100`.

### Startup loader correction

The stream at ROM `0x38F7` is not the source of the `0x0200` pointer table and does not populate XDATA. Its three command records branch to the internal-RAM bit-operation handler at `0xEB67`; the earlier hypothesis that this startup routine decompresses the performance table into XDATA was incorrect. The origin/mapping of the external table at `0x0200` remains unresolved.

### Implementation consequence

The plugin now has a backend-facing raw Performance image store with the exact 169-byte name + 65-byte Common + 4×24-byte Part layout, and can serialize it into the firmware's record order. The existing SysEx parser feeds it directly, and edits to the mapped Common/Part controls update the image. This preserves unknown Common bytes for the future recovered DSP backend without pretending the audio algorithm or the source pointer table has been decoded.
