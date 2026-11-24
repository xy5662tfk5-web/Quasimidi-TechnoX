# TechnoX front-panel control routing

Status: implementation checkpoint, 2026-11-24.

This note separates the controls that are already connected to the project state from the original-firmware paths that are still unverified. It is deliberately not a claim that the original sound engine has been reconstructed.

## 1. Two different kinds of preset selection

The interface exposes two independent levels of selection:

1. **Performance memory** — the processor has 50 factory Performance programs. JUCE host program selection calls `setCurrentProgram()`, selects one Performance record from the parsed factory bank, then copies its Common block and four stored Part blocks into the M.A.S.S. engine state.
2. **Single sound on a Part** — each of the four stored Performance Parts is Parts 13–16. Its Bank Number and Sound Number are the Part parameters at offsets `0x00` and `0x01`. The manual specifies Bank 0–3 and Sound 0–127; together these select one of 512 single sounds. The editor's PARAMETER encoder chooses which Part parameter is being edited; VALUE changes that parameter while EDIT mode is active.

The LCD now reports the Performance slot, the selected Part/parameter, and the Part's Bank/Sound number plus catalog name. This distinguishes changing a whole Performance from changing the sound assigned to one Part.

## 2. Encoder interaction

- PARAMETER drag owns only the selected-parameter index.
- VALUE drag owns only the selected Part parameter value, and is active only in EDIT mode.
- Each encoder has a separate drag-start value.
- The VALUE indicator is held steady while PARAMETER is being dragged. After the PARAMETER gesture ends, VALUE snaps to the stored value of the newly selected parameter. During VALUE editing, only the VALUE indicator moves.
- The VALUE indicator is refreshed from canonical Part state when a host Performance/program change, external MIDI edit, or Part selection changes the underlying value.

Previously the VALUE pointer was derived directly from the raw value of whichever parameter happened to be selected on every paint. As the PARAMETER encoder selected a different parameter, the VALUE pointer immediately jumped to that parameter's raw value. That looked like both knobs were being turned together, even though only one drag target was active. The independent display state removes that visual coupling without falsifying the value shown in the LCD.

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

## 4. What is not yet connected to the original firmware

The processor currently sends **host/external MIDI bytes** into the emulated MC6850-compatible ACIA path and clocks the IC4 v1.05 8032 firmware. The editor's PARAMETER/VALUE mouse events do not yet enter a decoded firmware front-panel input/encoder scan path.

The IC4 evidence currently establishes:
- MIDI bytes reach the firmware through the ACIA path.
- The Performance loader copies the selected record's name, Common bytes and stored Parts 13–16 into firmware-visible state.
- Part Bank/Sound data can be resolved through the IC51 resource catalog.
- The firmware reaches separate, still-unidentified external interfaces: the page-selected mailbox path and the fixed four-register write window at `0x8000–0x8003`.

It does **not** yet establish:
- the physical panel encoder/button scan registers or their exact event format;
- an authentic firmware command corresponding to a mouse drag of PARAMETER or VALUE;
- the physical chip behind the external register window;
- the final voice-start/sample-playback protocol or original DSP/sample decoding.

For that reason, do not invent 8032 input-register writes or claim that the UI has already been routed through the original panel scanner. That connection requires a firmware trace or hardware capture for each physical encoder/button action.

## 5. Original firmware panel-input path discovered

Reachable IC4 v1.05 code contains actual external-pin scan routines. The evidence is documented in `docs/research/FRONT_PANEL_GPIO_SCAN.md`:

- `0xA6FF` performs a four-pattern P1/P3.0 key-matrix scan and samples P1.3/P1.7.
- `0xA762` performs a two-channel quadrature-style scan: P1.5 is the phase/strobe, P1.6 is sampled for direction, and P3.0 selects the two external input rows.
- Exact FIFO codes are `BE/7E` for the P3.0-low channel and `BD/7D` for the P3.0-high channel.
- Both scan routines enqueue bytes through `0xA6D1`; `0xA6E7` pops the 16-byte internal-RAM event FIFO.
- The timer interrupt calls `0xA762`; several main-state routines call `0xA6FF`.

The emulator has a P3.0-selected P1 input fixture seam, and the regression test executes the actual IC4 ROM at `0xA6A8` and `0xA762` to assert both channels, both direction codes, and no repeat while the phase remains held low. The test checks the ROM's own instruction path and FIFO output.

This is the first firmware-confirmed two-channel encoder scanner. The remaining mapping is from these firmware channels to the physical front-panel labels PARAMETER and VALUE, and the full downstream control semantics of each event. Do not assign those labels yet: the source proves two selected quadrature-style input channels and their byte pairs, but not which panel legend belongs to which row.

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
