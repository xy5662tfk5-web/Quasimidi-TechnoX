#include "PluginEditor.h"
#include "BinaryData.h"
#include <cmath>

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

void QuasimidiTechnoXAudioProcessorEditor::timerCallback()
{
    repaint();
}

void QuasimidiTechnoXAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xffc6cbd1));
    if (frontPanel != nullptr)
        frontPanel->drawWithin(g, getLocalBounds().toFloat(),
                               juce::RectanglePlacement::stretchToFit, 1.0f);

    // Dynamic LCD content replaces the artwork's placeholder text.
    g.setColour(juce::Colour(0xff8dcc43));
    g.fillRoundedRectangle(266.0f, 47.0f, 176.0f, 42.0f, 2.0f);
    g.setColour(juce::Colour(0xff142605));
    g.setFont(juce::Font("Courier New", 17.0f, juce::Font::bold));
    g.drawText("TECHNOX", 274, 48, 160, 17, juce::Justification::centredLeft, false);
    g.setFont(juce::Font("Courier New", 13.0f, juce::Font::bold));
    const auto programLine = juce::String(audioProcessor.getCurrentProgram() + 1)
        .paddedLeft('0', 3) + " " + audioProcessor.getCurrentPerformanceName();
    g.drawText(programLine.substring(0, 21), 274, 68, 160, 17,
               juce::Justification::centredLeft, false);

    // Draw the volume pointer from the actual processor gain, not a static SVG mark.
    const float gain = audioProcessor.getOutputGain();
    const float angle = juce::degreesToRadians(-135.0f + gain * 270.0f);
    const juce::Point<float> centre(1012.0f, 68.0f);
    const juce::Point<float> tip(centre.x + std::sin(angle) * 13.0f,
                                 centre.y - std::cos(angle) * 13.0f);
    g.setColour(juce::Colour(0xff202225));
    g.fillEllipse(994.0f, 50.0f, 36.0f, 36.0f);
    g.setColour(juce::Colour(0xffe7eaed));
    g.drawLine(centre.x, centre.y, tip.x, tip.y, 2.6f);
    g.setColour(juce::Colour(0xff25282b));
    g.fillEllipse(centre.x - 2.2f, centre.y - 2.2f, 4.4f, 4.4f);

    g.setColour(juce::Colour(0xff181a1c));
    g.setFont(juce::Font(8.5f, juce::Font::bold));
    g.drawText(juce::String(static_cast<int>(std::lround(gain * 100.0f))) + "%",
               988, 91, 48, 11, juce::Justification::centred, false);
}

void QuasimidiTechnoXAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    const auto p = event.position;
    if (p.x >= 987.0f && p.x <= 1037.0f && p.y >= 43.0f && p.y <= 94.0f) {
        draggingVolume = true;
        volumeAtDragStart = audioProcessor.getOutputGain();
        dragStartY = p.y;
        audioProcessor.setOutputGain(juce::jlimit(0.0f, 1.0f,
            1.0f - (p.y - 43.0f) / 51.0f));
        repaint();
        return;
    }

    // The two small PART/BANK keys remain usable for quick navigation.
    if (p.y >= 80.0f && p.y <= 104.0f && p.x >= 730.0f && p.x <= 820.0f) {
        const int direction = p.x < 776.0f ? -1 : 1;
        const int next = juce::jlimit(0, audioProcessor.getNumPrograms() - 1,
                                      audioProcessor.getCurrentProgram() + direction);
        audioProcessor.setCurrentProgram(next);
        repaint();
    }
}

void QuasimidiTechnoXAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (draggingVolume)
        updateVolumeFromDrag(event.position.y);
}

void QuasimidiTechnoXAudioProcessorEditor::mouseUp(const juce::MouseEvent&)
{
    draggingVolume = false;
}

void QuasimidiTechnoXAudioProcessorEditor::updateVolumeFromDrag(float y)
{
    const float delta = (y - dragStartY) / 100.0f;
    audioProcessor.setOutputGain(volumeAtDragStart - delta);
    repaint();
}
