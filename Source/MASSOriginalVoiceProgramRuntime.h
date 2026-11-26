#pragma once

#include "MASSResourceROM.h"
#include "MASSFirmwareResourceCommandPlanner.h"
#include "MASSFirmwarePitchParameterExecution.h"
#include "MASSVoiceManager.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace technox {

// Real-time-safe binding between an allocated host voice and the immutable
// IC51 program streams decoded at startup. This is the first original-resource
// execution boundary: it binds the exact selected bank/program and retains
// both transient firmware-flag stream variants without guessing which
// call-site context is active for a particular command.
//
// It intentionally does not decode waveform samples or claim that the
// reconstructed C000-FFFF image is PCM. The actual playback/DSP layer remains
// a separate implementation stage.
class MASSOriginalVoiceProgramRuntime {
public:
    static constexpr std::size_t kVoiceCount = VoiceManager::kVoiceCount;
    static constexpr std::size_t kFirmwareVariantCount = 2;

    struct VoiceProgramBinding {
        bool active = false;
        bool releasing = false;
        std::uint8_t part = 0;
        std::uint8_t channel = 0;
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
        std::uint8_t bankNumber = 0;
        std::uint8_t soundNumber = 0;
        std::uint8_t drumSetNumber = 0xff;
        std::array<const MASSResolvedProgramResource*, kFirmwareVariantCount> variants{};
        std::uint16_t descriptorAddress = 0;
        std::uint16_t streamPointer = 0;
        std::uint8_t slotsConsumed = 0;
        std::uint8_t outputSlotsWritten = 0;
        std::uint8_t fillerSlotsWritten = 0;
        std::size_t commandCount = 0;
        std::array<std::size_t, kFirmwareVariantCount> commandCursors{};
    };

    void setProgramCatalog(const MASSResourceProgramCatalog* catalog) noexcept {
        programCatalog_ = catalog;
    }

    void setExternalSoundMemoryImage(
        const MASSIC51ExternalSoundMemoryImage* image) noexcept {
        externalSoundMemoryImage_ = image;
    }

    const MASSResourceProgramCatalog* programCatalog() const noexcept {
        return programCatalog_;
    }

    const MASSIC51ExternalSoundMemoryImage* externalSoundMemoryImage() const noexcept {
        return externalSoundMemoryImage_;
    }

    void reset() noexcept {
        voices_.fill(VoiceProgramBinding{});
    }

    void resetVoice(int voiceIndex) noexcept {
        if (validVoice(voiceIndex))
            voices_[static_cast<std::size_t>(voiceIndex)] = VoiceProgramBinding{};
    }

    // Synchronize from the authoritative VoiceSlot. This also handles mono
    // legato retargeting, which changes VoiceSlot note/program fields without
    // necessarily issuing a fresh backend noteOn callback.
    bool synchronizeVoice(int voiceIndex, const VoiceSlot& voice) noexcept {
        if (!validVoice(voiceIndex))
            return false;
        if (!voice.active) {
            resetVoice(voiceIndex);
            return false;
        }

        auto& binding = voices_[static_cast<std::size_t>(voiceIndex)];
        const bool resourceIdentityChanged =
            !binding.active ||
            binding.part != voice.part ||
            binding.bankNumber != voice.bankNumber ||
            binding.soundNumber != voice.soundNumber;
        if (resourceIdentityChanged)
            bind(voiceIndex, voice);
        else {
            binding.channel = voice.channel;
            binding.note = voice.note;
            binding.velocity = voice.velocity;
            binding.releasing = voice.releasing;
            binding.drumSetNumber = voice.drumSetNumber;
        }
        return binding.active;
    }

    bool noteOn(int voiceIndex, const VoiceSlot& voice) noexcept {
        if (!validVoice(voiceIndex) || !voice.active)
            return false;
        bind(voiceIndex, voice);
        return voices_[static_cast<std::size_t>(voiceIndex)].active;
    }

    void noteOff(int voiceIndex) noexcept {
        if (!validVoice(voiceIndex))
            return;
        auto& binding = voices_[static_cast<std::size_t>(voiceIndex)];
        if (binding.active)
            binding.releasing = true;
    }

    const VoiceProgramBinding* bindingForVoice(int voiceIndex) const noexcept {
        return validVoice(voiceIndex) ? &voices_[static_cast<std::size_t>(voiceIndex)] : nullptr;
    }

    const MASSResolvedProgramResource* programForVoice(
        int voiceIndex, bool firmwareFlag0ASet) const noexcept {
        const auto* binding = bindingForVoice(voiceIndex);
        if (binding == nullptr || !binding->active)
            return nullptr;
        return binding->variants[firmwareFlag0ASet ? 1u : 0u];
    }

    const MASSResourceCommand* commandForVoice(
        int voiceIndex, bool firmwareFlag0ASet,
        std::size_t commandIndex) const noexcept {
        const auto* program = programForVoice(voiceIndex, firmwareFlag0ASet);
        if (program == nullptr || commandIndex >= program->stream.commands.size())
            return nullptr;
        return &program->stream.commands[commandIndex];
    }

    void rewindCommandStream(int voiceIndex, bool firmwareFlag0ASet) noexcept {
        if (!validVoice(voiceIndex))
            return;
        auto& binding = voices_[static_cast<std::size_t>(voiceIndex)];
        binding.commandCursors[firmwareFlag0ASet ? 1u : 0u] = 0;
    }

