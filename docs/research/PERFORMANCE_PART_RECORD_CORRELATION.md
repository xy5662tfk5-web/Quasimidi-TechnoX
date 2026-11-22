# TechnoX Performance-to-runtime Part record correlation

Status: address-level correlation confirmed from IC4 v1.05 disassembly. This does not identify the physical device behind the XDATA window or the audio algorithm.

## 1. Runtime table established by firmware

Routine `0xB665` computes a record base as:

```
record_base(index) = 0x7C15 + 0x18 * index
record_size         = 0x18 (24 bytes)
```

Startup code at `0x35C7–0x35D7` calls this initializer for indices 0 through 15. The table therefore occupies:

- index 0: `0x7C15–0x7C2C`
- index 11: `0x7D05–0x7D1C`
- index 12: `0x7D1D–0x7D34`
- index 13: `0x7D35–0x7D4C`
- index 14: `0x7D4D–0x7D64`
- index 15: `0x7D65–0x7D7C`

Correction: the address ranges above follow the formula; the four Performance payload blocks are copied to `0x7D35–0x7D94`, which correspond to record indices **13–15 plus the preceding index 12**:
- index 12: `0x7D35–0x7D4C`
- index 13: `0x7D4D–0x7D64`
- index 14: `0x7D65–0x7D7C`
- index 15: `0x7D7D–0x7D94`

The 16-record table therefore spans `0x7C15–0x7D94`; `0x7D95` is the next byte and is independently read by firmware as a status/condition flag.

## 2. Exact overlap with the 169-byte Performance image

The firmware's Performance loader at `0xB475` copies four 24-byte Part blocks to:

- Part payload 0 → `0x7D35–0x7D4C`
- Part payload 1 → `0x7D4D–0x7D64`
- Part payload 2 → `0x7D65–0x7D7C`
- Part payload 3 → `0x7D7D–0x7D94`

These are exactly runtime record indices 12, 13, 14 and 15, respectively, because:

```
0x7C15 + 24 * 12 = 0x7D35
0x7C15 + 24 * 13 = 0x7D4D
0x7C15 + 24 * 14 = 0x7D65
0x7C15 + 24 * 15 = 0x7D7D
```

This is a byte-for-byte address-layout match, not an inference based only on similar field values. The existing host-side `MASSPerformanceImageStore` representation (8-byte name + 65-byte Common block + four 24-byte Part blocks = 169 bytes) correctly preserves the four payload records that firmware places in runtime slots 12–15.

The manual describes a 16-Part multitimbral instrument and the stored Performance data contains four Part blocks. The most conservative interpretation is that the Performance's four stored Parts populate the four runtime records reserved at indices 12–15, while the other 12 records are initialized/used by other firmware contexts. Do not assume that all 16 records have identical semantics until their individual callers and field readers are traced.

## 3. Adjacent memory regions

The loader and initializers also establish this contiguous layout:

- `0x7BD4–0x7C14`: 65-byte Common block (inclusive).
- `0x7C15–0x7D94`: 16 × 24-byte runtime records.
- `0x7D95`: separate status/condition byte read by MIDI receive and other code.

There is also a separate 21-byte prefix `0x7C00–0x7C14) in the same XDATA window. Firmware initialization writes known values to `0x7C05`, `0x7C06`, and `0x7C10–0x7C14`; these accesses may be control/status registers or a shared structure. Their physical interpretation remains open. Do not merge the 21-byte prefix into the 65-byte Common block: the Common block starts at `0x7BD4` and ends immediately before record index 0.

## 4. What this proves—and what it does not

Confirmed:
- There are 16 indexed 24-byte records initialized by `0xB665`.
- The records occupy `0x7C15–0x7D94`.
- The four 24-byte Part payloads in the 169-byte Performance image are copied to records 12–15.
- The byte immediately following the table, `0x7D95`, is read as a status/condition value by firmware.

Not established:
- Whether the `0x7Cxx` XDATA window is implemented as ordinary shared RAM, memory-mapped registers, dual-port RAM, or a window into another processor.
- Whether record indices 0–11 represent the remaining multitimbral Parts, temporary descriptors, sequencer channels, or a mixture.
- The identity of the 80-pin QFP or the sound-generation device.
- The meaning of each record byte beyond fields already independently mapped by the Part model.
- Any waveform codec, oscillator algorithm, or original DSP implementation.

## 5. Next trace targets

1. Compare writes and reads to offsets `+0x00…+0x17` for indices 0–15, especially after Performance load and MIDI Note On.
2. Identify all call sites of `0xB665` besides startup and classify why indices 0, 1 and 4 are initialized again.
3. Trace all accesses to `0x7D95` and `0x7CEF` alongside the 24-byte record updates.
4. Determine whether `0x7C00–0x7C14` is a separate device-control header or firmware-side control structure.
5. Keep the audio backend independent of unverified chip assumptions; do not implement a SAM9407 interface without package/bus evidence.
