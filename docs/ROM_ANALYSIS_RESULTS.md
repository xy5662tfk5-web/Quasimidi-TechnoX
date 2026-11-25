# TechnoX ROM audit — v1.05

## Verified dump identity

The two repository ROM files were read as binary data and independently checked for length and CRC32. Both match the MAME TechnoX set:

| Device | File | Size | CRC32 | MAME SHA1 |
|---|---|---:|---|---|
| IC4 | `technox_IC4_mx27c1000_v105.bin` | 0x20000 | `4d393cd9` | `9ee853f1f2e88fbfc86589c4d0f91003d0ff146f` |
| IC51 | `technox_IC51_nm27c010_v105.bin` | 0x20000 | `d669570e` | `0645a1b022ed25a836152933e6df32da4448f49a` |

The CRC32 values were computed from the repository bytes and match the values in MAME's `src/mame/skeleton/qmquasar.cpp`. The SHA1 values above are the MAME reference values; run `tools/rom_audit.py` to independently recalculate SHA1 locally.

## IC4 address descrambling

MAME applies `init_quasar()` to TechnoX and Quasar. For each 256-byte block, it copies the original block and reads source byte:

`bitswap<8>(offset, 1, 2, 3, 6, 5, 7, 1, 0) ^ 0x40 ^ ((offset & 0x11) << 3)`

The transform is applied to the program ROM only, not IC51. The descrambled IC4 begins:

`02 EB 32 FF FF FF FF FF FF FF FF 02 27 F7 FF FF FF FF FF 02 3D EE FF FF FF FF FF 02 28 41 ...`

The first opcode `02` is an 8051-family LJMP instruction, and the following address bytes point into the ROM. The interrupt-vector area is consequently structured after the transform. This is strong evidence that the MAME transform is being applied in the correct direction.

## IC51 preliminary findings

MAME declares IC51 as the `samples` region, but its TechnoX driver has no audio implementation and marks the machine `MACHINE_NO_SOUND | MACHINE_NOT_WORKING`. Therefore MAME does not identify the sample codec, sample table, sample boundaries, or playback hardware.

A byte-frequency pass over the 128 KiB dump shows:
- Shannon entropy: approximately 4.805 bits/byte.
- All 256 byte values occur.
- 0x00: 34,850 bytes; 0xFF: 30,663 bytes.
- The dump is not plain text. These statistics do not establish PCM, ADPCM, compression, or a complete waveform library.

The manual advertises 512 single sounds and 16-part multitimbrality, but IC51's 128 KiB size alone cannot substantiate that it contains all sound material. Further ROM inventory and board photographs remain necessary.

## Reproducibility

Run:

```bash
python3 tools/rom_audit.py technox_IC4_mx27c1000_v105.bin technox_IC51_nm27c010_v105.bin
```

The script validates size, CRC32 and SHA1 without changing either input, writes a separate `IC4_descrambled.bin`, and prints preliminary IC51 statistics.

## References

- MAME driver: https://github.com/mamedev/mame/blob/master/src/mame/skeleton/qmquasar.cpp
- TechnoX owner's manual (SysEx and MIDI): https://www.bn1studio.co.uk/wp-content/uploads/2022/05/quasimidi-technox-manual-478196.pdf
