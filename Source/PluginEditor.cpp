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

    performanceLabel.setText("PERFORMANCE", juce::dontSendNotification);
    performanceLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    performanceLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd8dde3));
    addAndMakeVisible(performanceLabel);
    for (int i = 0; i < audioProcessor.getNumPrograms(); ++i)
        performanceSelector.addItem(audioProcessor.getProgramName(i), i + 1);
    performanceSelector.setSelectedId(audioProcessor.getCurrentProgram() + 1, juce::dontSendNotification);
    performanceSelector.onChange = [this] {
        audioProcessor.setCurrentProgram(performanceSelector.getSelectedId() - 1);
        refreshPartControls();
        repaint();
    };
    performanceSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1d252b));
    performanceSelector.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe9f0f5));
    performanceSelector.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff76838b));
    addAndMakeVisible(performanceSelector);

    partLabel.setText("EDIT PART", juce::dontSendNotification);
    partLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd8dde3));
    addAndMakeVisible(partLabel);
    for (int part = 0; part < 4; ++part)
        partSelector.addItem("Part " + juce::String(13 + part), part + 1);
    partSelector.setSelectedId(1, juce::dontSendNotification);
    partSelector.onChange = [this] {
        selectedPartSlot = juce::jlimit(0, 3, partSelector.getSelectedId() - 1);
        refreshPartControls();
        repaint();
    };
    addAndMakeVisible(partSelector);

    midiChannelLabel.setText("MIDI CHANNEL", juce::dontSendNotification);
    midiChannelLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd8dde3));
    addAndMakeVisible(midiChannelLabel);
    for (int channel = 1; channel <= 16; ++channel)
        midiChannelSelector.addItem(juce::String(channel), channel);
    midiChannelSelector.onChange = [this] {
        const int channel = midiChannelSelector.getSelectedId();
        if (!audioProcessor.setPartMidiChannel(selectedPartSlot, channel))
            midiChannelSelector.setSelectedId(audioProcessor.getPartMidiChannel(selectedPartSlot),
                                              juce::dontSendNotification);
        repaint();
    };
    addAndMakeVisible(midiChannelSelector);

    for (std::size_t i = 0; i < parameterSliders.size(); ++i) {
        const auto* descriptor = technox::PartEditModel::descriptor(i);
        parameterLabels[i].setText(descriptor != nullptr ? descriptor->name : "Parameter",
                                   juce::dontSendNotification);
        parameterLabels[i].setColour(juce::Label::textColourId, juce::Colour(0xffdce3e8));
        parameterLabels[i].setFont(juce::Font(12.0f));
        addAndMakeVisible(parameterLabels[i]);

        auto& slider = parameterSliders[i];
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 20);
        if (descriptor != nullptr)
            slider.setRange(descriptor->displayOffset,
                            descriptor->displayOffset + descriptor->maximum, 1.0);
        slider.setColour(juce::Slider::trackColourId, juce::Colour(0xff8dbb38));
        slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffdce3e8));
        slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xfff1f4f6));
        slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff20282e));
        slider.onValueChange = [this, i] {
            if (refreshingControls) return;
            const auto* d = technox::PartEditModel::descriptor(i);
            if (d == nullptr) return;
            const int raw = static_cast<int>(std::lround(parameterSliders[i].getValue())) - d->displayOffset;
            audioProcessor.setPartParameterRaw(audioProcessor.getCurrentProgram(), selectedPartSlot,
                static_cast<technox::PartParameter>(i), raw);
            repaint();
        };
        addAndMakeVisible(slider);
    }

    setSize(1200, 600);
    setResizable(false, false);
    performanceLabel.setBounds(510, 12, 110, 20);
    performanceSelector.setBounds(620, 10, 300, 26);
    partLabel.setBounds(24, 124, 90, 22);
    partSelector.setBounds(112, 122, 120, 26);
    midiChannelLabel.setBounds(260, 124, 112, 22);
    midiChannelSelector.setBounds(374, 122, 64, 26);

    for (std::size_t i = 0; i < parameterSliders.size(); ++i) {
        const int column = static_cast<int>(i / 6);
        const int row = static_cast<int>(i % 6);
        const int x = 20 + column * 295;
        const int y = 178 + row * 64;
        parameterLabels[i].setBounds(x, y + 2, 112, 24);
        parameterSliders[i].setBounds(x + 112, y, 170, 28);
    }

    lastDisplayedPerformance = audioProcessor.getCurrentProgram();
    refreshPartControls();
    startTimerHz(15);
}

