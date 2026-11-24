# TechnoX front-panel control routing

Status: implementation checkpoint, 2026-11-24.

This note separates the controls that are already connected to the project state from the original-firmware paths that are still unverified. It is deliberately not a claim that the original sound engine has been reconstructed.

## 1. Manual-confirmed operating modes and preset selection

The English TechnoX Owner's Manual, pages 6–9, defines two operating modes and the panel behavior in each mode. The plugin currently maps its existing routing states to those two UI modes:

| Plugin/UI state | Original mode | PARAMETER/SOUNDGROUP | VALUE/SOUND | PART/BANK |
|---|---|---|---|---|
| `performanceOmni` | Performance | Selects a Performance | Selects a Performance | Switches ROM/RAM Performance bank |
| `partChannels` | Sequencer Multi | Selects a sound group | Selects a single sound | Selects a Part |
| EDIT page | Part edit | Selects a Part parameter | Changes that parameter | Selects a Part |

The manual states that both Alpha dials can select one of the 50 Performance memories in Performance mode; PART/BANK switches ROM and RAM. In Sequencer Multi mode the PARAMETER dial chooses the sound group, VALUE chooses the individual sound, and PART/BANK chooses the Part. In Edit mode PARAMETER navigates the edit parameters/menus and VALUE alters the selected parameter.

The plugin contains the 50 factory ROM Performance records. It does not yet contain a user RAM Performance dump or RAM-memory persistence, so selecting RAM currently displays `RAM-xx: EMPTY` and does not pretend that user Performances are available.

## 2. Current Sequencer Multi Part coverage

The manual defines 16 Sequencer Multi Parts, but the current factory Performance parser stores four Part blocks per Performance: Parts 13–16 (runtime indices 12–15). The present editor therefore lets the user select the four stored Performance Parts 13–16 only. It must not display them as if all 16 Sequencer Multi Parts were already modelled. Full Parts 1–16 and saved Sequencer Multi setups remain a follow-up implementation.

The sound-name catalog contains 4 banks × 128 names. In the current four-Part editor, PARAMETER writes the selected Part's Bank Number (0–3), and VALUE writes its Sound Number (0–127). The selected sound name is resolved through the catalog. The catalog contains names and IC51 resource metadata, not decoded waveforms.

## 3. What a panel edit reaches today

The current path for a panel VALUE edit is:

```
VALUE encoder (EDIT mode)
  -> selected Part + selected PartParameter
  -> queuePanelPartParameterRaw()
  -> canonical atomic Part value + bounded per-Performance mailbox
  -> next processBlock() drains the mailbox
  -> MASSAudioEngine::setPartParameter() on the audio thread
  -> active IMASSVoiceBackend
```

The mailbox coalesces repeated drag updates to the latest raw value per Part parameter. This avoids a growing queue while audio is stopped and keeps the editor/message thread from touching VoiceManager/backend state. The LCD reads the canonical atomic Part value immediately; the engine consumes the same value on its next audio callback.

Host Performance selection is similarly split: `setCurrentProgram()` changes the selected Performance index and sets a pending flag; the full Performance/Part image is synchronized to MASSAudioEngine at the start of the next `processBlock()`. This moves VoiceManager/backend mutations out of the host/message thread.

Bank/Sound values are the bank/program identity used by the engine. `VoiceSlot` captures that identity for newly started voices, and the predecoded IC51 descriptor/command catalog is available to the backend.

This is a real connection to the current engine state; the editor is not just moving a decorative knob. However, the active `BasicMASSVoiceBackend` remains a generic test oscillator. It is not the original M.A.S.S. synthesis implementation.

## 4. Current boundary between panel state, firmware and audio engine

The processor sends **host/external MIDI bytes** into the emulated MC6850-compatible ACIA path and clocks the IC4 v1.05 8032 firmware. Panel edits currently update the canonical Performance/Part model and are applied to MASSAudioEngine through a bounded mailbox drained by `processBlock()`. This prevents the GUI thread from mutating VoiceManager/backend state concurrently with audio rendering.

