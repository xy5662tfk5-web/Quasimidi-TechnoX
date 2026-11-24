#include "PluginEditor.h"
#include "BinaryData.h"
#include "TechnoXPartModel.h"
#include "TechnoXPanelControlMap.h"
#include <cmath>

namespace {
constexpr float kParameterX = 568.0f;
constexpr float kValueX = 674.0f;
constexpr float kVolumeX = 964.0f;
constexpr float kKnobY = 68.0f;
constexpr int kPartFirstParameter = 0;

void drawTrackedText(juce::Graphics& g, const juce::String& text,
                     float centreX, float baselineY, float fontHeight,
                     float letterSpacing, float scaleX = 1.0f, float scaleY = 1.0f)
{
    // The original SVG uses Arial Black with explicit 8/7.5/6.8 px sizing
    // and tracking. JUCE's SVG parser ignores letter-spacing and SVG font
    // shorthand, so place the same glyphs with the original metrics here.
    const juce::Font font(juce::FontOptions{}.withName("Arial Black")
                                             .withPointHeight(fontHeight));
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0.0f, 0.0f);

    const int count = glyphs.getNumGlyphs();
    if (count == 0)
        return;

    const float textWidth = juce::GlyphArrangement::getStringWidth(font, text)
        + letterSpacing * static_cast<float>(juce::jmax(0, count - 1));
    const float left = centreX - textWidth * scaleX * 0.5f;

    g.setColour(juce::Colour(0xff181a1c));
    for (int i = 0; i < count; ++i)
    {
        const auto& glyph = glyphs.getGlyph(i);
        const float x = left
            + (glyph.getLeft() + letterSpacing * static_cast<float>(i)) * scaleX;
        const auto transform = juce::AffineTransform::translation(
                -glyph.getLeft(), -glyph.getBaselineY())
            .scaled(scaleX, scaleY)
            .translated(x, baselineY);
        glyph.draw(g, transform);
    }
}

void drawOriginalCurvedLabel(juce::Graphics& g, const juce::String& text,
                             juce::Point<float> centre, float radius,
                             bool lowerArc)
{
    // Match the source SVG exactly: Arial Black, 8 px, 1.6 px tracking.
    // JUCE 8.0.13's SVG parser does not implement <textPath> or letter-spacing,
    // so render the same glyph run along the original SVG arc here.
    constexpr float fontHeight = 8.0f;
    constexpr float letterSpacing = 1.6f;
    const juce::Font font(juce::FontOptions{}.withName("Arial Black").withPointHeight(fontHeight));

    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0.0f, 0.0f);

    const int count = glyphs.getNumGlyphs();
    if (count == 0)
        return;

    const float textWidth = juce::GlyphArrangement::getStringWidth(font, text)
        + letterSpacing * static_cast<float>(juce::jmax(0, count - 1));
    const float pathLength = juce::MathConstants<float>::pi * radius;
    const float firstGlyphOffset = (pathLength - textWidth) * 0.5f;

    g.setColour(juce::Colour(0xff181a1c));
    for (int i = 0; i < count; ++i)
    {
        const auto& glyph = glyphs.getGlyph(i);
        const float glyphWidth = glyph.getRight() - glyph.getLeft();
        const float distance = firstGlyphOffset + glyph.getLeft()
            + glyphWidth * 0.5f + letterSpacing * static_cast<float>(i);
        const float theta = lowerArc
            ? distance / radius
            : juce::MathConstants<float>::pi + distance / radius;

        const juce::Point<float> position(
            centre.x + radius * std::cos(theta),
            centre.y + radius * std::sin(theta));
        const float tangent = std::atan2(radius * std::cos(theta),
                                         -radius * std::sin(theta));

        const auto glyphTransform = juce::AffineTransform::translation(
                -glyph.getLeft(), -glyph.getBaselineY())
            .rotated(tangent)
            .translated(position.x, position.y);
        glyph.draw(g, glyphTransform);
    }
}
}

QuasimidiTechnoXAudioProcessorEditor::QuasimidiTechnoXAudioProcessorEditor(
    QuasimidiTechnoXAudioProcessor& processor)
    : AudioProcessorEditor(&processor), audioProcessor(processor)
{
    auto svg = juce::XmlDocument::parse(
        juce::String::fromUTF8(BinaryData::technox_front_panel_svg,
                               static_cast<int>(BinaryData::technox_front_panel_svgSize)));
    if (svg != nullptr)
        frontPanel = juce::Drawable::createFromSVG(*svg);
    setSize(1200, 132);
    setResizable(false, false);
    lastRoutingMode = audioProcessor.getMidiRoutingMode();
    syncDialsFromCurrentSelection();
    startTimerHz(12);
}

