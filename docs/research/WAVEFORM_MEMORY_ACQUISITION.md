# TechnoX waveform-memory acquisition checkpoint

## What is known

- Public TechnoX specification listings report **64 MB of waveform memory** ([Japanese specification](https://piano.tyonmage.com/quasimidi/technox.html), [Vintage Synth Explorer](https://www.vintagesynth.com/quasimidi/technox)).
- The currently referenced public MAME TechnoX set listing contains only the IC4 and IC51 firmware ROMs (each 128 KiB); it does not provide a waveform-bank dump ([MAME set listing](https://www.planetemu.net/en/rom/mame-roms-merged/technox)).
- The project's IC51 stream catalog and the 16-KiB remapped startup-memory image are control/resource evidence. They are **not** a decoded PCM bank.
- Neither the exact physical memory topology nor the actual sample encoding/address map has been established. SAM9407 remains a candidate until the board marking and host wiring are verified.

The advertised capacity is a target for investigation, not an assumed exact file size. Do not pad, concatenate, byte-swap, or label candidate data as PCM merely to reach 64 MB.

## Required capture package

For each board revision, preserve:

1. Sharp photos of both sides of the board and readable markings on every large memory/DSP IC.
2. One separate raw dump per ROM device, with chip marking, package, pin-1 orientation, programmer model and read settings.
3. For RAM/DRAM or inaccessible mapped memory, a documented bus/capture method and the address range actually read.
4. Exact byte count, CRC32 and SHA-256 for each image; repeat the read and compare hashes.
5. The physical chip order and any lane/address mapping only when established from pin wiring or a measured bus trace.

Never read or remove a device from a powered board. Do not select a voltage or programming algorithm by package appearance alone.

## Integrity inspection

Run the evidence-first inspector against each candidate image:

```bash
python3 tools/inspect_technox_waveform_dump.py path/to/candidate.bin --json
```

When comparing a repeated read:

```bash
python3 tools/inspect_technox_waveform_dump.py second-read.bin \
  --expected-size 67108864 --expected-sha256 <sha256-from-first-read>
```

Use `--expected-size` only after the physical device capacity and intended combined image layout are verified. The inspector streams the file using bounded memory and reports size, CRC32, SHA-256 and a byte histogram. It deliberately performs **no PCM decoding**.

## Playback milestone after acquisition

Only after the dump is verified:

1. Identify the correct chip/lane/address assembly and confirm the resulting image size.
2. Determine sample encoding, byte order, address units, sample rate/clock, loop representation and compression from ROM structures or hardware captures.
3. Decode one known sample and add a byte-exact golden vector.
4. Connect that sample to one IC51 program descriptor and one voice.
5. Compare the rendered note against a dry recording from a physical TechnoX.

Until then, no sample address, codec or loop fields should be guessed in the real-time backend.
