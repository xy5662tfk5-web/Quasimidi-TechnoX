#include "PluginEditor.h"
#include "BinaryData.h"

QuasimidiTechnoXAudioProcessorEditor::QuasimidiTechnoXAudioProcessorEditor(
    QuasimidiTechnoXAudioProcessor& processor)
    : AudioProcessorEditor(&processor), audioProcessor(processor)
{
    auto svg = juce::XmlDocument::parse(
        juce::String::fromUTF8(BinaryData::technox_front_panel_svg,
                               (int) BinaryData::technox_front_panel_svgSize));
    if (svg != nullptr)
        frontPanel = juce::Drawable::createFromSVG(*svg);

    performanceLabel.setText("PERFORMANCE", juce::dontSendNotification);
    performanceLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    performanceLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd8dde3));
    addAndMakeVisible(performanceLabel);

    for (int i = 0; i < audioProcessor.getNumPrograms(); ++i)
        performanceSelector.addItem(audioProcessor.getProgramName(i), i + 1);
    performanceSelector.setSelectedId(audioProcessor.getCurrentProgram() + 1,
                                       juce::dontSendNotification);
    performanceSelector.onChange = [this]
    {
        audioProcessor.setCurrentProgram(performanceSelector.getSelectedId() - 1);
        repaint();
    };
    performanceSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1d252b));
    performanceSelector.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe9f0f5));
    performanceSelector.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff76838b));
    addAndMakeVisible(performanceSelector);

    setSize(1200, 112);
    setResizable(false, false);
    performanceLabel.setBounds(510, 12, 110, 20);
    performanceSelector.setBounds(620, 10, 300, 26);
    startTimerHz(15);
}

void QuasimidiTechnoXAudioProcessorEditor::timerCallback()
{
    // Hosts may change the program without interacting with this editor.
    // Poll the processor's thread-safe selection and mirror it without
    // triggering the ComboBox's onChange callback.
    const int selectedId = audioProcessor.getCurrentProgram() + 1;
    if (performanceSelector.getSelectedId() != selectedId)
        performanceSelector.setSelectedId(selectedId, juce::dontSendNotification);
    repaint();
}

void QuasimidiTechnoXAudioProcessorEditor::paint(juce::Graphics& g)
{
    if (frontPanel != nullptr)
        frontPanel->drawWithin(g, getLocalBounds().toFloat(),
                               juce::RectanglePlacement::stretchToFit, 1.0f);
    else
        g.fillAll(juce::Colour(0xffc7cbd1));

    g.setColour(juce::Colour(0xff9be034));
    g.fillRect(231, 43, 156, 32);
    g.setColour(juce::Colour(0xff142603));
    g.setFont(juce::Font("Courier New", 11.5f, juce::Font::bold));
    const auto name = audioProcessor.getCurrentPerformanceName();
    g.drawText(juce::String(audioProcessor.getCurrentProgram() + 1).paddedLeft('0', 2)
                   + " " + name,
               237, 43, 145, 14, juce::Justification::left, false);
    g.drawText("Part:" + juce::String(partNumber).paddedLeft('0', 2)
                   + "  Ch:" + juce::String(midiChannel).paddedLeft('0', 2),
               237, 56, 145, 15, juce::Justification::left, false);

    g.setFont(juce::Font("Courier New", 8.5f, juce::Font::bold));
    const auto soundName = audioProcessor.getCurrentPartSoundName(partNumber - 13);
    g.drawText(soundName.isNotEmpty() ? soundName : juce::String("Sound catalog unavailable"),
               237, 68, 145, 8, juce::Justification::left, false);

    g.setColour(juce::Colour(0xffe5e7ea));
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawText("P" + juce::String(parameterNumber) + "  VAL " + juce::String(valueNumber),
               232, 88, 150, 14, juce::Justification::left, false);
}

void QuasimidiTechnoXAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    const auto p = event.getPosition();
    if (p.x >= 400 && p.x <= 455 && p.y >= 43 && p.y <= 74)
    {
        if (p.y < 57 && audioProcessor.getNumPrograms() > 0)
        {
            const auto next = juce::jlimit(0, audioProcessor.getNumPrograms() - 1,
                                           audioProcessor.getCurrentProgram() + (p.x < 428 ? -1 : 1));
            audioProcessor.setCurrentProgram(next);
            performanceSelector.setSelectedId(next + 1, juce::sendNotification);
        }
        else if (p.y >= 57)
            partNumber = juce::jlimit(13, 16, partNumber + (p.x < 428 ? -1 : 1));
    }
    else if (p.x >= 218 && p.x <= 260 && p.y >= 35 && p.y <= 78)
        parameterNumber = (parameterNumber % 128) + 1;
    else if (p.x >= 290 && p.x <= 332 && p.y >= 35 && p.y <= 78)
        valueNumber = juce::jlimit(0, 127, valueNumber + (event.mods.isShiftDown() ? -1 : 1));
    else if (p.x >= 975 && p.x <= 1015 && p.y >= 35 && p.y <= 78)
        volumeNumber = juce::jlimit(0, 127, volumeNumber + (event.mods.isShiftDown() ? -1 : 1));
    repaint();
}
