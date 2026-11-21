# TechnoX sound engine research

## Documented architecture

The TechnoX owner's manual identifies the synthesis system as **M.A.S.S. — Multi Algorithm Sound Synthesis**. It specifies 21-voice polyphony and 16-part multitimbrality. The April 1995 *The Mix* review independently describes 6 MB of sample memory, 512 ROM patches and 24 ROM drum sets. These historical specifications are materially more reliable than repeating later web listings that say 64 MB without explaining whether they mean megabits or megabytes.

The manual documents a part-level low-pass filter with resonance, pitch/vibrato controls, amplitude envelope controls, a modulation matrix, arpeggiator, drum parameters, and two effect processors. The review characterizes FX1 as room/reverb/delay and FX2 as chorus/phaser/delay/Wah/EQ family effects.

## What can be implemented from documentation

1. 16-part MIDI channel state, bank/program selection and controller mapping.
2. 21-voice allocation and per-part settings.
3. Sample playback voice structure, once sample encoding, sample headers/boundaries and address mapping are established.
4. Per-voice amplitude envelope and pitch controls.
5. Part-level resonant low-pass filter, modulation routing and arpeggiator.
6. Two effect buses with documented algorithm families, while marking parameter curves as approximate until measured.

## Critical missing evidence

- MAME currently exposes only one 128 KiB IC51 dump as a `samples` region. Its driver has no sound implementation. That dump cannot represent a documented 6 MB waveform memory by itself.
- The IC4 firmware is recoverable after MAME's documented address descrambling, but its code alone does not identify the proprietary audio processor or reconstruct the waveform ROM.
- The current repository has no PCB photographs, complete ROM inventory, schematics, sample-memory chip dumps, hardware audio captures, or MIDI reference recordings.

## Required next evidence

Photograph both sides of the complete TechnoX main PCB. Every chip marking and board revision should be readable. Include close-ups of ICs with labels such as ROM, EPROM, DSP, ASIC, SRAM and DAC. Dump every waveform ROM found, not just IC4 and IC51. A short dry audio capture with the exact MIDI notes, velocities and controller automation is also highly valuable.

## Implementation rule

Do not fabricate a sound engine by mapping the 128 KiB IC51 bytes to guessed PCM or ADPCM. Build the sample decoder only after the encoding and sample address tables are evidenced. Until then, a generic subtractive test oscillator may be useful for validating plugin audio plumbing, but it must be clearly labelled as a test tone, not TechnoX emulation.

## Sources

- TechnoX Owner's Manual, printed page 51 and parameter chapters: https://www.bn1studio.co.uk/wp-content/uploads/2022/05/quasimidi-technox-manual-478196.pdf
- Rob Green, “Revolt into style”, *The Mix*, April 1995: https://www.muzines.co.uk/articles/revolt-into-style/12193?theme=2
- MAME skeleton driver: https://github.com/mamedev/mame/blob/master/src/mame/skeleton/qmquasar.cpp
