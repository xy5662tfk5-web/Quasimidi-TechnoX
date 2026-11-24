#include "PluginEditor.h"
#include "BinaryData.h"
#include "TechnoXPartModel.h"
#include <cmath>

namespace {
constexpr float kParameterX = 568.0f;
constexpr float kValueX = 674.0f;
constexpr float kVolumeX = 964.0f;
constexpr float kKnobY = 68.0f;
constexpr int kPartFirstParameter = 0;
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
    startTimerHz(12);
}

void QuasimidiTechnoXAudioProcessorEditor::timerCallback() { repaint(); }

void QuasimidiTechnoXAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xffc6cbd1));
    if (frontPanel != nullptr)
        frontPanel->drawWithin(g, getLocalBounds().toFloat(),
                               juce::RectanglePlacement::stretchToFit, 1.0f);

    const auto* descriptor = technox::PartEditModel::descriptor(
        static_cast<std::size_t>(selectedParameter));
    const int raw = audioProcessor.getPartParameterRaw(
        audioProcessor.getCurrentProgram(), selectedPart,
        static_cast<technox::PartParameter>(selectedParameter));
    const int shown = raw < 0 || descriptor == nullptr ? 0 : raw + descriptor->displayOffset;
    const auto line1 = juce::String("P") + juce::String(selectedPart + 13) +
        (editMode ? " EDIT " : " ") + (descriptor != nullptr ? descriptor->name : "Parameter");
    const auto line2 = raw < 0 ? juce::String("No part data") :
        juce::String(shown) + "  " + audioProcessor.getCurrentPartSoundName(selectedPart);

    // The new SVG bezel is 260x60 at (224,38). Keep an even 8 px inset
    // on all four sides so the active LCD follows the same rounded contour.
    // Preserve the existing Courier New bold 13 pt display typography.
    const juce::Rectangle<float> lcdBounds(232.0f, 46.0f, 244.0f, 44.0f);
    g.setColour(juce::Colour(0xff75cc56));
    g.fillRoundedRectangle(lcdBounds, 20.0f);
    g.setColour(juce::Colour(0xff203b16));
    g.drawRoundedRectangle(lcdBounds, 20.0f, 1.0f);
    g.setColour(juce::Colour(0xff142605));
    g.setFont(juce::Font("Courier New", 13.0f, juce::Font::bold));
    g.drawText(line1.substring(0, 22), 243, 50, 220, 16, juce::Justification::centredLeft);
    g.drawText(line2.substring(0, 22), 243, 68, 220, 16, juce::Justification::centredLeft);

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
    drawPointer(kParameterX, static_cast<float>(selectedParameter) / 23.0f, 21.0f);
    drawPointer(kValueX, descriptor != nullptr && descriptor->maximum > 0
        ? static_cast<float>(juce::jmax(0, raw)) / static_cast<float>(descriptor->maximum) : 0.0f, 21.0f);
    drawPointer(kVolumeX, audioProcessor.getOutputGain(), 14.0f);

    g.setColour(editMode ? juce::Colour(0xff1c7a27) : juce::Colour(0xff25282b));
    g.setFont(juce::Font(8.5f, juce::Font::bold));
    g.drawText(editMode ? "EDIT" : "PLAY", 740, 56, 32, 10, juce::Justification::centred);
    g.drawText(juce::String(static_cast<int>(std::lround(audioProcessor.getOutputGain() * 100.0f))) + "%",
               940, 91, 48, 11, juce::Justification::centred);
}

void QuasimidiTechnoXAudioProcessorEditor::selectNextParameter(int delta)
{
    selectedParameter = (selectedParameter + delta + 24) % 24;
    repaint();
}

void QuasimidiTechnoXAudioProcessorEditor::changeSelectedValue(int delta)
{
    const auto parameter = static_cast<technox::PartParameter>(selectedParameter);
    const auto* descriptor = technox::PartEditModel::descriptor(static_cast<std::size_t>(selectedParameter));
    if (descriptor == nullptr) return;
    const int current = audioProcessor.getPartParameterRaw(audioProcessor.getCurrentProgram(),
                                                            selectedPart, parameter);
    if (current < 0) return;
    audioProcessor.setPartParameterRaw(audioProcessor.getCurrentProgram(), selectedPart, parameter,
        juce::jlimit(0, static_cast<int>(descriptor->maximum), current + delta));
    repaint();
}

void QuasimidiTechnoXAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    // A new press always owns exactly one control. Never carry a previous drag
    // target into the next gesture.
    dragTarget = DragTarget::none;
    // JUCE events can originate in a child/component coordinate space; always
    // convert to this editor's local coordinates before hit-testing.
    const auto p = e.getEventRelativeTo(this).position;
    const auto insideKnob = [&p](float cx, float cy, float radius) {
        return p.getDistanceFrom(juce::Point<float>(cx, cy)) <= radius;
    };

    // Use the visible circular knob faces rather than broad rectangular zones.
    // The three hit regions are mutually exclusive in the TechnoX panel layout.
    if (insideKnob(kVolumeX, kKnobY, 21.0f)) {
        dragTarget = DragTarget::volume;
        dragStartY = p.y;
        dragStartValue = audioProcessor.getOutputGain();
        return;
    }
    if (insideKnob(kParameterX, kKnobY, 29.0f)) {
        dragTarget = DragTarget::parameter;
        dragStartY = p.y;
        dragStartValue = static_cast<float>(selectedParameter);
        return;
    }
    if (insideKnob(kValueX, kKnobY, 29.0f)) {
        dragTarget = DragTarget::value;
        dragStartY = p.y;
        const int v = audioProcessor.getPartParameterRaw(audioProcessor.getCurrentProgram(),
            selectedPart, static_cast<technox::PartParameter>(selectedParameter));
        dragStartValue = static_cast<float>(juce::jmax(0, v));
        return;
    }

    if (p.x >= 742 && p.x <= 771 && p.y >= 38 && p.y <= 58) {
        editMode = !editMode; // EDIT enters edit mode; OK confirms the current raw value.
        repaint();
        return;
    }
    if (p.x >= 780 && p.x <= 809 && p.y >= 38 && p.y <= 58) {
        editMode = false; // EXIT leaves edit mode without altering stored performance data.
        repaint();
        return;
    }
    if (p.y >= 82 && p.y <= 101 && p.x >= 742 && p.x <= 809) {
        selectedPart = juce::jlimit(0, 3, selectedPart + (p.x < 776 ? -1 : 1));
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
    dragTarget = DragTarget::none;
}

void QuasimidiTechnoXAudioProcessorEditor::updateFromDrag(float y)
{
    const float delta = (dragStartY - y) / 2.5f;
    if (dragTarget == DragTarget::volume) {
        audioProcessor.setOutputGain(dragStartValue + delta / 100.0f);
    } else if (dragTarget == DragTarget::parameter) {
        selectedParameter = juce::jlimit(0, 23, static_cast<int>(std::lround(dragStartValue + delta)));
    } else if (dragTarget == DragTarget::value && editMode) {
        const auto* descriptor = technox::PartEditModel::descriptor(static_cast<std::size_t>(selectedParameter));
        if (descriptor != nullptr) {
            const int next = juce::jlimit(0, static_cast<int>(descriptor->maximum),
                static_cast<int>(std::lround(dragStartValue + delta)));
            audioProcessor.setPartParameterRaw(audioProcessor.getCurrentProgram(), selectedPart,
                static_cast<technox::PartParameter>(selectedParameter), next);
        }
    }
    repaint();
}