The panel's encoder events are not yet injected into the original IC4 GPIO scanner in the live plugin. A real-ROM regression test does, however, execute the actual `0xA6A8` FIFO initializer and `0xA762` encoder scan routine using P3.0-selected external P1 inputs, then checks the exact FIFO codes.

The current active `BasicMASSVoiceBackend` remains a generic test oscillator, not the original M.A.S.S. synthesis implementation. The firmware's physical sound peripheral, final voice/sample command protocol, and PCM/sample decoding remain unresolved.

## 5. Original firmware panel-input path and logical role correlation

Reachable IC4 v1.05 code contains actual external-pin scan routines. The evidence is documented in `docs/research/FRONT_PANEL_GPIO_SCAN.md`:

- `0xA6FF` performs a four-pattern P1/P3.0 key-matrix scan and samples P1.3/P1.7.
- `0xA762` performs a two-channel quadrature-style scan: P1.5 is the phase/strobe, P1.6 is sampled for direction, and P3.0 selects the two external input rows.
- Exact FIFO codes are `BE/7E` for the P3.0-low channel and `BD/7D` for the P3.0-high channel.
- Both scan routines enqueue bytes through `0xA6D1`; `0xA6E7` pops the 16-byte internal-RAM event FIFO.
- The timer interrupt calls `0xA762`; several main-state routines call `0xA6FF`.

The ROM consumer evidence strengthens the logical role assignment:
- `BE/7E` enters `0x2D6D`, which reads the current selected index at `0x307A`, obtains the current value from `0x3063 + index`, converts it through `0xEC30`, and sends a value update through the page-`0x33` mailbox at offset `0xD8`. This is the VALUE/SOUND function.
- `BD/7D` falls through to `0x29AA`, which updates the current selection/display-related state using `0x307A`, the current Part data, and page-`0x33` mailbox fields. This is the PARAMETER/SOUNDGROUP navigation candidate.

The real-ROM regression executes `0xA6A8` and `0xA762` and verifies both channels, both direction codes, and no repeat while the phase remains held low. This proves the firmware's scan outputs and their logical consumer roles. The exact physical PCB traces from P1.5/P1.6/P3.0 to the two front-panel encoder bodies still require a board-level capture.

## 6. Required long-term architecture

Keep one canonical Performance/Part state and one typed control-event boundary:

```
Panel input
  -> PanelControlEvent (control ID, direction/value, gesture begin/end)
  -> TechnoX control router
  -> validated PerformanceState / PartState
       |-> verified IC4 firmware input adapter (only after its protocol is decoded)
       |-> MASSAudioEngine state / verified synthesis backend
  -> LCD feedback reads the same canonical state
```

The firmware adapter and audio backend must not become two independent sources of truth. A confirmed firmware response should update the canonical model; the backend should consume that same model or the verified firmware-produced resource/command stream. Unknown bytes and SysEx fields must continue to be preserved.

## 7. Important distinctions for later implementation

- **Performance index** is the selected one of 50 factory Performance memories.
- **Part number** is user-facing Parts 13–16 for the four stored Performance blocks; runtime firmware event IDs/record indices use zero-based values 12–15.
- **Bank/Sound** selects one of 512 single sounds assigned to a Part.
- **MIDI Program Change** is not the same as selecting one of the 50 Performance memories. The current processor applies Program Change to the Part Sound Number in Sequencer Multi/Part Channels mode; in Performance Omni mode the current MIDI handler does not apply Program Change to a Part. Keep this behavior explicit until the manual's mode-specific Program Change behavior and the firmware path are implemented and regression-tested.

## 8. Evidence boundary

Manual-confirmed ranges, firmware-derived record layout, and the current project implementation are distinct evidence layers. The control UI should follow the manual's documented parameter ranges, the firmware's verified Part/Performance mapping, and only verified hardware input/command semantics. No panel control should be wired to guessed DSP registers.
