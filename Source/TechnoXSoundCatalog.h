#pragma once

#include <JuceHeader.h>
#include "TechnoXPartModel.h"
#include <array>
#include <cstddef>
#include <utility>

namespace technox {

// Loads the 4 x 128 single-sound names extracted from Technox.ins.
// This is a name catalog only; it contains no waveform or synthesis data.
class SoundCatalog {
public:
    static constexpr std::size_t kBankCount = 4;
    static constexpr std::size_t kProgramsPerBank = 128;

    bool loadFromJson(const void* jsonData, int jsonSize) {
        if (jsonData == nullptr || jsonSize <= 0)
            return false;

        const auto root = juce::JSON::parse(
            juce::String::fromUTF8(static_cast<const char*>(jsonData), jsonSize));
        if (!root.isObject())
            return false;

        const auto* rootObject = root.getDynamicObject();
        if (rootObject == nullptr)
            return false;

        const auto banksValue = rootObject->getProperty(juce::Identifier("program_banks"));
        const auto* banksObject = banksValue.getDynamicObject();
        if (banksObject == nullptr)
            return false;

        constexpr const char* bankKeys[kBankCount] = {"A", "B", "C", "D"};
        std::array<std::array<juce::String, kProgramsPerBank>, kBankCount> parsedNames{};

        for (std::size_t bank = 0; bank < kBankCount; ++bank) {
            const auto bankValue = banksObject->getProperty(juce::Identifier(bankKeys[bank]));
            const auto* entries = bankValue.getArray();
            if (entries == nullptr || entries->size() != static_cast<int>(kProgramsPerBank))
                return false;

            for (const auto& entry : *entries) {
                const auto* entryObject = entry.getDynamicObject();
                if (entryObject == nullptr)
                    return false;

                const int program = entryObject->getProperty(juce::Identifier("program")).toString().getIntValue();
                const auto name = entryObject->getProperty(juce::Identifier("name")).toString();
                if (program < 0 || program >= static_cast<int>(kProgramsPerBank) || name.isEmpty())
                    return false;

                parsedNames[bank][static_cast<std::size_t>(program)] = name;
            }

            for (const auto& name : parsedNames[bank])
                if (name.isEmpty())
                    return false;
        }

        names_ = std::move(parsedNames);
        loaded_ = true;
        return true;
    }

    bool isLoaded() const noexcept { return loaded_; }

    juce::String nameFor(int bankNumber, int soundNumber) const {
        if (!loaded_ || bankNumber < 0 || bankNumber >= static_cast<int>(kBankCount) ||
            soundNumber < 0 || soundNumber >= static_cast<int>(kProgramsPerBank))
            return {};
        return names_[static_cast<std::size_t>(bankNumber)]
                     [static_cast<std::size_t>(soundNumber)];
    }

    juce::String nameFor(const PartEditModel& part) const {
        return nameFor(part.bankNumber(), part.soundNumber());
    }

private:
    std::array<std::array<juce::String, kProgramsPerBank>, kBankCount> names_{};
    bool loaded_ = false;
};

} // namespace technox
