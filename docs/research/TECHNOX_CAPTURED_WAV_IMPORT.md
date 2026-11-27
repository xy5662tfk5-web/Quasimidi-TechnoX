# Importing captured WAVs into the TechnoX sample playback backend

This is a practical path for **audio captures** made from a physical TechnoX or other user-supplied PCM WAVs. It does not dump/decode the instrument's internal waveform ROM and does not claim that the generated addresses match IC51 factory resources.

## Build a raw sample image and matching map

Place uncompressed mono/stereo PCM WAV files in one folder. Supported source depths are 8, 16, 24 and 32 bit; sample rates must be 4,000–192,000 Hz. The importer sorts files alphabetically, downmixes stereo to mono and writes signed 16-bit little-endian PCM.

```bash
python3 tools/build_sam9407_sample_bank.py ./TechnoX-Captures \
  --waveform-out ./waveforms.raw \
  --map-out ./sample_map.json \
  --bank 0 --start-program 0 --root-note 60
```

Each WAV becomes one sequential program in the selected bank. The map stores its own sample rate and exclusive start/end word addresses. The importer checks the 256-program bank limit and 25-bit word-address limit.

## Automatic plugin loading

Copy both generated files to:

`~/Library/Application Support/Quasimidi TechnoX/SampleBank/`

Use these exact names:

- `waveforms.raw`
- `sample_map.json`

The plugin attempts to load the pair during processor construction, before real-time audio begins. If either file is absent or validation fails, the external capture bank remains unloaded and the built-in IC4/IC51 resources are not modified.

The samples are available as sequential bank/program entries from the generated JSON manifest. This makes the current 16-bit sample-memory bus and voice renderer directly testable with actual captured sound, without a diagnostic oscillator.

## What this does not provide

A recorded one-shot may already contain the physical instrument's filter, envelopes and effects. Replaying it verifies the memory read/playback path, but does not reconstruct original multisamples, pitch zones, envelopes, filter behavior, modulation or FX DSP. The missing internal multi-megabyte waveform image and validated IC51-to-waveform map are still required for authentic factory preset synthesis.
