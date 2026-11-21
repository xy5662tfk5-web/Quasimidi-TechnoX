# Quasimidi TechnoX Lab

An Intel-Mac-oriented AU/VST3/Standalone research project for a Quasimidi TechnoX-inspired instrument interface and, ultimately, a documented software emulation.

## Current status

- [x] User-supplied 1200 × 112 SVG front-panel concept imported.
- [x] SVG namespace corrected to the standard W3C namespace.
- [ ] JUCE project and AU/VST3/Standalone targets.
- [ ] Interactive controls and LCD state.
- [ ] SysEx import/export and patch-bank handling.
- [ ] Sound ROM research and lawful acquisition/verification.
- [ ] Audio engine, voice allocation, filters, modulation and effects.
- [ ] Comparison against hardware reference recordings.

**Important:** this initial scaffold is an interface asset and project plan, not an audio emulation. Firmware dumps alone do not establish the original PCM ROM contents or reproduce the hardware DSP. Do not bundle copyrighted firmware or ROM data without authorization.

## Build target

Planned formats: Audio Unit, VST3 and Standalone. Initial platform target: Intel macOS. JUCE will be integrated after the UI and source layout are agreed.

## Asset

`assets/technox_front_panel.svg` — supplied front-panel artwork, with only the malformed SVG namespace corrected. The artwork is a project mock-up, not verified as a pixel-accurate factory panel.