void QuasimidiTechnoXAudioProcessorEditor::timerCallback()
{
    const auto currentMode = audioProcessor.getMidiRoutingMode();
    if (currentMode != lastRoutingMode && dragTarget == DragTarget::none) {
        lastRoutingMode = currentMode;
        syncDialsFromCurrentSelection();
    }
    repaint();
}

void QuasimidiTechnoXAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xffc6cbd1));
    if (frontPanel != nullptr)
        frontPanel->drawWithin(g, getLocalBounds().toFloat(),
                               juce::RectanglePlacement::stretchToFit, 1.0f);

    // Static panel lettering follows the original SVG's exact coordinates,
    // font sizes and tracking. Drawn here because JUCE 8.0.13 doesn't apply
    // SVG text shorthand/letter-spacing consistently.
    drawTrackedText(g, "MADE IN GERMANY", 56.0f, 81.0f, 6.8f, 0.4f);
    drawTrackedText(g, "POWER", 138.0f, 34.0f, 7.5f, 0.5f);
    drawTrackedText(g, "16 PART MULTITIMBRAL SYNTHESIZER",
                    354.0f, 115.0f, 8.0f, 1.0f, 1.12f, 0.85f);
    drawTrackedText(g, "PARAMETER", 568.0f, 30.0f, 7.5f, 0.5f);
    drawTrackedText(g, "SOUNDGROUP", 568.0f, 114.0f, 7.5f, 0.5f);
    drawTrackedText(g, "VALUE", 674.0f, 30.0f, 7.5f, 0.5f);
    drawTrackedText(g, "SOUND", 674.0f, 114.0f, 7.5f, 0.5f);
    drawTrackedText(g, "EDIT / OK", 756.0f, 34.0f, 6.8f, 0.4f);
    drawTrackedText(g, "EXIT", 794.0f, 34.0f, 6.8f, 0.4f);
    drawTrackedText(g, "PART / BANK", 775.0f, 112.0f, 6.8f, 0.4f);
    drawOriginalCurvedLabel(g, "TECHNO - RAVE", { 876.0f, 62.0f }, 40.0f, false);
    drawOriginalCurvedLabel(g, "ELECTRONIC", { 876.0f, 74.0f }, 40.0f, true);
    drawTrackedText(g, "VOLUME", 964.0f, 21.0f, 7.5f, 0.5f);
    drawTrackedText(g, "HEADPH.", 1031.0f, 21.0f, 7.5f, 0.5f);

    const auto mode = panelOperatingMode();
    const auto* descriptor = technox::PartEditModel::descriptor(
        static_cast<std::size_t>(selectedParameter));
    const int performanceIndex = audioProcessor.getCurrentProgram();
    const int bank = audioProcessor.getPartParameterRaw(
        performanceIndex, selectedPart, technox::PartParameter::bankNumber);
    const int sound = audioProcessor.getPartParameterRaw(
        performanceIndex, selectedPart, technox::PartParameter::soundNumber);
    const juce::String soundName = audioProcessor.getCurrentPartSoundName(selectedPart);

    juce::String line1;
    juce::String line2;
    if (page == EditorPage::editPart) {
        const int raw = audioProcessor.getPartParameterRaw(
            performanceIndex, selectedPart,
            static_cast<technox::PartParameter>(selectedParameter));
        const int shown = raw < 0 || descriptor == nullptr
            ? 0 : raw + descriptor->displayOffset;
        line1 = "PART " + juce::String(selectedPart + 13) + " "
            + (descriptor != nullptr ? juce::String(descriptor->name) : juce::String("PARAM"));
        line2 = juce::String(shown) + " " + soundName;
    } else if (page == EditorPage::editFunctionMenu) {
        static const char* const menuNames[] = {
            "Edit Common", "Edit Part", "Edit Drums", "Edit FX1", "Edit FX2",
            "Edit Arpegg.", "Edit System", "Write/Dump", "Play Demo"
        };
        line1 = "EDIT/FUNCTION";
        line2 = menuNotImplemented ? juce::String("MENU NOT BUILT")
            : juce::String("<") + juce::String(selectedEditFunction) + "> "
                + menuNames[juce::jlimit(1, 9, selectedEditFunction) - 1];
    } else if (page == EditorPage::editSystem) {
        line1 = "EDIT SYSTEM";
        line2 = audioProcessor.getMidiRoutingMode() == MidiRoutingMode::performanceOmni
            ? juce::String("MODE: PERFORM")
            : juce::String("MODE: SEQ MULTI");
    } else if (mode == technox::PanelOperatingMode::performance) {
        line1 = "TECHNOX";
        const int slot = selectedPerformanceForDial() + 1;
        if (ramPerformanceBank)
            line2 = "RAM-" + juce::String(slot).paddedLeft('0', 2) + ": EMPTY";
        else
            line2 = "ROM-" + juce::String(performanceIndex + 1).paddedLeft('0', 2)
                + ": " + audioProcessor.getCurrentPerformanceName();
    } else {
        line1 = "SEQUENCER MULTI";
        const juce::String bankLetter = bank >= 0 && bank < 4
            ? juce::String::charToString(static_cast<juce::juce_wchar>('A' + bank))
            : juce::String("?");
        line2 = juce::String(selectedPart + 13) + ":" + bankLetter
            + juce::String(sound < 0 ? 0 : sound).paddedLeft('0', 3)
            + " " + soundName;
    }

    // The hardware display is 2 x 16 characters. Keep the original fixed-width
    // character-cell presentation instead of squeezing extra UI text into it.
    const juce::Rectangle<float> lcdBounds(236.0f, 44.0f, 236.0f, 48.0f);
    g.setColour(juce::Colour(0xff75cc56));
    g.fillRoundedRectangle(lcdBounds, 20.0f);
    g.setColour(juce::Colour(0xff203b16));
    g.drawRoundedRectangle(lcdBounds, 20.0f, 1.0f);
    g.setColour(juce::Colour(0xff142605));
    g.setFont(juce::Font("Courier New", 13.0f, juce::Font::bold));
    g.drawText(line1.substring(0, 16), 248, 50, 212, 18, juce::Justification::centredLeft);
    g.drawText(line2.substring(0, 16), 248, 68, 212, 18, juce::Justification::centredLeft);

    const auto drawPointer = [&g](float x, float value, float radius) {
        const float angle = juce::degreesToRadians(-135.0f + juce::jlimit(0.0f, 1.0f, value) * 270.0f);
        const juce::Point<float> c(x, kKnobY);
        const juce::Point<float> tip(c.x + std::sin(angle) * radius,
                                     c.y - std::cos(angle) * radius);
        // Dark underlay separates the indicator from the knob's highlights;
        // the wider white stroke remains clearly visible at every angle.
        g.setColour(juce::Colour(0xff08090a));
        g.drawLine(c.x, c.y, tip.x, tip.y, 5.5f);
        g.setColour(juce::Colours::white);
        g.drawLine(c.x, c.y, tip.x, tip.y, 3.5f);
        g.fillEllipse(tip.x - 2.0f, tip.y - 2.0f, 4.0f, 4.0f);
    };
    drawPointer(kParameterX, parameterDialNormalized, 21.0f);
    drawPointer(kValueX, valueDialNormalized, 21.0f);
    drawPointer(kVolumeX, audioProcessor.getOutputGain(), 14.0f);

}

