#include "../Source/MASSResourceROM.h"
#include "../Source/MASSAudioEngine.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <utility>
#include <vector>

using namespace technox;

namespace {
class ResourceCatalogProbeBackend final : public IMASSVoiceBackend {
public:
    void setProgramCatalog(const MASSResourceProgramCatalog* catalog) noexcept override {
        catalog_ = catalog;
    }
    void setExternalSoundMemoryImage(
        const MASSIC51ExternalSoundMemoryImage* image) noexcept override {
        receivedExternalMemoryImage = image;
    }
    void prepare(double, int) override {}
    void noteOn(int, const VoiceSlot& voice) noexcept override {
        const auto* resource = catalog_ != nullptr
            ? catalog_->find(voice.bankNumber, voice.soundNumber) : nullptr;
        resolvedOnNote = resource != nullptr && !resource->stream.commands.empty();
        if (resource != nullptr) {
            descriptorAddress = resource->descriptorAddress;
            commandCount = resource->stream.commands.size();
        }
    }
    void noteOff(int, const VoiceSlot&) noexcept override {}
    void resetVoice(int, const VoiceSlot&) noexcept override {}
    bool isVoiceFinished(int) const noexcept override { return false; }
    void render(float* const*, int, int, const VoiceManager&, const ModulationMatrix&,
                const MASSPartParameterStore&, const MASSModulationSourceStore&,
                const MASSPartRuntimeControlStore&, const MASSCommonControlStore&,
                const MASSPerformanceImageStore&, const MASSDrumParameterStore&) noexcept override {}
    bool resolvedOnNote = false;
    std::uint16_t descriptorAddress = 0;
    std::size_t commandCount = 0;
    const MASSIC51ExternalSoundMemoryImage* receivedExternalMemoryImage = nullptr;
private:
    const MASSResourceProgramCatalog* catalog_ = nullptr;
};
void writePointerBE(std::vector<std::uint8_t>& image,
                    MASSResourceBank bank,
                    std::uint8_t index,
                    std::uint16_t address) {
    const auto base = bank == MASSResourceBank::bank0 ? 0u : MASSResourceROM::kBankSize;
    const auto offset = base + static_cast<std::size_t>(index) * 2;
    image[offset] = static_cast<std::uint8_t>(address >> 8);
    image[offset + 1] = static_cast<std::uint8_t>(address & 0xff);
}

void testMASS1C61AccumulatorTransform() {
    // Positive input, no incoming carry.
    auto result = MASS1C61AccumulatorTransform::apply(
        0x12, 0x34, 0x02, 0x05, 0x06, 0x07, 0x11, 0x22, 0x03);
    assert(!result.zeroInputBypass && !result.negativeInput);
    assert(result.r2 == 0x2d && result.r3 == 0x06 && result.r4 == 0x07);

    // The initial address carry changes the MOVC page, not the final math:
    // RLC A consumes it, but A is overwritten before the second MUL.
    result = MASS1C61AccumulatorTransform::apply(
        0x12, 0x34, 0x02, 0x05, 0x06, 0x07, 0x11, 0x22, 0x03);
    assert(result.r2 == 0x2d && result.r3 == 0x06 && result.r4 == 0x07);

    // Negative R2 takes the absolute-value path, computes the 16-bit
    // multiply-sum, negates it, sign extends, then adds R5:R6:R7.
    result = MASS1C61AccumulatorTransform::apply(
        0x80, 0x20, 0x03, 0x01, 0x02, 0x03, 0x11, 0x22, 0x03);
    assert(result.negativeInput);
    assert(result.r2 == 0x71 && result.r3 == 0x00 && result.r4 == 0x03);

    // R2==0 bypasses the multiplies and copies direct RAM[0x05..0x07].
    result = MASS1C61AccumulatorTransform::apply(
        0x00, 0x20, 0x03, 0x01, 0x02, 0x03, 0x45, 0x56, 0x67);
    assert(result.zeroInputBypass && result.r2 == 0x45 &&
           result.r3 == 0x56 && result.r4 == 0x67);
}

void testMASS1C61SecondaryTableAddress() {
    const auto normal = MASS1C61SecondaryTableAddressBuilder::resolve(
        0x4000, 0x30, 0x20, 0x00);
    assert(normal.normalizedIndex == 0x50 && normal.bankOffset == 0);
    assert(normal.address == 0x4050);

    const auto upper = MASS1C61SecondaryTableAddressBuilder::resolve(
        0x4000, 0x30, 0x20, 0x10);
    assert(upper.normalizedIndex == 0x50 && upper.bankOffset == 0x180);
    assert(upper.address == 0x41d0);

    const auto negativeWrapped = MASS1C61SecondaryTableAddressBuilder::resolve(
        0x4000, 0x80, 0x05, 0x00);
    assert(negativeWrapped.normalizedIndex == 0x79 && negativeWrapped.address == 0x4079);

    const auto positiveWrapped = MASS1C61SecondaryTableAddressBuilder::resolve(
        0x4000, 0xc0, 0x00, 0x00);
    assert(positiveWrapped.normalizedIndex == 0x08 && positiveWrapped.address == 0x4008);

    const auto noCarry = MASS1C61SecondaryTableFetchAddressBuilder::addOriginalR7(
        0x4050, 0x10);
    assert(noCarry.address == 0x4060 && !noCarry.pageCarry);
    const auto withCarry = MASS1C61SecondaryTableFetchAddressBuilder::addOriginalR7(
        0x40f0, 0x20);
    assert(withCarry.address == 0x4110 && withCarry.pageCarry);

    // One synthetic code ROM backs both the base-DPTR and adjusted-DPTR reads.
    std::vector<std::uint8_t> image(0x20000, 0xff);
    image[0x4050] = 0x12;
    image[0x40d0] = 0x34;
    image[0x4150] = 0x56;
    image[0x4210] = 0x33;
    image[0x4290] = 0x44;
    image[0x4310] = 0x55;
    MASSFirmwareROMView view;
    assert(view.attach(image.data(), image.size()));

    MASS1C61SecondaryTableBytes bytes{};
    assert(MASS1C61SecondaryTableReader::read(view, normal, bytes));
    assert((bytes.bytes == std::array<std::uint8_t, 3>{{0x12, 0x34, 0x56}}));
    const auto fields = MASS1C61PackedLookupDecoder::decode(bytes);
    assert(fields.r5 == 0x12 && fields.r6 == 0x34 && fields.r7 == 0x56);
    assert(fields.r3 == 0x82 && fields.r4 == 0xce);

    MASS1C61SecondaryTableBytes triplet{};
    MASS1C61SecondaryTableFetchAddress resolved{};
    assert(MASS1C61SecondaryTableReader::read(
        view, upper, 0x40, triplet, &resolved));
    assert(resolved.address == 0x4210 && resolved.pageCarry);
    assert((triplet.bytes == std::array<std::uint8_t, 3>{{0x33, 0x44, 0x55}}));
}

void testMASS1C61InterpolationLookup() {
    std::vector<std::uint8_t> romBytes(0x20000, 0xff);
    const std::array<std::uint8_t, 12> positive{{
        0x80, 0x88, 0x90, 0x98, 0xa1, 0xab,
        0xb5, 0xc0, 0xcb, 0xd7, 0xe4, 0xf2
    }};
    const std::array<std::uint8_t, 12> negative{{
        0xff, 0xf2, 0xe4, 0xd7, 0xcb, 0xc0,
        0xb5, 0xab, 0xa1, 0x98, 0x90, 0x88
    }};
    std::copy(positive.begin(), positive.end(), romBytes.begin() + 0x11f0);
    std::copy(negative.begin(), negative.end(), romBytes.begin() + 0x11fc);
    MASSFirmwareROMView view;
    assert(view.attach(romBytes.data(), romBytes.size()));

    MASS1C61InterpolationLookupResult result{};
    assert(MASS1C61InterpolationLookup::resolve(view, 0x07, result));
    assert(result.lookupPerformed && result.signedIndex == 0x07);
    assert(result.shiftCount == 0 && result.tableRemainder == 7);
    assert(result.coefficient == 0xc0 && !result.negativeBranch);

    // XDATA 0x2067 = -1 selects the negative table with magnitude 1.
    assert(MASS1C61InterpolationLookup::resolve(view, 0xff, result));
    assert(result.lookupPerformed && result.signedIndex == 0x01);
    assert(result.shiftCount == 0 && result.tableRemainder == 1);
    assert(result.coefficient == 0xf2 && result.negativeBranch);

    // XDATA 0x2067 = -7 becomes +7 before division; table 0x11FC[7].
    assert(MASS1C61InterpolationLookup::resolve(view, 0xf9, result));
    assert(result.signedIndex == 0x07 && result.shiftCount == 0);
    assert(result.tableRemainder == 7 && result.coefficient == 0xab);

    // Positive +13 and negative -13 both produce quotient 1/remainder 1,
    // but select different tables.
    assert(MASS1C61InterpolationLookup::resolve(view, 0x0d, result));
    assert(result.signedIndex == 0x0d && result.shiftCount == 1);
    assert(result.tableRemainder == 1 && result.coefficient == 0x88);
    assert(MASS1C61InterpolationLookup::resolve(view, 0xf3, result));
    assert(result.signedIndex == 0x0d && result.shiftCount == 1);
    assert(result.tableRemainder == 1 && result.coefficient == 0xf2);
    assert(result.negativeBranch);

    assert(MASS1C61InterpolationLookup::resolve(view, 0x00, result));
    assert(!result.lookupPerformed);
}

void testMASS1C61InterpolationArithmetic() {
    std::vector<std::uint8_t> romBytes(0x20000, 0xff);
    const std::array<std::uint8_t, 12> positive{{
        0x80, 0x88, 0x90, 0x98, 0xa1, 0xab,
        0xb5, 0xc0, 0xcb, 0xd7, 0xe4, 0xf2
    }};
    const std::array<std::uint8_t, 12> negative{{
        0xff, 0xf2, 0xe4, 0xd7, 0xcb, 0xc0,
        0xb5, 0xab, 0xa1, 0x98, 0x90, 0x88
    }};
    std::copy(positive.begin(), positive.end(), romBytes.begin() + 0x11f0);
    std::copy(negative.begin(), negative.end(), romBytes.begin() + 0x11fc);
    MASSFirmwareROMView view;
    assert(view.attach(romBytes.data(), romBytes.size()));

    MASS1C61InterpolationLookupResult lookup{};
    assert(MASS1C61InterpolationLookup::resolve(view, 0x07, lookup));
    const auto positiveResult =
        MASS1C61InterpolationArithmetic::apply(lookup, 0x12, 0x34, 0x01);
    assert(positiveResult.value24 == 0x01ce1a);
    assert(positiveResult.r2 == 0x1a && positiveResult.r3 == 0xce &&
           positiveResult.r4 == 0x01);

    assert(MASS1C61InterpolationLookup::resolve(view, 0xf9, lookup));
    const auto negativeResult =
        MASS1C61InterpolationArithmetic::apply(lookup, 0x12, 0x34, 0x01);
    assert(negativeResult.value24 == 0x00cdc8);
    assert(negativeResult.r2 == 0xc8 && negativeResult.r3 == 0xcd &&
           negativeResult.r4 == 0x00);

    assert(MASS1C61InterpolationLookup::resolve(view, 0x00, lookup));
    const auto unchanged =
        MASS1C61InterpolationArithmetic::apply(lookup, 0x12, 0x34, 0x01);
    assert(unchanged.value24 == 0x013412);
}

void testMASS1EDDLookupIndexBuilder() {
    std::vector<std::uint8_t> romBytes(0x20000, 0xff);
    for (std::size_t i = 0; i < 128; ++i)
        romBytes[0x1288 + i] = static_cast<std::uint8_t>(i);
    MASSFirmwareROMView view;
    assert(view.attach(romBytes.data(), romBytes.size()));

    MASS1EDDLookupResult result{};
    // B=RL(0x21)=0x42, ordinary path: (~high(MUL))>>1 = 0x6C.
    assert(MASS1EDDLookupIndexBuilder::build(view, 0x12, 0x35, 0x21, result));
    assert(result.lookupPerformed && !result.bSignBranch);
    assert(result.productHighByte == 0x26 && result.tableIndex == 0x6c);
    assert(result.curveValue == 0x6c);

    // Opcode.6 selects complemented RAM[0x30] when opcode.0 is clear.
    assert(MASS1EDDLookupIndexBuilder::build(view, 0x52, 0x81, 0x41, result));
    assert(result.lookupPerformed && result.bSignBranch);
    assert(result.productHighByte == 0x7f && result.tableIndex == 0x7f);

    // Sign branch with a non-saturating high byte returns B directly.
    assert(MASS1EDDLookupIndexBuilder::build(view, 0x02, 0x12, 0x40, result));
    assert(result.bSignBranch && result.productHighByte == 0x12);
    assert(result.tableIndex == 0x12);

    // R4=0 is an early alternate path and must not access 0x1288.
    assert(MASS1EDDLookupIndexBuilder::build(view, 0x12, 0x35, 0x00, result));
    assert(!result.lookupPerformed);
}

void testMASS1EDDParameterPipeline() {
    std::vector<std::uint8_t> romBytes(0x20000, 0xff);
    for (std::size_t i = 0; i < 128; ++i)
        romBytes[0x1288 + i] = 0x55; // fixed curve value for deterministic pipeline tests
    romBytes[0x1308 + 0x55] = 0xa6;

    MASSFirmwareROMView view;
    assert(view.attach(romBytes.data(), romBytes.size()));
    MASS1EDDParameterPipelineResult result{};

    // R4==0 skips both ROM tables. With R7==0, B remains the original R3.
    assert(MASS1EDDParameterPipeline::process(
        view, 0x04, 0x22, 0x33, 0x00, 0x00, 0x10,
        0x1200, 0x02, 0x90, 0x00, 0x88, result));
    assert(result.r4 == 0xff && result.coefficientB == 0x33);
    assert(!result.nonlinearLookupPerformed && !result.externalReadRequired);

    // Full nonlinear path: 0x1288 produces 0x55, then all three gates allow
    // 0x1308 to map it to 0xA6. R7=0 skips the external 0x2381/0x1848 tail.
    assert(MASS1EDDParameterPipeline::process(
        view, 0x04, 0x22, 0x33, 0x21, 0x00, 0x10,
        0x1200, 0x02, 0x90, 0x00, 0x88, result));
    assert(result.r4 == 0xa6 && result.coefficientB == 0x33);
    assert(result.nonlinearLookupPerformed && result.quantizeLookupUsed);
    assert(!result.externalReadRequired && !result.externalTransformApplied);

    // R7 nonzero invokes 0x2381 then 0x1848. The caller supplies the byte
    // read from the computed P3-selected address.
    assert(MASS1EDDParameterPipeline::process(
        view, 0x04, 0x22, 0x5d, 0x00, 0x00, 0x10,
        0x1200, 0x02, 0x90, 0x40, 0x80, result));
    const auto expected = MASS1848Arithmetic::execute(0x80, 0x40, 0x5d);
    assert(result.r4 == 0xff && result.externalReadRequired);
    assert(result.externalAddress == 0x1260);
    assert(result.externalTransformApplied);
    assert(result.coefficientB == expected.value);
    assert(result.externalTransform.overflow == expected.overflow);
}

void testMASS1EDDPostLookup() {
    std::vector<std::uint8_t> romBytes(0x20000, 0xff);
    romBytes[0x1308 + 0x55] = 0xa6;
    MASSFirmwareROMView view;
    assert(view.attach(romBytes.data(), romBytes.size()));

    MASS1EDDPostLookupResult result{};
    // All three gates pass: RAM[3F].7=0, RAM[40].4=1, opcode.2=1.
    assert(MASS1EDDPostLookup::apply(
        view, 0x04, 0x00, 0x10, 0x33, 0x55, result));
    assert(result.quantizeLookupUsed && result.r4 == 0xa6);
    assert(result.coefficientB == 0x33 && !result.fallbackSignCorrection);

    // Gate fails on RAM[3F].7: plain R3*curve high byte, R4 sentinel.
    assert(MASS1EDDPostLookup::apply(
        view, 0x02, 0x80, 0x10, 0x80, 0x80, result));
    assert(!result.quantizeLookupUsed && result.r4 == 0xff);
    assert(result.coefficientB == 0x40 && !result.fallbackSignCorrection);

    // Opcode.6 set and opcode.0 clear activates complemented-R3 multiply,
    // then adds the original R3 to the high product byte.
    assert(MASS1EDDPostLookup::apply(
        view, 0x40, 0x80, 0x00, 0x20, 0x40, result));
    assert(result.r4 == 0xff && result.fallbackSignCorrection);
    assert(result.coefficientB == 0x57);

    // Common nonzero guard: a zero high product with nonzero R3 becomes 1.
    assert(MASS1EDDPostLookup::apply(
        view, 0x01, 0x80, 0x00, 0x01, 0x01, result));
    assert(result.coefficientB == 1 && result.r4 == 0xff);
}

void testMASS2381AddressBuilder() {
    const auto low = MASS2381AddressBuilder::resolve(0x1200, 0x03, 0x20);
    assert(low.selector == 3 && low.offset == 0x0a && low.address == 0x129a);

    const auto clamped = MASS2381AddressBuilder::resolve(0x1200, 0x03, 0x70);
    assert(clamped.selector == 3 && clamped.offset == 0x2f && clamped.address == 0x12bf);

    const auto zeroOffset = MASS2381AddressBuilder::resolve(0x1200, 0x02, 0x90);
    assert(zeroOffset.selector == 2 && zeroOffset.offset == 0x00 && zeroOffset.address == 0x1260);

    const auto wrap = MASS2381AddressBuilder::resolve(0xfff0, 0x00, 0x20);
    assert(wrap.selector == 0 && wrap.offset == 0x0a && wrap.address == 0xfffa);

    // Caller-side preprocessing paths: R5 high nibble or an external byte.
    const std::uint8_t rawR5 = 0x3f;
    const auto fromR5 = MASS2381AddressBuilder::resolve(
        0x1200, static_cast<std::uint8_t>(((rawR5 << 4) | (rawR5 >> 4)) & 0x03),
        0x20);
    assert(fromR5.selector == 3 && fromR5.address == low.address);
    const auto fromExternalByte = MASS2381AddressBuilder::resolve(
        0x1200, static_cast<std::uint8_t>(0xA7 & 0x03), 0x20);
    assert(fromExternalByte.selector == 3 && fromExternalByte.address == low.address);
}

void testMASSModulationScaleMode41() {
    std::vector<std::uint8_t> romBytes(0x20000, 0xff);
    const std::array<std::uint8_t, 8> table0{{0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0}};
    const std::array<std::uint8_t, 8> table1{{0x10, 0x20, 0x38, 0x50, 0x70, 0x90, 0xc0, 0xe0}};
    std::copy(table0.begin(), table0.end(), romBytes.begin() + 0x11e0);
    std::copy(table1.begin(), table1.end(), romBytes.begin() + 0x11e8);
    MASSFirmwareROMView view;
    assert(view.attach(romBytes.data(), romBytes.size()));

    MASSModulationScaleResult result{};
    assert(MASSModulationScaleMode41::apply(view, 0x41, 0x80, 0x00, 0x7f, result));
    assert(result.combinedCoefficient == 0x80 && result.value == 0x3f);

    assert(MASSModulationScaleMode41::apply(view, 0x41, 0x00, 0x80, 0x7f, result));
    assert(result.combinedCoefficient == 0x0f && result.value == 0x07);

    assert(MASSModulationScaleMode41::apply(view, 0x41, 0x80, 0x80, 0x7f, result));
    assert(result.combinedCoefficient == 0x08 && result.value == 0x03);

    assert(!MASSModulationScaleMode41::apply(view, 0x01, 0x80, 0x80, 0x7f, result));
}

void testMASS1EDDFullParameterPipeline() {
    std::vector<std::uint8_t> romBytes(0x20000, 0xff);
    for (std::size_t i = 0; i < 128u; ++i)
        romBytes[0x1288u + i] = static_cast<std::uint8_t>(i);
    for (std::size_t i = 0; i < 256u; ++i)
        romBytes[0x1308u + i] = static_cast<std::uint8_t>(i);
    const std::array<std::uint8_t, 8> modulationA{{
        0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0
    }};
    const std::array<std::uint8_t, 8> modulationB{{
        0x10, 0x20, 0x38, 0x50, 0x70, 0x90, 0xc0, 0xe0
    }};
    std::copy(modulationA.begin(), modulationA.end(), romBytes.begin() + 0x11e0);
    std::copy(modulationB.begin(), modulationB.end(), romBytes.begin() + 0x11e8);

    MASSFirmwareROMView view;
    assert(view.attach(romBytes.data(), romBytes.size()));

    MASS1EDDFullParameterPipelineResult result{};

    // R4=0 bypasses the nonlinear table/external-read stage. The complete
    // 0x41 path then applies the 0x11E0 coefficient table to R3.
    assert(MASS1EDDFullParameterPipeline::process(
        view, 0x41, 0x35, 0x40, 0x00, 0x00, 0x00,
        0x1200, 0x00, 0x80, 0x00, 0x00,
        0x00, 0x00, 0x80, 0x00, result));
    assert(!result.nonlinearAndExternal.nonlinearLookupPerformed);
    assert(!result.nonlinearAndExternal.externalReadRequired);
    assert(result.nonlinearAndExternal.coefficientB == 0x40);
    assert(result.slotCoefficient.modulationApplied);
    assert(result.slotCoefficient.coefficientB == 0x20);

    // The 0x01 selector uses the alternate 0x1890 arithmetic on RAM[0x43].
    assert(MASS1EDDFullParameterPipeline::process(
        view, 0x01, 0x10, 0x40, 0x00, 0x00, 0x00,
        0x1200, 0x00, 0x80, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x80, result));
    assert(result.slotCoefficient.modulationApplied);
    assert(result.slotCoefficient.coefficientB ==
           MASS1890Arithmetic::apply(0x80, 0x40));

    // The 0x40 selector takes the complementary RAM[0x41] branch.
    assert(MASS1EDDFullParameterPipeline::process(
        view, 0x40, 0x10, 0x40, 0x00, 0x00, 0x00,
        0x1200, 0x00, 0x80, 0x00, 0x00,
        0x00, 0x00, 0x80, 0x00, result));
    assert(result.slotCoefficient.modulationApplied);
    assert(result.slotCoefficient.coefficientB ==
           MASS1890Arithmetic::apply(0x80, 0x40));

    // Mask 0x00 bypasses both the slot scale and modulation arithmetic.
    assert(MASS1EDDFullParameterPipeline::process(
        view, 0x00, 0x10, 0x40, 0x00, 0x00, 0x00,
        0x1200, 0x00, 0x80, 0x00, 0x00,
        0x00, 0x00, 0x80, 0x80, result));
    assert(!result.slotCoefficient.slotOpcodeScaleApplied);
    assert(!result.slotCoefficient.modulationApplied);
    assert(result.slotCoefficient.coefficientB == 0x40);

    // Opcode bit1 selects the two-stage RL/MUL scale before the modulation
    // dispatch; RAM[0x41] then applies the independent 0x40 alternate path.
    assert(MASS1EDDFullParameterPipeline::process(
        view, 0x42, 0x10, 0xff, 0x00, 0x00, 0x00,
        0x1200, 0x00, 0x80, 0x00, 0x00,
        0x40, 0x01, 0x80, 0x00, result));
    assert(result.slotCoefficient.slotOpcodeScaleApplied);
    assert(result.slotCoefficient.modulationApplied);

    // Full pipeline with R7!=0: the caller-provided byte is passed through
    // the 0x2381 address stage and 0x1848 arithmetic before the modulation
    // coefficient branch consumes the updated coefficient.
    MASS1EDDFullParameterPipelineResult externalResult{};
    assert(MASS1EDDFullParameterPipeline::process(
        view, 0x41, 0x35, 0x40, 0x21, 0x00, 0x00,
        0x1200, 0x02, 0x20, 0x40, 0x80,
        0x00, 0x00, 0x80, 0x00, externalResult));
    MASS1EDDParameterPipelineResult externalBase{};
    assert(MASS1EDDParameterPipeline::process(
        view, 0x41, 0x35, 0x40, 0x21, 0x00, 0x00,
        0x1200, 0x02, 0x20, 0x40, 0x80, externalBase));
    assert(externalBase.externalReadRequired);
    assert(externalBase.externalAddress ==
           MASS2381AddressBuilder::resolve(0x1200, 0x02, 0x20).address);
    assert(externalBase.externalTransformApplied);
    MASS1EDDSlotCoefficientResult externalSlot{};
    assert(MASS1EDDSlotCoefficientStage::apply(
        view, 0x41, externalBase.coefficientB,
        0x00, 0x00, 0x80, 0x00, externalSlot));
    assert(externalResult.nonlinearAndExternal.coefficientB ==
           externalBase.coefficientB);
    assert(externalResult.slotCoefficient.coefficientB ==
           externalSlot.coefficientB);
}

void testMASS1EDDSlotCoefficientStage() {
    std::vector<std::uint8_t> romBytes(0x20000, 0xff);
    const std::array<std::uint8_t, 8> table0{{0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0}};
    const std::array<std::uint8_t, 8> table1{{0x10, 0x20, 0x38, 0x50, 0x70, 0x90, 0xc0, 0xe0}};
    std::copy(table0.begin(), table0.end(), romBytes.begin() + 0x11e0);
    std::copy(table1.begin(), table1.end(), romBytes.begin() + 0x11e8);
    MASSFirmwareROMView view;
    assert(view.attach(romBytes.data(), romBytes.size()));

    MASS1EDDSlotCoefficientResult result{};

    // Opcode bit1 enables the two-MUL scale: RL(40)=80; high(80*FF)=7F;
    // RL(7F)=FE; high(FE*FF)=FD.
    assert(MASS1EDDSlotCoefficientStage::apply(
        view, 0x02, 0xff, 0x40, 0xff, 0x00, 0x00, result));
    assert(result.slotOpcodeScaleApplied && !result.modulationApplied);
    assert(result.coefficientB == 0xfd);

    // The second MUL can produce zero; nonzero RAM[32] forces coefficient 1.
    assert(MASS1EDDSlotCoefficientStage::apply(
        view, 0x02, 0xff, 0x40, 0x01, 0x00, 0x00, result));
    assert(result.coefficientB == 1);

    // Opcode bit1 clear bypasses the slot scale; mode 0x41 modulation still
    // runs when either RAM[0x41] or RAM[0x43] is nonzero.
    assert(MASS1EDDSlotCoefficientStage::apply(
        view, 0x41, 0x40, 0x00, 0x00, 0x80, 0x00, result));
    assert(!result.slotOpcodeScaleApplied && result.modulationApplied);
    assert(result.coefficientB == 0x20);

    // 0x01 dispatches RAM[0x43] through the alternate 0x1890 entry.
    assert(MASS1EDDSlotCoefficientStage::apply(
        view, 0x01, 0x7f, 0x00, 0x00, 0x80, 0x40, result));
    assert(result.modulationApplied && result.coefficientB == 0x7f);

    // 0x40 dispatches RAM[0x41] through the same entry's ACC.7 sign branch.
    assert(MASS1EDDSlotCoefficientStage::apply(
        view, 0x40, 0x7f, 0x00, 0x00, 0x80, 0x40, result));
    assert(result.modulationApplied && result.coefficientB == 0x00);

    // If the selected side is zero, 0x23C7 returns without changing R2,
    // even if the other modulation byte is nonzero.
    assert(MASS1EDDSlotCoefficientStage::apply(
        view, 0x01, 0x55, 0x00, 0x00, 0x80, 0x00, result));
    assert(!result.modulationApplied && result.coefficientB == 0x55);

    assert(MASS1EDDSlotCoefficientStage::apply(
        view, 0x40, 0x55, 0x00, 0x00, 0x00, 0x80, result));
    assert(!result.modulationApplied && result.coefficientB == 0x55);
}

void testMASS1890Arithmetic() {
    // Entry 0x1890 joins the second half of 0x1848. These vectors cover
    // both ACC.7 entry paths and the resulting multiply/accumulate byte.
    assert(MASS1890Arithmetic::apply(0x40, 0x7f) == 0x7f);
    assert(MASS1890Arithmetic::apply(0x80, 0x7f) == 0x00);
    assert(MASS1890Arithmetic::apply(0xc0, 0x7f) == 0x40);
    assert(MASS1890Arithmetic::apply(0x20, 0x40) == 0x4f);
}

void testMASS1848Arithmetic() {
    const auto ordinary = MASS1848Arithmetic::execute(0x01, 0x4d, 0x5d);
    assert(ordinary.value == 0x5d && !ordinary.carryOut &&
           !ordinary.overflow && !ordinary.saturated);

    // A set ACC.7 causes sign normalization; each XCH is followed by a
    // fresh test of the current accumulator sign bit.
    const auto signedInputs = MASS1848Arithmetic::execute(0x20, 0x70, 0xf0);
    assert(signedInputs.value == 0xed && signedInputs.carryOut &&
           !signedInputs.overflow && !signedInputs.saturated);

    // Signed overflow with no carry saturates upward to 0x7F.
    const auto clampPositive = MASS1848Arithmetic::execute(0x7f, 0x7f, 0x7f);
    assert(clampPositive.value == 0x7f && !clampPositive.carryOut &&
           clampPositive.overflow && clampPositive.saturated);

    // Signed overflow with carry saturates downward to 0x81.
    const auto clampNegative = MASS1848Arithmetic::execute(0x80, 0x81, 0x81);
    assert(clampNegative.value == 0x81 && clampNegative.carryOut &&
           clampNegative.overflow && clampNegative.saturated);
}

void testFirmwareROMTableView() {
    std::vector<std::uint8_t> synthetic(0x20000, 0xff);
    synthetic[0x1288] = 0x12;
    synthetic[0x1308 + 0x12] = 0x58;
    synthetic[0x11e0] = 0x80;
    MASSFirmwareROMView view;
    assert(!view.isAttached());
    assert(!view.attach(nullptr, synthetic.size()));
    assert(!view.attach(synthetic.data(), 0x1407));
    assert(view.attach(synthetic.data(), synthetic.size()));
    std::uint8_t value = 0;
    assert(view.read(MASSFirmwareLookupTable::modulationScale11E0, 0, value) && value == 0x80);
    assert(view.lookupNonlinearScale(0, value) && value == 0x58);
    assert(!view.read(MASSFirmwareLookupTable::nonlinearScale1288, 128, value));
    assert(!view.read(MASSFirmwareLookupTable::quantizeMap1308, 256, value));
    view.detach();
    assert(!view.isAttached());
    assert(!view.read(MASSFirmwareLookupTable::nonlinearScale1288, 0, value));
}

void testActualFirmwareROMTables(const char* path) {
    std::ifstream file(path, std::ios::binary);
    assert(file.good());
    std::vector<std::uint8_t> image(
        (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    assert(image.size() == 0x20000);
    MASSFirmwareROMView view;
    assert(view.attach(image.data(), image.size()));
    std::uint8_t value = 0;
    assert(view.read(MASSFirmwareLookupTable::modulationScale11E0, 0, value) && value == 0x80);
    assert(view.read(MASSFirmwareLookupTable::modulationScale11E0, 7, value) && value == 0xf0);
    assert(view.read(MASSFirmwareLookupTable::modulationScale11E8, 0, value) && value == 0x10);
    assert(view.read(MASSFirmwareLookupTable::modulationScale11E8, 7, value) && value == 0xe0);
    assert(view.read(MASSFirmwareLookupTable::interpolationCurve11F0, 0, value) && value == 0x80);
    assert(view.read(MASSFirmwareLookupTable::interpolationCurve11F0, 11, value) && value == 0xf2);
    assert(view.read(MASSFirmwareLookupTable::interpolationCurve11FC, 0, value) && value == 0xff);
    assert(view.read(MASSFirmwareLookupTable::interpolationCurve11FC, 11, value) && value == 0x88);
    assert(view.read(MASSFirmwareLookupTable::parameterMap1208, 0, value) && value == 0x00);
    assert(view.read(MASSFirmwareLookupTable::parameterMap1208, 127, value) && value == 0x7f);
    assert(view.read(MASSFirmwareLookupTable::nonlinearScale1288, 0, value) && value == 0x00);
    assert(view.read(MASSFirmwareLookupTable::nonlinearScale1288, 127, value) && value == 0xff);
    assert(view.read(MASSFirmwareLookupTable::quantizeMap1308, 0, value) && value == 0x00);
    assert(view.read(MASSFirmwareLookupTable::quantizeMap1308, 255, value) && value == 0xf8);
    for (const auto table : {MASSFirmwareLookupTable::parameterMap1208,
                             MASSFirmwareLookupTable::nonlinearScale1288,
                             MASSFirmwareLookupTable::quantizeMap1308}) {
        std::uint8_t previous = 0;
        for (std::size_t i = 0; i < MASSFirmwareROMView::tableLength(table); ++i) {
            assert(view.read(table, i, value));
            if (i != 0) assert(value >= previous);
            previous = value;
        }
    }
    for (std::size_t index = 0; index < 128; ++index)
        assert(view.lookupNonlinearScale(static_cast<std::uint8_t>(index), value));
}

void testPointerTableAndPartMapping() {
    std::vector<std::uint8_t> image(MASSResourceROM::kImageSize, 0xff);
    writePointerBE(image, MASSResourceBank::bank0, 5, 0x1234);
    writePointerBE(image, MASSResourceBank::bank1, 7, 0xabcd);

    MASSResourceROM rom;
    assert(!rom.isLoaded());
    assert(!rom.load(image.data(), image.size() - 1));
    assert(rom.load(image.data(), image.size()));
    assert(rom.isLoaded());

    std::uint16_t address = 0;
    assert(rom.readResourceDescriptorAddress(MASSResourceBank::bank0, 5, address));
    assert(address == 0x1234);
    assert(rom.readResourceDescriptorAddress(MASSResourceBank::bank1, 7, address));
    assert(address == 0xabcd);

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

    rom.clear();
    assert(!rom.isLoaded());
    assert(!rom.readResourceDescriptorAddress(MASSResourceBank::bank0, 0, address));
}

void testResourceSelectionRecordParser() {
    const auto part12Address = MASSResourceSelectionRecordParser::addressForRuntimePart(12);
    assert(part12Address.page == 0x29 && part12Address.offset == 0x90);
    assert(part12Address.linearAddress() == 0x2990);
    const auto part15Address = MASSResourceSelectionRecordParser::addressForRuntimePart(15);
    assert(part15Address.page == 0x29 && part15Address.offset == 0xb4);

    std::array<std::uint8_t, 20> page{};
    const std::array<std::uint8_t, 12> record = {
        0x92, 0x31, 0x15, 0x44, 0x55, 0x24, 0xd4, 0x77, 0x88, 0x05, 0x2a, 0x66
    };
    std::copy(record.begin(), record.end(), page.begin() + 4);

    MASSResourceSelectionRecord parsed{};
    assert(MASSResourceSelectionRecordParser::parse(
        page.data(), page.size(), 4, parsed));
    assert(parsed.selectionCode() == 0x12);
    assert(parsed.descriptorAddress() == 0x24d4);
    assert(parsed.p3ControlFlags() == 0x05);
    assert(parsed.lookupIndex0() == 0x15);
    assert(parsed.lookupIndex1() == 0x2a);

    // MOVX @R0 increments only the low byte. A record beginning at 0xFA
    // wraps from 0xFF to 0x00 while remaining on the P2-selected page.
    std::array<std::uint8_t, 256> selectedPage{};
    const std::array<std::uint8_t, 12> wrappedRecord = {
        0x82, 0x31, 0x12, 0x44, 0x55, 0x24, 0xd4, 0x77, 0x88, 0x06, 0x2a, 0x66
    };
    for (std::size_t i = 0; i < wrappedRecord.size(); ++i)
        selectedPage[static_cast<std::uint8_t>(0xfa + i)] = wrappedRecord[i];
    MASSResourceSelectionRecord wrapped{};
    const MASSXDataAddress wrappedAddress{0x29, 0xfa};
    assert(MASSResourceSelectionRecordParser::parseSelectedPage(
        selectedPage.data(), selectedPage.size(), wrappedAddress, wrapped));
    assert(wrapped.xdataAddress.page == 0x29 && wrapped.xdataAddress.offset == 0xfa);
    assert(wrapped.bytes[0] == 0x82 && wrapped.bytes[6] == 0xd4 &&
           wrapped.bytes[9] == 0x06 && wrapped.bytes[11] == 0x66);
    assert(!MASSResourceSelectionRecordParser::parseSelectedPage(
        selectedPage.data(), selectedPage.size() - 1, wrappedAddress, wrapped));

    assert(!MASSResourceSelectionRecordParser::parse(
        page.data(), page.size(), 9, parsed));
    assert(!MASSResourceSelectionRecordParser::parse(
        nullptr, page.size(), 0, parsed));
}

void testCommandRegisterFieldDecoders() {
    MASSResourceCommand command{};
    command.opcode = 0x0d;
    command.kind = MASSResourceCommandKind::pitchParameter;
    command.length = 7;
    command.bytes = {{0x0d, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76}};

    MASSPitchParameterRegisterFields pitch{};
    assert(MASSResourceCommandFieldDecoder::decodePitchParameter(command, pitch));
    assert(pitch.opcode == 0x0d && pitch.r2 == 0x21 && pitch.r3 == 0x32);
    assert(pitch.r4 == 0x43 && pitch.r7 == 0x54 &&
           pitch.r6 == 0x65 && pitch.r5 == 0x76);

    command.opcode = 0x12;
    command.kind = MASSResourceCommandKind::multiParameter;
    command.bytes = {{0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78}};
    MASSMultiParameterRegisterFields multi{};
    assert(MASSResourceCommandFieldDecoder::decodeMultiParameter(command, multi));
    assert(multi.opcode == 0x12 && multi.r3 == 0x23 && multi.r5 == 0x34);
    assert(multi.r6 == 0x45 && multi.f0 == 0x56 &&
           multi.r4 == 0x67 && multi.r7 == 0x78);
    assert(!multi.takesFillerPath());

    command.bytes[1] = 0;
    command.kind = MASSResourceCommandKind::emptyMultiParameter;
    assert(MASSResourceCommandFieldDecoder::decodeMultiParameter(command, multi));
    assert(multi.takesFillerPath());

    command.opcode = 0x22;
    assert(!MASSResourceCommandFieldDecoder::decodeMultiParameter(command, multi));
}

void testMultiParameterPacketSequenceBuilder() {
    MASSResourceCommand command{};
    command.opcode = 0x15;
    command.kind = MASSResourceCommandKind::multiParameter;
    command.length = 7;
    command.bytes = {{0x15, 0x01, 0x12, 0x23, 0x34, 0x45, 0x56}};

    MASSResolvedMultiParameterRegisters registers{};
    registers.ram2B = 0x40;
    registers.r2 = 0x81;
    registers.r3 = 0x92;
    registers.r4 = 0x03;
    registers.r5 = 0xa4;
    registers.r6 = 0xb5;
    registers.r7 = 0xc6;

    MASSHardwareCommandPacketSequence sequence{};
    const std::array<std::uint8_t, 3> buffered{{0xd7, 0xe8, 0xf9}};
    assert(MASSResourceHardwareCommandBuilder::buildMultiParameterPackets(
        command, registers, false, buffered, sequence));
    assert(sequence.count == 2);
    assert((sequence.packets[0].bytes ==
            std::array<std::uint8_t, 4>{{0x40, 0xa4, 0xb5, 0xc6}}));
    assert((sequence.packets[1].bytes ==
            std::array<std::uint8_t, 4>{{0x3f, 0x81, 0x92, 0x03}}));

    assert(MASSResourceHardwareCommandBuilder::buildMultiParameterPackets(
        command, registers, true, buffered, sequence));
    assert(sequence.count == 3);
    assert((sequence.packets[0].bytes ==
            std::array<std::uint8_t, 4>{{0x3e, 0xd7, 0xe8, 0xf9}}));
    assert((sequence.packets[1].bytes ==
            std::array<std::uint8_t, 4>{{0x40, 0xa4, 0xb5, 0xc6}}));
    assert((sequence.packets[2].bytes ==
            std::array<std::uint8_t, 4>{{0x3f, 0x81, 0x92, 0x03}}));

    command.bytes[1] = 0;
    assert(!MASSResourceHardwareCommandBuilder::buildMultiParameterPackets(
        command, registers, false, buffered, sequence));
}

void testConditionalExtendedPacketBuilder() {
    MASSResourceCommand command{};
    command.opcode = 0x00;
    command.kind = MASSResourceCommandKind::conditionalRecord;
    command.length = 7;
    command.branchesToExtendedCommand = true;
    command.bytes = {{0x00, 0x11, 0x22, 0x08, 0xa1, 0xb2, 0xfd}};

    MASSHardwareCommandPacket packet{};
    assert(MASSResourceHardwareCommandBuilder::buildConditionalExtendedPacket(
        command, 0x30, false, packet));
    assert((packet.bytes == std::array<std::uint8_t, 4>{{0x30, 0xa1, 0xb2, 0x05}}));
    assert(!MASSResourceHardwareCommandBuilder::buildConditionalExtendedPacket(
        command, 0x30, true, packet));
    command.branchesToExtendedCommand = false;
    assert(!MASSResourceHardwareCommandBuilder::buildConditionalExtendedPacket(
        command, 0x30, false, packet));
}

void testFourByteParameterPacketBuilder() {
    MASSResourceCommand command{};
    command.opcode = 0x1b;
    command.kind = MASSResourceCommandKind::fourByteCommand;
    command.length = 4;
    command.bytes = {{0x1b, 0xa1, 0xb2, 0xfd}};

    MASSHardwareCommandPacket packet{};
    assert(MASSResourceHardwareCommandBuilder::buildFourByteParameterPacket(
        command, 0x30, false, packet));
    assert((packet.bytes == std::array<std::uint8_t, 4>{{0x30, 0xa1, 0xb2, 0x05}}));
    assert(!MASSResourceHardwareCommandBuilder::buildFourByteParameterPacket(
        command, 0x30, true, packet));

    command.opcode = 0x22;
    assert(!MASSResourceHardwareCommandBuilder::buildFourByteParameterPacket(
        command, 0x30, false, packet));
}

void testTerminalHardwarePacketBuilder() {
    MASSResourceCommand command{};
    command.opcode = 0x22; // (opcode & 0x38) == 0x20 terminal group
    command.kind = MASSResourceCommandKind::threePayloadCommand;
    command.length = 4;
    command.bytes = {{0x22, 0x11, 0x22, 0xfd}};

    MASSHardwareCommandPacket packet{};
    assert(MASSResourceHardwareCommandBuilder::buildTerminalPacket(
        command, 0x20, 0x91, 0x72, packet));
    assert((packet.bytes == std::array<std::uint8_t, 4>{{0x2f, 0x91, 0x72, 0x05}}));

    command.opcode = 0x18;
    assert(!MASSResourceHardwareCommandBuilder::buildTerminalPacket(
        command, 0x20, 0x91, 0x72, packet));
    command.opcode = 0x22;
    command.length = 3;
    assert(!MASSResourceHardwareCommandBuilder::buildTerminalPacket(
        command, 0x20, 0x91, 0x72, packet));
}

void testDescriptorParser() {
    std::vector<std::uint8_t> bank(0x10000, 0xff);
    constexpr std::uint16_t base = 0x0200;
    for (std::size_t i = 0; i < 8; ++i)
        bank[base + i] = static_cast<std::uint8_t>(0xa0 + i);

    std::size_t p = base + 8;
    bank[p++] = 0x00; // Table A group 0
    bank[p++] = 0x01; bank[p++] = 0xaa; bank[p++] = 0xbb;
    bank[p++] = 0x7f; bank[p++] = 0xcc; bank[p++] = 0xdd; // group terminator
    bank[p++] = 0x05; // Table A terminator
    bank[p++] = 0x00; bank[p++] = 0x11; bank[p++] = 0x22;
    bank[p++] = 0x05; bank[p++] = 0x33; bank[p++] = 0x44;
    bank[p++] = 0x06; // Table B terminator
    bank[p++] = 0x00; bank[p++] = 0x12; // little-endian stream pointer 0x1200

    MASSResourceDescriptor descriptor{};
    assert(MASSResourceDescriptorParser::parse(
        bank.data(), bank.size(), base, descriptor));
    assert(descriptor.descriptorAddress == base);
    assert(descriptor.fixedHeader[0] == 0xa0 && descriptor.fixedHeader[7] == 0xa7);
    assert(descriptor.tableA.size() == 1);
    assert(descriptor.tableA[0].groupId == 0);
    assert(descriptor.tableA[0].entries.size() == 2);
    assert(descriptor.tableA[0].entries[0].selector == 0x01);
    assert(descriptor.tableA[0].entries[0].data0 == 0xaa);
    assert(descriptor.tableA[0].entries[1].selector == 0x7f);
    assert(descriptor.tableATerminator == 0x05);
    assert(descriptor.tableB.size() == 2);
    assert(descriptor.tableB[0].key == 0x00);
    assert(descriptor.tableB[1].key == 0x05);
    assert(descriptor.tableBTerminator == 0x06);
    assert(descriptor.commandStreamPointer == 0x1200);
}

void testCommandStreamDecoder() {
    std::vector<std::uint8_t> bank(0x10000, 0xff);
    bank[0x1200] = 0x18;
    bank[0x1201] = 0xaa;
    bank[0x1202] = 0xbb;
    bank[0x1203] = 0xcc;
    bank[0x1204] = 0x08;
    bank[0x1205] = 0x01;
    bank[0x1206] = 0x02;
    bank[0x1207] = 0x03;
    bank[0x1208] = 0x04;
    bank[0x1209] = 0x05;
    bank[0x120a] = 0x06;
    bank[0x120b] = 0xff;

    MASSDecodedResourceStream decoded{};
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0x1200, decoded));
    assert(decoded.terminated && !decoded.truncated);
    assert(decoded.slotsConsumed == 2 && decoded.commands.size() == 2);
    assert(decoded.commands[0].kind == MASSResourceCommandKind::fourByteCommand);
    assert(decoded.commands[0].length == 4 && decoded.commands[0].bytes[3] == 0xcc);
    assert(decoded.commands[1].kind == MASSResourceCommandKind::pitchParameter);
    assert(decoded.commands[1].length == 7 && decoded.commands[1].bytes[6] == 0x06);
    assert(decoded.bytesConsumed == 11);

    bank[0x1300] = 0x10;
    bank[0x1301] = 0x00;
    bank[0x1302] = 0x11;
    bank[0x1303] = 0x22;
    bank[0x1304] = 0x33;
    bank[0x1305] = 0x44;
    bank[0x1306] = 0x55;
    bank[0x1307] = 0xff;
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0x1300, decoded));
    assert(decoded.terminated && decoded.slotsConsumed == 1);
    assert(decoded.commands[0].kind == MASSResourceCommandKind::emptyMultiParameter);
    assert(decoded.commands[0].length == 7 && decoded.commands[0].bytes[6] == 0x55);

    // Group 0x00 has two paths: byte +3 bit 3 chooses a seven-byte
    // hardware-routed path unless firmware flag 0x0A is set.
    bank[0x1400] = 0x00;
    bank[0x1401] = 0x11;
    bank[0x1402] = 0x22;
    bank[0x1403] = 0x08;
    bank[0x1404] = 0x31;
    bank[0x1405] = 0x42;
    bank[0x1406] = 0x53;
    bank[0x1407] = 0xff;
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0x1400, decoded));
    assert(decoded.terminated && decoded.slotsConsumed == 1);
    assert(decoded.commands[0].kind == MASSResourceCommandKind::conditionalRecord);
    assert(decoded.commands[0].length == 7 && decoded.commands[0].branchesToExtendedCommand);

    bank[0x1450] = 0x00;
    bank[0x1451] = 0x11;
    bank[0x1452] = 0x22;
    bank[0x1453] = 0x08;
    bank[0x1454] = 0xff;
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0x1450, decoded, true));
    assert(decoded.terminated && decoded.slotsConsumed == 1);
    assert(decoded.commands[0].length == 4 && !decoded.commands[0].branchesToExtendedCommand);

    // Without flag 0x0A, a clear bit 3 in byte +3 takes the short four-byte
    // branch and does not enter handler 0x224C.
    bank[0x1480] = 0x00;
    bank[0x1481] = 0x11;
    bank[0x1482] = 0x22;
    bank[0x1483] = 0x00;
    bank[0x1484] = 0xff;
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0x1480, decoded));
    assert(decoded.terminated && decoded.commands[0].length == 4);
    assert(!decoded.commands[0].branchesToExtendedCommand);

    bank[0x1500] = 0x3b; // 0x38 filler/slot marker, not the end-of-stream byte
    bank[0x1501] = 0x28; // group 0x28 advances by one byte
    bank[0x1502] = 0x30; // group 0x30 advances by one byte
    bank[0x1503] = 0xff;
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0x1500, decoded));
    assert(decoded.terminated && decoded.slotsConsumed == 3);
    assert(decoded.commands[0].kind == MASSResourceCommandKind::marker);
    assert(decoded.commands[1].kind == MASSResourceCommandKind::singleByteAdvance);
    assert(decoded.commands[2].kind == MASSResourceCommandKind::singleByteAdvance);

    for (std::size_t i = 0; i < MASSCommandStreamDecoder::kMaximumSlots; ++i)
        bank[0x1600 + i] = 0x28;
    bank[0x1610] = 0xff;
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0x1600, decoded));
    assert(!decoded.terminated && decoded.slotLimitReached);
    assert(decoded.slotsConsumed == MASSCommandStreamDecoder::kMaximumSlots);

    // Group 0x20 is an interpreter-exit command: data after its four bytes
    // must not be consumed even if it resembles another valid command.
    bank[0x1700] = 0x20;
    bank[0x1701] = 0x11;
    bank[0x1702] = 0x22;
    bank[0x1703] = 0x03;
    bank[0x1704] = 0x28;
    bank[0x1705] = 0xff;
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0x1700, decoded));
    assert(!decoded.terminated && decoded.endedByControlCommand);
    assert(decoded.fillsRemainingSlots);
    assert(decoded.slotsConsumed == 1 && decoded.commands.size() == 1);
    assert(decoded.outputSlotsWritten == 16 && decoded.fillerSlotsWritten == 15);
    assert(decoded.bytesConsumed == 4 && decoded.endOffset == 0x1704);

    bank[0x1710] = 0x20;
    bank[0x1711] = 0x11;
    bank[0x1712] = 0x22;
    bank[0x1713] = 0x03;
    bank[0x1714] = 0xff;
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0x1710, decoded, true));
    assert(!decoded.terminated && decoded.endedByControlCommand);
    assert(!decoded.fillsRemainingSlots && decoded.bytesConsumed == 4);
    assert(decoded.outputSlotsWritten == 1 && decoded.fillerSlotsWritten == 0);

    bank[0xfffc] = 0x08;
    bank[0xfffd] = 0x01;
    assert(MASSCommandStreamDecoder::decode(
        bank.data(), bank.size(), 0xfffc, decoded));
    assert(decoded.truncated && !decoded.terminated);
    assert(decoded.commands.empty());
}

