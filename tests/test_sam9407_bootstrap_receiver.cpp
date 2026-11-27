#include "../Source/SAM9407BootstrapReceiver.h"

#include <cassert>
#include <cstdint>

using technox::SAM9407BootstrapReceiver;

int main() {
    SAM9407BootstrapReceiver receiver;

    // BOOT low bypasses the internal bootstrap ROM and starts from external
    // word address 0x0100. Burst writes are not consumed in this mode.
    receiver.reset(false);
    assert(!receiver.bootPinWasHigh());
    assert(receiver.isExecutingExternalMemory());
    assert(receiver.receivedWordCount() == 0);
    std::uint32_t pc = 0;
    assert(receiver.getEntryProgramCounter(pc));
    assert(pc == 0x0100u);
    assert(!receiver.writeBurstWord(0x1234u));

    // BOOT high waits for exactly 256 words from the decoded 16-bit burst
    // interface. No execution hand-off occurs after only 255 words.
    receiver.reset(true);
    assert(receiver.bootPinWasHigh());
    assert(receiver.isAwaitingBootstrapWords());
    assert(!receiver.getEntryProgramCounter(pc));
    std::uint16_t value = 0;
    assert(!receiver.readBootstrapWord(0x0100u, value));

    for (std::size_t i = 0; i < 255; ++i) {
        const auto word = static_cast<std::uint16_t>(0xA500u ^ i);
        assert(receiver.writeBurstWord(word));
        assert(receiver.isAwaitingBootstrapWords());
        assert(receiver.receivedWordCount() == i + 1);
    }

    assert(receiver.readBootstrapWord(0x0100u, value));
    assert(value == 0xA500u);
    assert(receiver.readBootstrapWord(0x01FEu, value));
    assert(value == static_cast<std::uint16_t>(0xA500u ^ 254u));
    assert(!receiver.readBootstrapWord(0x01FFu, value));
    assert(!receiver.readBootstrapWord(0x00FFu, value));
    assert(!receiver.readBootstrapWord(0x0200u, value));

    assert(receiver.writeBurstWord(0x5AA5u));
    assert(receiver.receivedWordCount() == 256u);
    assert(receiver.isExecutingExternalMemory());
    assert(receiver.getEntryProgramCounter(pc));
    assert(pc == 0x0100u);
    assert(receiver.readBootstrapWord(0x01FFu, value));
    assert(value == 0x5AA5u);

    // Extra host words are rejected and cannot overwrite the downloaded image.
    assert(!receiver.writeBurstWord(0xFFFFu));
    assert(receiver.readBootstrapWord(0x01FFu, value));
    assert(value == 0x5AA5u);

    // Reset restarts bootstrap and discards the previous partial/full image.
    receiver.reset(true);
    assert(receiver.isAwaitingBootstrapWords());
    assert(receiver.receivedWordCount() == 0u);
    assert(!receiver.readBootstrapWord(0x0100u, value));
    assert(receiver.writeBurstWord(0xCAFEu));
    assert(receiver.receivedWordCount() == 1u);
    assert(receiver.isAwaitingBootstrapWords());

    return 0;
}