    const MASSResourceCommand* nextCommand(
        int voiceIndex, bool firmwareFlag0ASet) noexcept {
        if (!validVoice(voiceIndex))
            return nullptr;
        const auto variant = firmwareFlag0ASet ? 1u : 0u;
        auto& binding = voices_[static_cast<std::size_t>(voiceIndex)];
        const auto* program = binding.active ? binding.variants[variant] : nullptr;
        if (program == nullptr)
            return nullptr;
        auto& cursor = binding.commandCursors[variant];
        if (cursor >= program->stream.commands.size())
            return nullptr;
        return &program->stream.commands[cursor++];
    }

    // Consume one immutable IC51 command and route its already-decoded kind
    // through the exact packet builders. Caller supplies the register/flag
    // snapshot at the corresponding firmware handler boundary.
    MASSFirmwarePacketPlan planNextCommand(
        int voiceIndex, bool firmwareFlag0ASet,
        const MASSFirmwareCommandBoundaryState& state) noexcept {
        const auto* command = nextCommand(voiceIndex, firmwareFlag0ASet);
        if (command == nullptr) {
            MASSFirmwarePacketPlan exhausted{};
            exhausted.status = MASSFirmwarePacketPlanStatus::commandStreamExhausted;
            return exhausted;
        }
        return MASSFirmwareResourceCommandPlanner::plan(*command, state);
    }

    // Group-08 path uses a different evidence-bounded projection than the
    // 0x1EDD packet builder. Its address-selection inputs are supplied as a
    // captured handler snapshot; no SAM9407 register semantics are inferred.
    MASS1C61PitchExecutionResult planNextPitchCommand(
        int voiceIndex, bool firmwareFlag0ASet,
        const MASSFirmwareROMView& firmwareROM,
        const MASS1C61PitchExecutionSnapshot& snapshot) noexcept {
        const auto* command = nextCommand(voiceIndex, firmwareFlag0ASet);
        if (command == nullptr) {
            MASS1C61PitchExecutionResult exhausted{};
            exhausted.status = MASS1C61PitchExecutionStatus::commandStreamExhausted;
            return exhausted;
        }
        return MASS1C61PitchParameterExecutionProjection::execute(
            firmwareROM, *command, snapshot);
    }

    std::size_t commandCursor(int voiceIndex, bool firmwareFlag0ASet) const noexcept {
        const auto* binding = bindingForVoice(voiceIndex);
        return binding != nullptr
            ? binding->commandCursors[firmwareFlag0ASet ? 1u : 0u] : 0u;
    }

    // Read access to the reconstructed startup memory window is intentionally
    // range-checked. This exposes bytes to a future verified decoder without
    // assigning them a PCM/sample interpretation.
    bool readExternalSoundMemory(std::uint16_t address,
                                 std::uint8_t& value) const noexcept {
        if (externalSoundMemoryImage_ == nullptr ||
            !externalSoundMemoryImage_->isLoaded() ||
            address < MASSIC51ExternalSoundMemoryImage::kBaseAddress ||
            static_cast<std::size_t>(address -
                MASSIC51ExternalSoundMemoryImage::kBaseAddress) >=
                MASSIC51ExternalSoundMemoryImage::kImageSize)
            return false;
        value = externalSoundMemoryImage_->byteAt(address);
        return true;
    }

private:
    const MASSResourceProgramCatalog* programCatalog_ = nullptr;
    const MASSIC51ExternalSoundMemoryImage* externalSoundMemoryImage_ = nullptr;
    std::array<VoiceProgramBinding, kVoiceCount> voices_{};

    static constexpr bool validVoice(int voiceIndex) noexcept {
        return voiceIndex >= 0 && voiceIndex < static_cast<int>(kVoiceCount);
    }

    void bind(int voiceIndex, const VoiceSlot& voice) noexcept {
        auto& binding = voices_[static_cast<std::size_t>(voiceIndex)];
        binding = VoiceProgramBinding{};
        binding.active = true;
        binding.releasing = voice.releasing;
        binding.part = voice.part;
        binding.channel = voice.channel;
        binding.note = voice.note;
        binding.velocity = voice.velocity;
        binding.bankNumber = voice.bankNumber;
        binding.soundNumber = voice.soundNumber;
        binding.drumSetNumber = voice.drumSetNumber;

        if (programCatalog_ == nullptr || !programCatalog_->isLoaded())
            return;

        // Firmware flag 0x0A is a transient interpreter branch, not a stable
        // Part/Performance setting. Cache both already-decoded outcomes and
        // leave branch selection to the exact command handler that needs it.
        binding.variants[0] = programCatalog_->find(
            voice.bankNumber, voice.soundNumber, false);
        binding.variants[1] = programCatalog_->find(
            voice.bankNumber, voice.soundNumber, true);

        // Both variants refer to the same descriptor bank/address for this
        // program. Keep a neutral summary while preserving both streams.
        const auto* summary = binding.variants[0] != nullptr
            ? binding.variants[0] : binding.variants[1];
        if (summary != nullptr) {
            binding.descriptorAddress = summary->descriptorAddress;
            binding.streamPointer = summary->descriptor.commandStreamPointer;
            binding.slotsConsumed = summary->stream.slotsConsumed;
            binding.outputSlotsWritten = summary->stream.outputSlotsWritten;
            binding.fillerSlotsWritten = summary->stream.fillerSlotsWritten;
            binding.commandCount = summary->stream.commands.size();
        }
    }
};

} // namespace technox
