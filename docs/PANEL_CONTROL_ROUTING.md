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

The manual states that both Alpha dials can select one of the 50 Performance memories in Performance mode; PART/BANK switches ROM and RAM. In Sequencer Multi mode the PARAMETER dial chooses the sound group, VALUE chooses the individual sound, and PART/BANK chooses the Part. Pressing EDIT opens the Edit Function menu; the Alpha dials navigate it, OK enters the highlighted function, and EXIT returns to the main page. In the Edit pages PARAMETER chooses the field and VALUE changes it.

The plugin contains the 50 factory ROM Performance records. It does not yet contain a user RAM Performance dump or RAM-memory persistence, so selecting RAM currently displays `RAM-xx: EMPTY` and does not pretend that user Performances are available.

## 2. Implemented panel pages

- **Main / Performance:** both dials select a factory ROM Performance; the inactive dial indicator stays still during a drag and follows the same selected Performance when the gesture ends. PART/BANK left selects ROM and right selects RAM.
- **Main / Sequencer Multi:** PARAMETER selects Bank/Sound Group A–D, VALUE selects the individual sound 000–127, and PART/BANK moves independently through all 16 Sequencer Multi Parts. Part 10 is treated as the dedicated drum channel and VALUE selects one of its eight Drum Sets.
- **Edit Function:** EDIT opens the menu, both dials navigate the highlighted function, OK confirms, EXIT leaves the menu. `Edit Part` and `Edit System` are implemented; other entries display `MENU NOT BUILT` rather than invoking placeholder behavior.
- **Edit Part:** PARAMETER selects one of the documented 24 Part fields; VALUE edits its documented raw range. The edit is queued into the audio-thread mailbox.
- **Edit System:** the first implemented item is Mode (Perform / Sequencer Multi). VALUE switches the plugin's canonical MIDI routing mode. This currently updates the plugin's state model; the exact original IC4 System-Edit SysEx/write transaction is not yet connected.



## 3. Independent Performance and 16-Part Sequencer state

The project now keeps two separate models:

- **Performance memory:** 50 factory ROM Performance records. Each contains a Common block and four stored Part blocks, corresponding to user-visible Parts 13–16 (runtime voice slots 12–15).
- **Sequencer Multi:** 16 independent live Part parameter sets, separate from the four stored Performance blocks. The panel has a separate Part selector for these Parts 1–16; selecting a Multi Part never changes the Performance Part 13–16 selector.

The 16 Sequencer Multi Parts default to MIDI channels 1–16. A unique channel map is validated when a V5/V6 plugin state is restored. Part 10 (index 9) has a **separate Drum Set field** (manual temporary address `02 7E 00`), with values 0–7. It is not stored in Part 10's ordinary Sound Number field. MIDI Program Change on channel 10 and the panel VALUE control address this dedicated Drum Set state.

The 4-bank × 128-sound catalog contains names, not waveforms. The 512 names are used for single-sound lookup on Parts 1–9 and 11–16. The Drum Set name/selection uses its own field and is distinct from the single-sound catalog.

## 4. What a panel edit reaches today

The current panel edit path depends on the active mode:

```
Performance Edit Part:
  VALUE encoder
    -> Performance index + Performance Part 13–16 + PartParameter
    -> queuePanelPartParameterRaw()
    -> atomic Performance value + 50 × 4 × 24 mailbox
    -> processBlock() drains the mailbox
    -> MASSAudioEngine Part 13–16

Sequencer Multi single-sound Part 1–9 / 11–16:
  VALUE encoder
    -> Sequencer Part 1–16 + PartParameter
    -> queueSequencerPartParameterRaw()
    -> atomic live Part value + 16 × 24 mailbox
    -> processBlock() drains the mailbox
    -> MASSAudioEngine Part 1–16

Sequencer Multi Part 10 Drum Set:
  VALUE encoder or MIDI Program Change on channel 10
    -> dedicated Drum Set value 0–7 (manual temporary address 02 7E 00)
    -> atomic drum-set state + pending audio-thread update
    -> processBlock() calls MASSAudioEngine::setDrumSet(Part 10, kit)
```