void QuasimidiTechnoXAudioProcessorEditor::selectNextParameter(int delta)
{
    selectedParameter = juce::jlimit(0, 23, selectedParameter + delta);
    parameterDialNormalized = static_cast<float>(selectedParameter) / 23.0f;
    syncValueDialFromSelection();
    repaint();
}

void QuasimidiTechnoXAudioProcessorEditor::changeSelectedValue(int delta)
{
    const auto parameter = static_cast<technox::PartParameter>(selectedParameter);
    const auto* descriptor = technox::PartEditModel::descriptor(
        static_cast<std::size_t>(selectedParameter));
    if (descriptor == nullptr)
        return;

    const int current = audioProcessor.getPartParameterRaw(
        audioProcessor.getCurrentProgram(), selectedPart, parameter);
    if (current < 0)
        return;

    const int next = juce::jlimit(0, static_cast<int>(descriptor->maximum),
                                  current + delta);
    if (audioProcessor.queuePanelPartParameterRaw(
            audioProcessor.getCurrentProgram(), selectedPart, parameter, next))
        syncValueDialFromSelection();
    repaint();
}

technox::PanelOperatingMode
QuasimidiTechnoXAudioProcessorEditor::panelOperatingMode() const noexcept
{
    return audioProcessor.getMidiRoutingMode() == MidiRoutingMode::performanceOmni
        ? technox::PanelOperatingMode::performance
        : technox::PanelOperatingMode::sequencerMulti;
}

