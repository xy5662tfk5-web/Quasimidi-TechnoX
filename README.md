# Quasimidi TechnoX Lab

An Intel-Mac-oriented JUCE project targeting Audio Unit, VST3 and Standalone builds.

## Current implementation

- [x] User-supplied 1200 × 112 SVG front-panel artwork imported.
- [x] JUCE 7.0.12 CMake project created.
- [x] AU, VST3 and Standalone plugin formats declared.
- [x] Initial editor renders the SVG, overlays a dynamic LCD and responds to basic front-panel clicks.
- [x] MIDI input is accepted by the plugin shell.
- [ ] Verified hardware preset/parameter mapping and SysEx support.
- [ ] Original TechnoX sound engine, sample playback, filters, modulation and effects.
- [ ] Intel macOS build and AU validation in Logic Pro X.

## Important status note

This is an **interactive interface/plugin scaffold**, not yet a sound emulation. The current audio processor intentionally outputs silence. The LCD preset/part values are UI placeholders and are not yet connected to verified factory patch data. No firmware or ROM images are included.

Firmware dumps alone do not establish the original PCM ROM contents or reproduce the hardware DSP. Do not bundle copyrighted firmware or ROM data without authorization.

## Build on Intel macOS

Install Xcode Command Line Tools and CMake, then run:

```bash
cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

JUCE 7.0.12 is fetched by CMake. The project declares AU, VST3 and Standalone formats. Build and host validation are still pending.

## Project map

- `assets/technox_front_panel.svg` — supplied front-panel illustration.
- `Source/PluginProcessor.*` — JUCE processor shell.
- `Source/PluginEditor.*` — SVG panel and basic LCD/control interaction.
- `PROJECT_PLAN.md` — engineering roadmap.
