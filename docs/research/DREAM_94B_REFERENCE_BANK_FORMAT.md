# Dream 94B reference-bank container inspector

Status: a structural parser for a third-party reference format. This is **not** evidence that TechnoX uses a Dream 94B bank, that its IC51 resource streams are .94B data, or that the external processor in TechnoX is SAM9407.

## Evidence and boundary

A public Q-Chord/SAM9713 research project records Dream sound-bank workflows and links the SAM9407 programmer references. Its checked-in reference bank `GMSBK040.94B` has a top-level chunk sequence that can be read as:

| Chunk | Offset in reference file | Declared payload size |
|---|---:|---:|
| PRIO | 0 | 10 |
| IMAP | 18 | 1,570 |
| RLOC | 1,596 | 26,598 |
| PARA | 28,202 | 35,090 |
| SAMP | 63,300 | 442,096 |

The file ends immediately after the SAMP payload at 505,404 bytes. This is a **reference sample bank from another Dream-family application**, not a TechnoX factory bank. See [the Q-Chord project soundbank notes](https://github.com/partlyhuman/qchord/blob/ed279145c1b88cb2f3eb8f174a43d767987864bd/docs/soundbank.md) and [its GMSBK040.94B sample](https://github.com/partlyhuman/qchord/blob/ed279145c1b88cb2f3eb8f174a43d767987864bd/soundfont/GMSBK040.94B).

General .94B references describe the container as RIFF-like with PRIO, IMAP, RLOC, PARA and SAMP sections. The purpose of this tool is deliberately limited to **container validation and byte integrity**. It does not interpret instrument regions, relocation entries, sample encoding, loops, envelopes, rates, or playback commands. Unknown chunks are retained in the report rather than discarded.

## Parser behavior

Run:

```bash
python3 tools/inspect_dream_94b_bank.py candidate.94b --json

# Extract the SAMP chunk as opaque bytes, preserving its exact payload.
python3 tools/inspect_dream_94b_bank.py candidate.94b \\
  --extract-samp candidate.samp.bin --json
```

The inspector:
- checks each 4-byte chunk tag and little-endian 32-bit payload length;
- validates file bounds, two-byte padding for odd-sized payloads, a first PRIO chunk and required IMAP/RLOC/PARA/SAMP chunks;
- rejects duplicate required chunks, truncation and incomplete banks;
- streams through SAMP with bounded memory and reports its byte count, SHA-256 and 256-bin byte histogram;
- reports unknown chunk tags and explicitly marks audio decoding and TechnoX compatibility as false;
- optionally extracts the exact SAMP payload using bounded-memory streaming, verifies its SHA-256 against the inspected chunk, writes atomically, and refuses to overwrite the source bank.

The extracted `.samp.bin` is deliberately **opaque**. Its bytes have not been proven to be signed PCM, and extraction does not interpret IMAP regions, RLOC fixups, PARA instrument parameters, sample codecs, loop points, or address units. It must not be loaded into the TechnoX sample backend as though it were a decoded waveform image.

## What this enables next

Once a real TechnoX memory image or verified bank file is obtained, use an inspector like this to establish its exact container/byte layout before writing a decoder. Public Dream-bank research documents that relocation data must be updated when instrument/sample records are moved; copying sample bytes alone cannot preserve instrument address references. Do not feed a random .94B sample bank into the plugin as though it were the original TechnoX resource store. The next real-audio milestone remains: one verified TechnoX waveform, one validated address/loop map, and one matching factory program/voice.