void QuasimidiTechnoXAudioProcessorEditor::refreshPartControls()
{
    const int performance = audioProcessor.getCurrentProgram();
    refreshingControls = true;
    midiChannelSelector.setSelectedId(audioProcessor.getPartMidiChannel(selectedPartSlot),
                                      juce::dontSendNotification);
    for (std::size_t i = 0; i < parameterSliders.size(); ++i) {
        const auto* descriptor = technox::PartEditModel::descriptor(i);
        const int raw = audioProcessor.getPartParameterRaw(
            performance, selectedPartSlot, static_cast<technox::PartParameter>(i));
        if (descriptor != nullptr && raw >= 0)
            parameterSliders[i].setValue(raw + descriptor->displayOffset, juce::dontSendNotification);
    }
    refreshingControls = false;
}

void QuasimidiTechnoXAudioProcessorEditor::timerCallback()
{
    const int currentPerformance = audioProcessor.getCurrentProgram();
    const int selectedId = currentPerformance + 1;
    if (performanceSelector.getSelectedId() != selectedId)
        performanceSelector.setSelectedId(selectedId, juce::dontSendNotification);
    if (currentPerformance != lastDisplayedPerformance)
        lastDisplayedPerformance = currentPerformance;
    refreshPartControls();
    repaint();
}

void QuasimidiTechnoXAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff252d33));
    if (frontPanel != nullptr)
        frontPanel->drawWithin(g, juce::Rectangle<float>(0.0f, 0.0f,
            static_cast<float>(getWidth()), 112.0f),
            juce::RectanglePlacement::stretchToFit, 1.0f);

    g.setColour(juce::Colour(0xff9be034));
    g.fillRect(231, 43, 156, 32);
    g.setColour(juce::Colour(0xff142603));
    g.setFont(juce::Font("Courier New", 11.5f, juce::Font::bold));
    const auto name = audioProcessor.getCurrentPerformanceName();
    g.drawText(juce::String(audioProcessor.getCurrentProgram() + 1).paddedLeft('0', 2)
                   + " " + name, 237, 43, 145, 14, juce::Justification::left, false);
    g.drawText("Part:" + juce::String(13 + selectedPartSlot)
                   + "  Ch:" + juce::String(audioProcessor.getPartMidiChannel(selectedPartSlot)),
               237, 56, 145, 15, juce::Justification::left, false);
    g.setFont(juce::Font("Courier New", 8.5f, juce::Font::bold));
    const auto soundName = audioProcessor.getCurrentPartSoundName(selectedPartSlot);
    g.drawText(!soundName.isEmpty() ? soundName : juce::String("Sound catalog unavailable"),
               237, 68, 145, 8, juce::Justification::left, false);

    g.setColour(juce::Colour(0xffd8dde3));
    g.setFont(juce::Font(14.0f, juce::Font::bold));
    g.drawText("PART PARAMETER EDITOR", 20, 153, 320, 20, juce::Justification::left, false);
    g.setFont(juce::Font(10.0f));
    g.drawText("MIDI CC / NRPN input follows the assigned MIDI channel.",
               350, 153, 820, 20, juce::Justification::right, false);
}

void QuasimidiTechnoXAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    const auto p = event.getPosition();
    if (p.x >= 400 && p.x <= 455 && p.y >= 43 && p.y <= 74) {
        if (p.y < 57 && audioProcessor.getNumPrograms() > 0) {
            const int next = juce::jlimit(0, audioProcessor.getNumPrograms() - 1,
                audioProcessor.getCurrentProgram() + (p.x < 428 ? -1 : 1));
            audioProcessor.setCurrentProgram(next);
            performanceSelector.setSelectedId(next + 1, juce::dontSendNotification);
            lastDisplayedPerformance = next;
            refreshPartControls();
        } else if (p.y >= 57) {
            selectedPartSlot = juce::jlimit(0, 3, selectedPartSlot + (p.x < 428 ? -1 : 1));
            partSelector.setSelectedId(selectedPartSlot + 1, juce::dontSendNotification);
            refreshPartControls();
        }
    }
    repaint();
}