void testActualBulkBlockLoader(const char* path) {
    std::ifstream file(path, std::ios::binary);
    assert(file.good());
    std::vector<std::uint8_t> image(
        (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    assert(image.size() == MASSResourceROM::kImageSize);

    // IC4 0xAF1A indexes the four-byte transfer record at DPTR = 2*R7.
    // Startup calls it with R7=0x48. The two IC51 banks contain distinct
    // source/length records for that same firmware index.
    MASSIC51BulkBlockHeader bank0{};
    MASSIC51BulkBlockHeader bank1{};
    assert(MASSIC51BulkBlockHeader::parse(
        image.data(), MASSResourceROM::kBankSize,
        MASSIC51BulkBlockHeader::kObservedStartupTransferIndex, bank0));
    assert(MASSIC51BulkBlockHeader::parse(
        image.data() + MASSResourceROM::kBankSize,
        MASSResourceROM::kBankSize,
        MASSIC51BulkBlockHeader::kObservedStartupTransferIndex, bank1));

    assert(bank0.sourceAddress == 0x3c64);
    assert(bank0.countHigh == 0x35 && bank0.countLow == 0x4f);
    assert(bank0.firmwareByteCount() == 0x354f);
    assert(bank1.sourceAddress == 0x295c);
    assert(bank1.countHigh == 0x25 && bank1.countLow == 0x74);
    assert(bank1.firmwareByteCount() == 0x2574);

    std::vector<std::uint8_t> payload0(bank0.firmwareByteCount());
    std::vector<std::uint8_t> payload1(bank1.firmwareByteCount());
    assert(MASSIC51BulkBlockHeader::copyPayload(
        image.data(), MASSResourceROM::kBankSize, bank0,
        payload0.data(), payload0.size()));
    assert(MASSIC51BulkBlockHeader::copyPayload(
        image.data() + MASSResourceROM::kBankSize, MASSResourceROM::kBankSize,
        bank1, payload1.data(), payload1.size()));
    assert(payload0.size() == 13647u && payload1.size() == 9588u);
    assert(payload0.front() == 0x00 && payload1.front() == 0x18);

    // Exercise the page-boundary behavior with a synthetic map. The real
    // IC4 table is checked byte-for-byte in test_technox_8032_machine.cpp,
    // which also executes the firmware's actual 13,647-byte transfer.
    std::array<std::uint8_t, 256> addressMap{};
    for (std::size_t i = 0; i < addressMap.size(); ++i)
        addressMap[i] = static_cast<std::uint8_t>(i);
    std::swap(addressMap[0], addressMap[0x7f]);
    std::swap(addressMap[1], addressMap[0x42]);
    MASSIC51BulkBlockHeader syntheticHeader{};
    syntheticHeader.sourceAddress = 0x0200;
    syntheticHeader.countHigh = 0x01;
    syntheticHeader.countLow = 0x01; // 257 bytes: crosses one R0 page boundary.
    std::vector<std::uint8_t> syntheticBank(MASSResourceROM::kBankSize);
    for (std::size_t i = 0; i < syntheticBank.size(); ++i)
        syntheticBank[i] = static_cast<std::uint8_t>((i * 13u) & 0xffu);
    std::array<std::uint8_t, 512> xdata{};
    xdata.fill(0xee);
    std::uint16_t lastPhysicalAddress = 0;
    assert(MASSIC51BulkBlockHeader::copyPayloadToPagedXdata(
        syntheticBank.data(), syntheticBank.size(), syntheticHeader,
        addressMap.data(), xdata.data(), xdata.size(), 0xc000,
        &lastPhysicalAddress));
    assert(xdata[0x7f] == syntheticBank[0x0200]);
    assert(xdata[0x42] == syntheticBank[0x0201]);
    assert(xdata[0x100 + 0x7f] == syntheticBank[0x0300]);
    assert(lastPhysicalAddress == 0xc17f);
    assert(!MASSIC51BulkBlockHeader::copyPayloadToPagedXdata(
        syntheticBank.data(), syntheticBank.size(), syntheticHeader,
        addressMap.data(), xdata.data(), 256));
}

void testBankProgramResolver(const char* path) {
    std::ifstream file(path, std::ios::binary);
    assert(file.good());
    std::vector<std::uint8_t> image(
        (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    assert(image.size() == MASSResourceROM::kImageSize);

    MASSResourceROM rom;
    assert(rom.load(image.data(), image.size()));

    // Build the startup sound-memory image using an identity byte map. The
    // TechnoX-specific 0xADDD permutation is verified against real firmware
    // separately in test_technox_8032_machine.cpp.
    constexpr std::size_t imageFirmwareCodeSize =
        MASSIC51ExternalSoundMemoryImage::kFirmwareMapAddress +
        MASSIC51ExternalSoundMemoryImage::kFirmwareMapSize;
    std::array<std::uint8_t, imageFirmwareCodeSize> imageFirmwareCode{};
    for (std::size_t i = 0; i < MASSIC51ExternalSoundMemoryImage::kFirmwareMapSize; ++i)
        imageFirmwareCode[MASSIC51ExternalSoundMemoryImage::kFirmwareMapAddress + i] =
            static_cast<std::uint8_t>(i);

    MASSIC51ExternalSoundMemoryImage externalImage;
    assert(!externalImage.isLoaded());
    assert(!externalImage.load(rom, nullptr, imageFirmwareCode.size()));
    assert(externalImage.load(
        rom, imageFirmwareCode.data(), imageFirmwareCode.size()));
    assert(externalImage.isLoaded());
    assert(externalImage.baseAddress() == 0xc000u);
    assert(externalImage.endAddress() == 0xffffu);
    assert(externalImage.size() == 0x4000u);
    assert(externalImage.sourceBank() == MASSResourceBank::bank0);
    assert(externalImage.sourceHeader().sourceAddress == 0x3c64u);
    assert(externalImage.sourceHeader().firmwareByteCount() == 13647u);
    assert(externalImage.loadedByteCount() == 13647u);
    assert(externalImage.lastPhysicalAddress() == 0xf54eu);
    assert(externalImage.byteAt(0xbfffu) == 0xffu);
    for (std::size_t i = 0; i < externalImage.loadedByteCount(); ++i) {
        const auto expected = rom.dataForBank(MASSResourceBank::bank0)[
            static_cast<std::uint16_t>(0x3c64u + i)];
        const auto physicalOffset = (i & ~std::size_t{0xffu}) + (i & 0xffu);
        assert(externalImage.data()[physicalOffset] == expected);
    }
    assert(externalImage.data()[0x3600u - 1u] == 0u);

    std::set<std::pair<int, int>> resolvedPairs;
    std::set<std::pair<int, std::uint16_t>> resolvedDescriptors;
    for (int bankNumber = 0; bankNumber < 4; ++bankNumber) {
        for (int soundNumber = 0; soundNumber < 128; ++soundNumber) {
            MASSResolvedProgramResource resolved{};
            assert(MASSResourceProgramResolver::resolve(
                rom, bankNumber, soundNumber, resolved));
            assert(resolved.bankNumber == bankNumber);
            assert(resolved.soundNumber == soundNumber);
            const int expectedBank = bankNumber < 2 ? 0 : 1;
            const int expectedIndex = soundNumber + ((bankNumber & 1) ? 0x80 : 0);
            assert(static_cast<int>(resolved.selection.bank) == expectedBank);
            assert(resolved.selection.pointerIndex == expectedIndex);
            assert(resolvedDescriptors.insert({expectedBank, resolved.descriptorAddress}).second);
            assert(!resolved.stream.truncated);
            assert(resolved.stream.terminated || resolved.stream.endedByControlCommand ||
                   resolved.stream.slotLimitReached);
            assert(resolvedPairs.insert({bankNumber, soundNumber}).second);
        }
    }
    assert(resolvedPairs.size() == 512);
    assert(resolvedDescriptors.size() == 512);

    MASSResourceProgramCatalog catalog;
    assert(!catalog.isLoaded());
    assert(catalog.find(2, 10) == nullptr);
    assert(catalog.loadBothFirmwareFlagVariants(rom));
    assert(catalog.isLoaded());
    assert(catalog.isVariantLoaded(false));
    assert(catalog.isVariantLoaded(true));
    assert(catalog.find(-1, 0) == nullptr);
    assert(catalog.find(4, 0) == nullptr);
    assert(catalog.find(0, 128) == nullptr);
    assert(catalog.find(0, 0, true) != nullptr);
    assert(catalog.find(0, 0, true)->firmwareFlag0ASet);
    for (int bank = 0; bank < 4; ++bank) {
        for (int sound = 0; sound < 128; ++sound) {
            const auto* cached = catalog.find(bank, sound);
            const auto* alternate = catalog.find(bank, sound, true);
            assert(cached != nullptr && alternate != nullptr);
            assert(cached->bankNumber == bank);
            assert(cached->soundNumber == sound);
            assert(!cached->firmwareFlag0ASet);
            assert(alternate->firmwareFlag0ASet);
            assert(cached->descriptorAddress == alternate->descriptorAddress);
            assert(cached->descriptor.commandStreamPointer ==
                   alternate->descriptor.commandStreamPointer);
            assert(!cached->stream.truncated);
            assert(!alternate->stream.truncated);
            assert(cached->stream.outputSlotsWritten <= MASSCommandStreamDecoder::kMaximumSlots);
            assert(alternate->stream.outputSlotsWritten <= MASSCommandStreamDecoder::kMaximumSlots);
        }
    }
    const auto* performanceOnePart13 = catalog.find(2, 10);
    assert(performanceOnePart13 != nullptr);
    assert(performanceOnePart13->bankNumber == 2);
    assert(performanceOnePart13->soundNumber == 10);
    assert(performanceOnePart13->descriptorAddress != 0);
    assert(performanceOnePart13->stream.commands.size() > 0);
    const auto* performanceOnePart13FlagSet = catalog.find(2, 10, true);
    assert(performanceOnePart13FlagSet != nullptr);
    assert(performanceOnePart13FlagSet->firmwareFlag0ASet);
    assert(performanceOnePart13FlagSet->descriptorAddress ==
           performanceOnePart13->descriptorAddress);
    assert(performanceOnePart13FlagSet->stream.commands.size() > 0);

    // The same immutable, predecoded catalog is handed to the voice backend.
    // A note captures its Part's bank/program pair, and the backend can resolve
    // the exact firmware command stream without parsing ROM data on the audio thread.
    ResourceCatalogProbeBackend probeBackend;
    MASSAudioEngine resourceAwareEngine(&probeBackend);
    resourceAwareEngine.setProgramCatalog(&catalog);
    resourceAwareEngine.setExternalSoundMemoryImage(&externalImage);
    assert(resourceAwareEngine.programCatalog() == &catalog);
    assert(resourceAwareEngine.externalSoundMemoryImage() == &externalImage);
    assert(probeBackend.receivedExternalMemoryImage == &externalImage);
    assert(resourceAwareEngine.setPartParameter(12, PartParameter::bankNumber, 2));
    assert(resourceAwareEngine.setPartParameter(12, PartParameter::soundNumber, 10));
    resourceAwareEngine.noteOn(12, 1, 60, 100);
    assert(probeBackend.resolvedOnNote);
    assert(probeBackend.descriptorAddress == performanceOnePart13->descriptorAddress);
    assert(probeBackend.commandCount == performanceOnePart13->stream.commands.size());

    // Replacing the backend while stopped must preserve the same catalog handoff.
    ResourceCatalogProbeBackend replacementBackend;
    resourceAwareEngine.setBackend(&replacementBackend);
    assert(replacementBackend.receivedExternalMemoryImage == &externalImage);
    resourceAwareEngine.noteOn(12, 1, 61, 100);
    assert(replacementBackend.resolvedOnNote);
    assert(replacementBackend.descriptorAddress == performanceOnePart13->descriptorAddress);

    // A later Part program change must resolve a different catalog entry.
    const auto* performanceOnePart14 = catalog.find(1, 93);
    assert(performanceOnePart14 != nullptr);
    assert(resourceAwareEngine.setPartParameter(12, PartParameter::bankNumber, 1));
    assert(resourceAwareEngine.setPartParameter(12, PartParameter::soundNumber, 93));
    resourceAwareEngine.noteOn(12, 1, 62, 100);
    assert(replacementBackend.resolvedOnNote);
    assert(replacementBackend.descriptorAddress == performanceOnePart14->descriptorAddress);
    assert(replacementBackend.commandCount == performanceOnePart14->stream.commands.size());

    // Performance 1 in TECHNOX.MID uses Part 13 = bank 2 / sound 10 and
    // Part 14 = bank 1 / sound 93. Keep their real IC51 descriptors and
    // command streams visible as the first concrete firmware playback cases.
    for (const auto request : {std::pair<int, int>{2, 10}, {1, 93}}) {
        MASSResolvedProgramResource resolved{};
        assert(MASSResourceProgramResolver::resolve(
            rom, request.first, request.second, resolved));
        std::array<std::size_t, 8> kindCounts{};
        for (const auto& command : resolved.stream.commands)
            ++kindCounts[static_cast<std::size_t>(command.kind)];
        std::printf("Performance 1 resource bank=%d sound=%d pointerBank=%u "
                    "pointerIndex=%02X P3=%02X descriptor=%04X stream=%04X "
                    "bytes=%zu commands=%zu slots=%u output=%u filler=%u "
                    "kindCounts=%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu\\n",
                    request.first, request.second,
                    static_cast<unsigned>(resolved.selection.bank),
                    static_cast<unsigned>(resolved.selection.pointerIndex),
                    static_cast<unsigned>(resolved.selection.p3BankMask),
                    static_cast<unsigned>(resolved.descriptorAddress),
                    static_cast<unsigned>(resolved.descriptor.commandStreamPointer),
                    resolved.stream.bytesConsumed, resolved.stream.commands.size(),
                    static_cast<unsigned>(resolved.stream.slotsConsumed),
                    static_cast<unsigned>(resolved.stream.outputSlotsWritten),
                    static_cast<unsigned>(resolved.stream.fillerSlotsWritten),
                    kindCounts[0], kindCounts[1], kindCounts[2], kindCounts[3],
                    kindCounts[4], kindCounts[5], kindCounts[6], kindCounts[7]);
        const auto previewCount = std::min<std::size_t>(8, resolved.stream.commands.size());
        for (std::size_t i = 0; i < previewCount; ++i) {
            const auto& command = resolved.stream.commands[i];
            std::printf("  PERF1-CMD[%zu] offset=%04X opcode=%02X kind=%u len=%u "
                        "bytes=%02X %02X %02X %02X %02X %02X %02X %02X\\n",
                        i, static_cast<unsigned>(command.bankOffset),
                        static_cast<unsigned>(command.opcode),
                        static_cast<unsigned>(command.kind),
                        static_cast<unsigned>(command.length),
                        static_cast<unsigned>(command.bytes[0]),
                        static_cast<unsigned>(command.bytes[1]),
                        static_cast<unsigned>(command.bytes[2]),
                        static_cast<unsigned>(command.bytes[3]),
                        static_cast<unsigned>(command.bytes[4]),
                        static_cast<unsigned>(command.bytes[5]),
                        static_cast<unsigned>(command.bytes[6]),
                        static_cast<unsigned>(command.bytes[7]));
        }
    }

    MASSResolvedProgramResource invalid{};
    assert(!MASSResourceProgramResolver::resolve(rom, -1, 0, invalid));
    assert(!MASSResourceProgramResolver::resolve(rom, 4, 0, invalid));
    assert(!MASSResourceProgramResolver::resolve(rom, 0, -1, invalid));
    assert(!MASSResourceProgramResolver::resolve(rom, 0, 128, invalid));
}

void testActualIC51(const char* path) {
    std::ifstream file(path, std::ios::binary);
    assert(file.good());
    std::vector<std::uint8_t> image(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    assert(image.size() == MASSResourceROM::kImageSize);

    MASSResourceROM rom;
    assert(rom.load(image.data(), image.size()));

    std::uint16_t address = 0;
    assert(rom.readResourceDescriptorAddress(MASSResourceBank::bank0, 0, address));
    assert(address == 0x24d4);
    assert(rom.readResourceDescriptorAddress(MASSResourceBank::bank1, 0, address));
    assert(address == 0x0f5b);

    const std::array<std::array<std::array<std::size_t, 3>, 2>, 2> expectedEndStates{{
        {{{159, 46, 51}, {165, 38, 53}}}, // bank 0: flag clear, then set
        {{{124, 55, 77}, {135, 60, 61}}}  // bank 1: flag clear, then set
    }};
    // Counts of real IC51 group-0x10 commands with nonzero byte +1,
    // bucketed by (opcode & 0x41): 0x00, 0x01, 0x40, 0x41.
    // These prove that all three 0x23C7 paths occur in factory resources.
    const std::array<std::array<std::size_t, 4>, 2> expectedModulationBranches{{
        {{55, 48, 72, 83}}, // flag 0x0A clear
        {{75, 42, 66, 82}}  // flag 0x0A set
    }};
    std::array<std::array<std::size_t, 4>, 2> modulationBranchCounts{};

    for (const auto bank : {MASSResourceBank::bank0, MASSResourceBank::bank1}) {
        std::set<std::uint16_t> pointers;
        const auto bankNumber = bank == MASSResourceBank::bank0 ? 0u : 1u;
        const auto* bankData = image.data() + bankNumber * MASSResourceROM::kBankSize;
        std::array<std::array<std::size_t, 3>, 2> endStates{};

        for (std::size_t i = 0; i < MASSResourceROM::kPointerCount; ++i) {
            assert(rom.readResourceDescriptorAddress(
                bank, static_cast<std::uint8_t>(i), address));
            assert(pointers.insert(address).second);

            MASSResourceDescriptor descriptor{};
            assert(MASSResourceDescriptorParser::parse(
                bankData, MASSResourceROM::kBankSize, address, descriptor));
            assert(descriptor.commandStreamPointer < MASSResourceROM::kBankSize);

            for (std::size_t flagIndex = 0; flagIndex < 2; ++flagIndex) {
                MASSDecodedResourceStream decoded{};
                assert(MASSCommandStreamDecoder::decode(
                    bankData, MASSResourceROM::kBankSize,
                    descriptor.commandStreamPointer, decoded, flagIndex != 0));
                assert(!decoded.truncated);
                for (const auto& command : decoded.commands) {
                    if (command.kind != MASSResourceCommandKind::multiParameter ||
                        command.bytes[1] == 0)
                        continue;
                    const auto mask = static_cast<std::uint8_t>(command.opcode & 0x41u);
                    std::size_t bucket = 0;
                    switch (mask) {
                        case 0x00: bucket = 0; break;
                        case 0x01: bucket = 1; break;
                        case 0x40: bucket = 2; break;
                        case 0x41: bucket = 3; break;
                        default: assert(false && "unexpected 0x23C7 mask");
                    }
                    ++modulationBranchCounts[flagIndex][bucket];
                }
                if (decoded.endedByControlCommand) {
                    ++endStates[flagIndex][0];
                    if (!flagIndex) {
                        assert(decoded.fillsRemainingSlots);
                        assert(decoded.outputSlotsWritten == MASSCommandStreamDecoder::kMaximumSlots);
                    }
                } else if (decoded.terminated) {
                    ++endStates[flagIndex][1];
                } else if (decoded.slotLimitReached) {
                    ++endStates[flagIndex][2];
                } else {
                    assert(false && "resource stream ended without a firmware terminal state");
                }
            }
        }
        assert(pointers.size() == MASSResourceROM::kPointerCount);
        assert(endStates == expectedEndStates[bankNumber]);
    }
    assert(modulationBranchCounts == expectedModulationBranches);
}
}

int main(int argc, char** argv) {
    testPointerTableAndPartMapping();
    testMASS1C61SecondaryTableAddress();
    testMASS1C61AccumulatorTransform();
    testMASS1C61InterpolationLookup();
    testMASS1C61InterpolationArithmetic();
    testMASS1EDDLookupIndexBuilder();
    testMASS1EDDPostLookup();
    testMASS1EDDParameterPipeline();
    testMASS1EDDSlotCoefficientStage();
    testMASS1EDDFullParameterPipeline();
    testMASS1890Arithmetic();
    testMASS2381AddressBuilder();
    testMASSModulationScaleMode41();
    testMASS1848Arithmetic();
    testFirmwareROMTableView();
    if (argc > 2)
        testActualFirmwareROMTables(argv[2]);
    testResourceSelectionRecordParser();
    testCommandRegisterFieldDecoders();
    testMultiParameterPacketSequenceBuilder();
    testConditionalExtendedPacketBuilder();
    testFourByteParameterPacketBuilder();
    testTerminalHardwarePacketBuilder();
    testDescriptorParser();
    testCommandStreamDecoder();
    if (argc > 1) {
        testActualIC51(argv[1]);
        testActualBulkBlockLoader(argv[1]);
        testBankProgramResolver(argv[1]);
    }
    return 0;
}
