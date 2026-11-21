# Engineering plan

## Phase 1 — interface foundation
1. Render the supplied SVG at its native 1200 × 112 aspect ratio.
2. Implement a dynamic two-line LCD overlay.
3. Add hit targets for POWER, four menu keys, PARAMETER, VALUE and VOLUME.
4. Keep UI state separate from audio state so the editor can be tested without ROM assets.

## Phase 2 — format and data handling
1. AU, VST3 and Standalone builds for Intel macOS.
2. MIDI input/output and channel/part selection.
3. SysEx parser with strict length/checksum validation where applicable.
4. Patch and performance-bank import/export; distinguish documented data from inferred data.

## Phase 3 — sound engine research
1. Identify CPU/DSP and memory devices from service documentation and verified board photographs.
2. Determine whether the v1.05 archive is executable firmware, EPROM images, or a package containing multiple components.
3. Locate lawful PCM ROM dumps or user-provided authorized data; verify sizes and hashes.
4. Research voice allocation, sample playback, filtering, modulation and effects.
5. Implement only verified behavior; label approximations clearly.

## Phase 4 — validation
1. Unit tests for SysEx and patch parsing.
2. AU validation and Logic Pro X host testing.
3. MIDI multi-part stress tests and voice-stealing tests.
4. Audio comparisons against hardware reference captures.