Both mailboxes coalesce repeated drag updates to the latest raw value for each Part parameter. This avoids a growing queue while audio is stopped and keeps the editor/message thread from touching VoiceManager/backend state. The LCD reads the canonical atomic value immediately; the engine consumes that value on its next audio callback. Switching modes defers the relevant 4-Part Performance or 16-Part Sequencer model synchronization to the audio thread.

Plugin state format V6 stores all 16 Sequencer-Multi Part parameter sets, 16 MIDI channel assignments, and the separate Drum Set value. V5 is migrated by moving its legacy Part 10 Sound Number value into the dedicated Drum Set field; V1–V4 states are also accepted and their four legacy MIDI-channel fields are consumed using the old chunk length and migrated only when they form a valid Part 13–16 channel mapping.

Host Performance selection is similarly split: `setCurrentProgram()` changes the selected Performance index and sets a pending flag; the full Performance/Part image is synchronized to MASSAudioEngine at the start of the next `processBlock()`. This moves VoiceManager/backend mutations out of the host/message thread.

Bank/Sound values are the bank/program identity used by the engine. `VoiceSlot` captures that identity for newly started voices, and the predecoded IC51 descriptor/command catalog is available to the backend.

The dedicated Drum Set selection is now represented in the engine and exposed through a separate optional `IMASSVoiceBackend::setDrumSet()` hook. The current `BasicMASSVoiceBackend` does not yet synthesize the original drum kits; this is a state/contract connection only, not recovered drum audio.

This is a real connection to the current engine state; the editor is not just moving a decorative knob. However, the active `BasicMASSVoiceBackend` remains a generic test oscillator. It is not the original M.A.S.S. synthesis implementation.

## 5. Current boundary between panel state, firmware and audio engine

The processor sends **host/external MIDI bytes** into the emulated MC6850-compatible ACIA path and clocks the IC4 v1.05 8032 firmware. Panel edits currently update the canonical Performance/Part model and are applied to MASSAudioEngine through a bounded mailbox drained by `processBlock()`. This prevents the GUI thread from mutating VoiceManager/backend state concurrently with audio rendering.

The panel's encoder events are not yet injected into the original IC4 GPIO scanner in the live plugin. A real-ROM regression test does, however, execute the actual `0xA6A8` FIFO initializer and `0xA762` encoder scan routine using P3.0-selected external P1 inputs, then checks the exact FIFO codes.

The current active `BasicMASSVoiceBackend` remains a generic test oscillator, not the original M.A.S.S. synthesis implementation. The firmware's physical sound peripheral, final voice/sample command protocol, and PCM/sample decoding remain unresolved.

## 6. Original firmware panel-input path and logical role correlation

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

## 7. Required long-term architecture

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

## 8. Important distinctions for later implementation

- **Performance index** is the selected one of 50 factory Performance memories.
- **Part number** is user-facing Parts 13–16 for the four stored Performance blocks; runtime firmware event IDs/record indices use zero-based values 12–15.
- **Bank/Sound** selects one of 512 single sounds assigned to a Part.
- **MIDI Program Change** is not the same as selecting one of the 50 Performance memories. The current processor applies Program Change to the Part Sound Number in Sequencer Multi/Part Channels mode; in Performance Omni mode the current MIDI handler does not apply Program Change to a Part. Keep this behavior explicit until the manual's mode-specific Program Change behavior and the firmware path are implemented and regression-tested.

## 9. Evidence boundary

Manual-confirmed ranges, firmware-derived record layout, and the current project implementation are distinct evidence layers. The control UI should follow the manual's documented parameter ranges, the firmware's verified Part/Performance mapping, and only verified hardware input/command semantics. No panel control should be wired to guessed DSP registers.