technox::PanelEditPage
QuasimidiTechnoXAudioProcessorEditor::panelEditPage() const noexcept
{
    switch (page) {
        case EditorPage::editFunctionMenu: return technox::PanelEditPage::editFunctionMenu;
        case EditorPage::editPart: return technox::PanelEditPage::editPart;
        case EditorPage::editSystem: return technox::PanelEditPage::editSystem;
        case EditorPage::main: default: return technox::PanelEditPage::main;
    }
}

int QuasimidiTechnoXAudioProcessorEditor::selectedPerformanceForDial() const noexcept
{
    return ramPerformanceBank
        ? juce::jlimit(0, juce::jmax(0, audioProcessor.getNumPrograms() - 1),
                       ramPerformanceSelection)
        : audioProcessor.getCurrentProgram();
}

void QuasimidiTechnoXAudioProcessorEditor::syncParameterDialFromSelection() noexcept
{
    const int maximumProgram = juce::jmax(1, audioProcessor.getNumPrograms() - 1);
    if (page == EditorPage::editPart) {
        parameterDialNormalized = static_cast<float>(selectedParameter) / 23.0f;
    } else if (page == EditorPage::editFunctionMenu) {
        parameterDialNormalized = static_cast<float>(selectedEditFunction - 1) / 8.0f;
    } else if (page == EditorPage::editSystem) {
        parameterDialNormalized = static_cast<float>(selectedSystemParameter);
    } else if (panelOperatingMode() == technox::PanelOperatingMode::performance) {
        parameterDialNormalized = static_cast<float>(selectedPerformanceForDial())
            / static_cast<float>(maximumProgram);
    } else {
        const int bank = audioProcessor.getPartParameterRaw(
            audioProcessor.getCurrentProgram(), selectedPart,
            technox::PartParameter::bankNumber);
        parameterDialNormalized = bank < 0 ? 0.0f : static_cast<float>(bank) / 3.0f;
    }
}

void QuasimidiTechnoXAudioProcessorEditor::syncValueDialFromSelection() noexcept
{
    if (page == EditorPage::editPart) {
        const auto* descriptor = technox::PartEditModel::descriptor(
            static_cast<std::size_t>(selectedParameter));
        if (descriptor == nullptr || descriptor->maximum == 0) {
            valueDialNormalized = 0.0f;
            return;
        }
        const int raw = audioProcessor.getPartParameterRaw(
            audioProcessor.getCurrentProgram(), selectedPart,
            static_cast<technox::PartParameter>(selectedParameter));
        valueDialNormalized = raw < 0 ? 0.0f
            : static_cast<float>(juce::jlimit(0, static_cast<int>(descriptor->maximum), raw))
                / static_cast<float>(descriptor->maximum);
    } else if (page == EditorPage::editFunctionMenu) {
        valueDialNormalized = static_cast<float>(selectedEditFunction - 1) / 8.0f;
    } else if (page == EditorPage::editSystem) {
        valueDialNormalized = audioProcessor.getMidiRoutingMode() == MidiRoutingMode::performanceOmni
            ? 0.0f : 1.0f;
    } else if (panelOperatingMode() == technox::PanelOperatingMode::performance) {
        const int maximumProgram = juce::jmax(1, audioProcessor.getNumPrograms() - 1);
        valueDialNormalized = static_cast<float>(selectedPerformanceForDial())
            / static_cast<float>(maximumProgram);
    } else {
        const int sound = audioProcessor.getPartParameterRaw(
            audioProcessor.getCurrentProgram(), selectedPart,
            technox::PartParameter::soundNumber);
        valueDialNormalized = sound < 0 ? 0.0f : static_cast<float>(sound) / 127.0f;
    }
}

void QuasimidiTechnoXAudioProcessorEditor::syncDialsFromCurrentSelection() noexcept
{
    syncParameterDialFromSelection();
    syncValueDialFromSelection();
}

void QuasimidiTechnoXAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    dragTarget = DragTarget::none;
    const auto p = e.getEventRelativeTo(this).position;
    const auto insideKnob = [&p](float cx, float cy, float radius) {
        return p.getDistanceFrom(juce::Point<float>(cx, cy)) <= radius;
    };

    const auto mode = panelOperatingMode();
    const auto page = editMode
        ? technox::PanelEditPage::editPart : technox::PanelEditPage::main;

    if (insideKnob(kVolumeX, kKnobY, 21.0f)) {
        dragTarget = DragTarget::volume;
        dragStartY = p.y;
        volumeDragStartValue = audioProcessor.getOutputGain();
        return;
    }

    if (insideKnob(kParameterX, kKnobY, 29.0f)) {
        const auto action = technox::panelAction(
            mode, page, technox::PanelAlphaDial::parameterSoundGroup);
        dragTarget = DragTarget::parameter;
        dragStartY = p.y;
        switch (action) {
            case technox::PanelControlAction::selectPerformance:
                parameterDragStartIndex = static_cast<float>(selectedPerformanceForDial());
                break;
            case technox::PanelControlAction::selectSoundGroup:
                parameterDragStartIndex = static_cast<float>(juce::jmax(0,
                    audioProcessor.getPartParameterRaw(audioProcessor.getCurrentProgram(),
                        selectedPart, technox::PartParameter::bankNumber)));
                break;
            case technox::PanelControlAction::selectEditParameter:
                parameterDragStartIndex = static_cast<float>(selectedParameter);
                break;
            default:
                dragTarget = DragTarget::none;
                break;
        }
        return;
    }

    if (insideKnob(kValueX, kKnobY, 29.0f)) {
        const auto action = technox::panelAction(
            mode, page, technox::PanelAlphaDial::valueSound);
        dragTarget = DragTarget::value;
        dragStartY = p.y;
        switch (action) {
            case technox::PanelControlAction::selectPerformance:
                valueDragStartRaw = static_cast<float>(selectedPerformanceForDial());
                break;
            case technox::PanelControlAction::selectSingleSound:
                valueDragStartRaw = static_cast<float>(juce::jmax(0,
                    audioProcessor.getPartParameterRaw(audioProcessor.getCurrentProgram(),
                        selectedPart, technox::PartParameter::soundNumber)));
                break;
            case technox::PanelControlAction::editSelectedParameter:
                valueDragStartRaw = static_cast<float>(juce::jmax(0,
                    audioProcessor.getPartParameterRaw(audioProcessor.getCurrentProgram(),
                        selectedPart,
                        static_cast<technox::PartParameter>(selectedParameter))));
                break;
            default:
                dragTarget = DragTarget::none;
                break;
        }
        return;
    }

    if (p.x >= 742 && p.x <= 771 && p.y >= 38 && p.y <= 58) {
        editMode = !editMode;
        syncDialsFromCurrentSelection();
        repaint();
        return;
    }

    if (p.x >= 780 && p.x <= 809 && p.y >= 38 && p.y <= 58) {
        editMode = false;
        syncDialsFromCurrentSelection();
        repaint();
        return;
    }

    if (p.y >= 82 && p.y <= 101 && p.x >= 742 && p.x <= 809) {
        const auto key = p.x < 776
            ? technox::PanelKey::partBankPrevious
            : technox::PanelKey::partBankNext;
        const auto action = technox::panelAction(mode, page, key);
        if (action == technox::PanelControlAction::switchPerformanceBank) {
            // ROM has the 50 verified factory Performances. A RAM Performance
            // image is not embedded yet, so expose the bank honestly as empty.
            ramPerformanceBank = p.x >= 776.0f;
            ramPerformanceSelection = audioProcessor.getCurrentProgram();
            syncDialsFromCurrentSelection();
        } else if (action == technox::PanelControlAction::selectPart) {
            // The current parser has four stored Performance Part blocks:
            // user-visible Parts 13–16. Full Sequencer Multi Parts 1–16 follow later.
            selectedPart = juce::jlimit(0, 3, selectedPart + (p.x < 776 ? -1 : 1));
            syncDialsFromCurrentSelection();
        }
        repaint();
    }
}

void QuasimidiTechnoXAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (dragTarget == DragTarget::none || !e.mods.isLeftButtonDown())
        return;
    updateFromDrag(e.getEventRelativeTo(this).position.y);
}

void QuasimidiTechnoXAudioProcessorEditor::mouseUp(const juce::MouseEvent&)
{
    const auto completedTarget = dragTarget;
    dragTarget = DragTarget::none;

    // In Edit mode, changing the parameter selects a new stored value for the
    // VALUE encoder. In the two main modes the dials remain independent.
    if (completedTarget == DragTarget::parameter && editMode)
        syncValueDialFromSelection();
    else if (completedTarget == DragTarget::value)
        syncValueDialFromSelection();
}

void QuasimidiTechnoXAudioProcessorEditor::updateFromDrag(float y)
{
    const float delta = (dragStartY - y) / 2.5f;
    if (dragTarget == DragTarget::volume) {
        audioProcessor.setOutputGain(volumeDragStartValue + delta / 100.0f);
        repaint();
        return;
    }

    const auto mode = panelOperatingMode();
    const auto page = editMode
        ? technox::PanelEditPage::editPart : technox::PanelEditPage::main;
    const int maximumProgram = juce::jmax(0, audioProcessor.getNumPrograms() - 1);

    if (dragTarget == DragTarget::parameter) {
        const auto action = technox::panelAction(
            mode, page, technox::PanelAlphaDial::parameterSoundGroup);
        const int next = static_cast<int>(std::lround(parameterDragStartIndex + delta));
        switch (action) {
            case technox::PanelControlAction::selectPerformance: {
                const int selection = juce::jlimit(0, maximumProgram, next);
                if (ramPerformanceBank)
                    ramPerformanceSelection = selection;
                else
                    audioProcessor.setCurrentProgram(selection);
                parameterDialNormalized = maximumProgram > 0
                    ? static_cast<float>(selection) / static_cast<float>(maximumProgram) : 0.0f;
                break;
            }
            case technox::PanelControlAction::selectSoundGroup: {
                const int bank = juce::jlimit(0, 3, next);
                if (audioProcessor.queuePanelPartParameterRaw(
                        audioProcessor.getCurrentProgram(), selectedPart,
                        technox::PartParameter::bankNumber, bank))
                    parameterDialNormalized = static_cast<float>(bank) / 3.0f;
                break;
            }
            case technox::PanelControlAction::selectEditParameter:
                selectedParameter = juce::jlimit(0, 23, next);
                parameterDialNormalized = static_cast<float>(selectedParameter) / 23.0f;
                break;
            default:
                break;
        }
    } else if (dragTarget == DragTarget::value) {
        const auto action = technox::panelAction(
            mode, page, technox::PanelAlphaDial::valueSound);
        const int next = static_cast<int>(std::lround(valueDragStartRaw + delta));
        switch (action) {
            case technox::PanelControlAction::selectPerformance: {
                const int selection = juce::jlimit(0, maximumProgram, next);
                if (ramPerformanceBank)
                    ramPerformanceSelection = selection;
                else
                    audioProcessor.setCurrentProgram(selection);
                valueDialNormalized = maximumProgram > 0
                    ? static_cast<float>(selection) / static_cast<float>(maximumProgram) : 0.0f;
                break;
            }
            case technox::PanelControlAction::selectSingleSound: {
                const int sound = juce::jlimit(0, 127, next);
                if (audioProcessor.queuePanelPartParameterRaw(
                        audioProcessor.getCurrentProgram(), selectedPart,
                        technox::PartParameter::soundNumber, sound))
                    valueDialNormalized = static_cast<float>(sound) / 127.0f;
                break;
            }
            case technox::PanelControlAction::editSelectedParameter: {
                const auto* descriptor = technox::PartEditModel::descriptor(
                    static_cast<std::size_t>(selectedParameter));
                if (descriptor != nullptr) {
                    const int value = juce::jlimit(0, static_cast<int>(descriptor->maximum), next);
                    if (audioProcessor.queuePanelPartParameterRaw(
                            audioProcessor.getCurrentProgram(), selectedPart,
                            static_cast<technox::PartParameter>(selectedParameter), value)) {
                        valueDialNormalized = descriptor->maximum > 0
                            ? static_cast<float>(value) / static_cast<float>(descriptor->maximum)
                            : 0.0f;
                    }
                }
                break;
            }
            default:
                break;
        }
    }
    repaint();
}
