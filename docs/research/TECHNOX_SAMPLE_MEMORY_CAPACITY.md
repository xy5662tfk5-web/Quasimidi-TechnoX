# TechnoX sample-memory capacity and dump status

## Conflicting capacity reports

Rob Green's review of the Quasimidi TechnoX in The Mix, April 1995, states that the machine has **six megabytes of sample memory**, 21-note polyphony, 16-part multitimbrality, 512 ROM patches and 24 ROM drum sets. Several later product databases instead report **64 MB** of waveform/sample ROM. These figures conflict; neither is accepted as the physical board capacity until the installed memory devices and decoding are verified.

Source: [The Mix, April 1995 — Revolt into style](https://www.muzines.co.uk/articles/revolt-into-style/12193), especially the comparison/specification at lines 59–62 and 81–89.

## Separate device capability from installed TechnoX memory

Later pages that report 64 MB include [Vintage Synth Explorer](https://www.vintagesynth.com/quasimidi/technox) and [Sound Programming](https://soundprogramming.net/synthesizers/quasimidi/quasimidi-technox/). Separately, the SAM9407 objective specification documents a **maximum theoretical external address space of 32M 16-bit words (64 MB)**. That chip capability is not evidence that TechnoX uses a SAM9407 or populates the full range.

Until TechnoX board markings and address decoding prove otherwise, keep these concepts separate:

- contemporary 1995 review: 6 MB sample memory;
- later databases: 64 MB waveform/sample ROM;
- candidate SAM9407 address-bus capacity: up to 64 MB;
- physical TechnoX memory chip count, type, bus width and ROM/RAM composition: unresolved.

Source: [DREAM SAM9407 objective specification, February 1996](https://dosdays.co.uk/media/dream/SAM9407.PDF), pin description for WA0–WA24.

## Public ROM availability

The publicly indexed MAME TechnoX archive lists only two ROM images: IC4 mx27c1000_v105.ic4 and IC51 nm27c010_v105.ic51, together 160 KB. These are the control/firmware images already used by this project; this listing does not include a multi-megabyte waveform dump.

Source: [Planet Emulation — TechnoX MAME ROM contents](https://www.planetemu.net/en/rom/mame-roms-merged/technox).

## Engineering consequence

The new SAM9407 memory-bus model implements the candidate device's documented 25-bit word-address space and S1/S0 external-cycle classes. Its address ceiling is **not** configured as the TechnoX sample-memory size. A real map must come from the board and a dump/capture. Do not assume either 6 MB or 64 MB from secondary listings alone, and do not extrapolate waveform addresses from the 128-KiB IC51 resource ROM or the 16-KiB startup image.

The next audible milestone remains one factory waveform with a verified address, format, loop and pitch map, rendered through the memory bus and compared with a physical TechnoX/reference capture.
