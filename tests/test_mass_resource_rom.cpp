#include "../Source/MASSResourceROM.h"

#include <array>
#include <cassert>
#include <cstdint>

using namespace technox;

namespace {
void writePointer(std::array<std::uint8_t, MASSResourceROM::kImageSize>& image,
                  MASSResourceBank bank,
                  std::uint8_t index,
                  std::uint16_t pointer) {
    const auto base = bank == MASSResourceBank::bank0 ? 0u : MASSResourceROM::kBankSize;
    const auto address = base + static_cast<std::size_t>(index) * 2;
    image[address] = static_cast<std::uint8_t>(pointer & 0xff);
    image[address + 1] = static_cast<std::uint8_t>(pointer >> 8);
}
}

int main() {
    std::array<std::uint8_t, MASSResourceROM::kImageSize> image{};
    image.fill(0xff);

    writePointer(image, MASSResourceBank::bank0, 5, 0x0200);
    image[0x0200] = 0x18;
    image[0x0201] = 0xaa;
    image[0x0202] = 0xbb;
    image[0x0203] = 0xcc;
    image[0x0204] = 0x08;
    image[0x0205] = 0x01;
    image[0x0206] = 0x02;
    image[0x0207] = 0x03;
    image[0x0208] = 0x04;
    image[0x0209] = 0x05;
    image[0x020a] = 0x06;
    image[0x020b] = 0xff;

    writePointer(image, MASSResourceBank::bank0, 6, 0x0300);
    image[0x0300] = 0x10;
    image[0x0301] = 0x00;
    image[0x0302] = 0x11;
    image[0x0303] = 0x22;
    image[0x0304] = 0x33;
    image[0x0305] = 0x44;
    image[0x0306] = 0x55;
    image[0x0307] = 0x66;
    image[0x0308] = 0xff;

    writePointer(image, MASSResourceBank::bank0, 7, 0x0400);
    image[0x0400] = 0x3b;
    image[0x0401] = 0x42;
    image[0x0402] = 0xff;

    writePointer(image, MASSResourceBank::bank0, 8, 0x0500);
    for (std::size_t i = 0; i < MASSResourceROM::kMaximumSlots; ++i)
        image[0x0500 + i] = 0x00;
    image[0x0510] = 0xff;

    writePointer(image, MASSResourceBank::bank0, 9, 0xfffc);
    image[0xfffc] = 0x08;
    image[0xfffd] = 0x01;

    writePointer(image, MASSResourceBank::bank1, 7, 0x1234);
    image[MASSResourceROM::kBankSize + 0x1234] = 0xff;

    MASSResourceROM rom;
    assert(!rom.isLoaded());
    assert(!rom.load(image.data(), image.size() - 1));
    assert(rom.load(image.data(), image.size()));
    assert(rom.isLoaded());

    std::uint16_t pointer = 0;
    assert(rom.readPointer(MASSResourceBank::bank0, 5, pointer));
    assert(pointer == 0x0200);
    assert(rom.readPointer(MASSResourceBank::bank1, 7, pointer));
    assert(pointer == 0x1234);

    MASSResourceSelection selection{};
    assert(MASSResourceROM::resolvePartResource(0, 0x35, selection));
    assert(selection.bank == MASSResourceBank::bank0);
    assert(selection.pointerIndex == 0x35 && selection.p3BankMask == 0x07);
    assert(MASSResourceROM::resolvePartResource(1, 0x35, selection));
    assert(selection.bank == MASSResourceBank::bank0);
    assert(selection.pointerIndex == 0xb5 && selection.p3BankMask == 0x07);
    assert(MASSResourceROM::resolvePartResource(2, 0x35, selection));
    assert(selection.bank == MASSResourceBank::bank1);
    assert(selection.pointerIndex == 0x35 && selection.p3BankMask == 0x06);
    assert(MASSResourceROM::resolvePartResource(3, 0x35, selection));
    assert(selection.bank == MASSResourceBank::bank1);
    assert(selection.pointerIndex == 0xb5 && selection.p3BankMask == 0x06);
    assert(MASSResourceROM::resolvePartResource(9, 0x35, selection));
    assert(selection.normalizedPartParameter == 3 && selection.pointerIndex == 0xb5);
    assert(!MASSResourceROM::resolvePartResource(-1, 0x35, selection));

    MASSDecodedResourceStream decoded{};
    assert(rom.decodeResource(MASSResourceBank::bank0, 5, decoded));
    assert(decoded.terminated && !decoded.truncated);
    assert(decoded.slotsConsumed == 2 && decoded.commands.size() == 2);
    assert(decoded.commands[0].kind == MASSResourceCommandKind::fourByteCommand);
    assert(decoded.commands[0].length == 4 && decoded.commands[0].bytes[3] == 0xcc);
    assert(decoded.commands[1].kind == MASSResourceCommandKind::pitchParameter);
    assert(decoded.commands[1].length == 7 && decoded.commands[1].bytes[6] == 0x06);
    assert(decoded.bytesConsumed == 11);

    assert(rom.decodeResource(MASSResourceBank::bank0, 6, decoded));
    assert(decoded.terminated && decoded.slotsConsumed == 1);
    assert(decoded.commands.size() == 1);
    assert(decoded.commands[0].kind == MASSResourceCommandKind::emptyMultiParameter);
    assert(decoded.commands[0].length == 8);

    assert(rom.decodeResource(MASSResourceBank::bank0, 7, decoded));
    assert(decoded.terminated && decoded.slotsConsumed == 2);
    assert(decoded.commands[0].kind == MASSResourceCommandKind::marker);
    assert(decoded.commands[1].kind == MASSResourceCommandKind::singleByte);

    assert(rom.decodeResource(MASSResourceBank::bank0, 8, decoded));
    assert(!decoded.terminated && decoded.slotLimitReached);
    assert(decoded.slotsConsumed == MASSResourceROM::kMaximumSlots);
    assert(decoded.commands.size() == MASSResourceROM::kMaximumSlots);

    assert(rom.decodeResource(MASSResourceBank::bank0, 9, decoded));
    assert(decoded.truncated && !decoded.terminated);
    assert(decoded.commands.empty());

    assert(rom.decodeResource(MASSResourceBank::bank1, 7, decoded));
    assert(decoded.terminated && decoded.commands.empty());

    rom.clear();
    assert(!rom.isLoaded());
    assert(!rom.readPointer(MASSResourceBank::bank0, 0, pointer));
    return 0;
}
