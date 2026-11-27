#include "../Source/SAM9407VoiceBackend.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>

using namespace technox;

template <std::size_t Size>
static void writeLittleEndian(std::array<std::uint8_t, Size>& bytes,
                              std::size_t word, std::int16_t value) {
    const auto bits = static_cast<std::uint16_t>(value);
    bytes[word * 2] = static_cast<std::uint8_t>(bits & 0xffu);
    bytes[word * 2 + 1] = static_cast<std::uint8_t>(bits >> 8);
}

static double energy(const std::array<float, 8>& left,
                     const std::array<float, 8>& right) {
    double total = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i)
        total += std::abs(left[i]) + std::abs(right[i]);
    return total;
}

int main() {
    std::array<std::uint8_t, 12> pcm{};
    writeLittleEndian(pcm, 0, 0);
    writeLittleEndian(pcm, 1, 16384);
    writeLittleEndian(pcm, 2, -16384);
    writeLittleEndian(pcm, 3, 8192);
    writeLittleEndian(pcm, 4, 0);
    writeLittleEndian(pcm, 5, -8192);

    MASSWaveformMemoryView memory;
    assert(memory.attach(pcm.data(), pcm.size()));

    SAM9407VoiceBackend backend;
    SAM9407SampleMapEntry entry{};
    entry.bank = 0;
    entry.program = 0;
    entry.startWord = 0;
    entry.endWord = 6;
    entry.sampleRate = 44100;
    entry.rootNote = 60;
    entry.gain = 127;
    entry.pan = 64;
    entry.byteOrder = SAM9407SampleByteOrder::littleEndian;
    entry.loopMode = SAM9407LoopMode::oneShot;
    assert(backend.addSampleMapping(entry));
    assert(backend.sampleMappingCount() == 1);
    backend.setWaveformMemoryView(&memory);

    MASSAudioEngine engine(&backend);
    engine.prepare(44100.0, 8);
    assert(engine.setPartParameter(0, PartParameter::level, 127));
    assert(engine.setPartParameter(0, PartParameter::panorama, 0));
    engine.noteOn(0, 1, 60, 127);

    std::array<float, 8> left{}, right{};
    float* channels[] = {left.data(), right.data()};
    engine.render(channels, 2, 6);
    assert(energy(left, right) > 0.2);
    assert(std::isfinite(left[1]) && std::isfinite(right[1]));
    // The source's second 16-bit word is positive; its third is negative.
    assert(left[1] > 0.05f && right[1] > 0.05f);
    assert(left[2] < -0.05f && right[2] < -0.05f);
    assert(engine.voiceManager().activeVoiceCount() == 1);

    // A Note Off turns a looping voice into its release/tail traversal.
    SAM9407VoiceBackend loopBackend;
    entry.loopMode = SAM9407LoopMode::forward;
    entry.loopStartWord = 1;
    entry.loopEndWord = 4;
    assert(loopBackend.addSampleMapping(entry));
    loopBackend.setWaveformMemoryView(&memory);
    MASSAudioEngine loopEngine(&loopBackend);
    loopEngine.prepare(44100.0, 8);
    assert(loopEngine.setPartParameter(0, PartParameter::level, 127));
    loopEngine.noteOn(0, 1, 60, 127);
    std::array<float, 8> loopLeft{}, loopRight{};
    float* loopChannels[] = {loopLeft.data(), loopRight.data()};
    loopEngine.render(loopChannels, 2, 5);
    loopEngine.noteOff(1, 60);
    loopEngine.render(loopChannels, 2, 1);
    assert(energy(loopLeft, loopRight) > 0.2);

    // Alternating loops keep the fractional sample coordinate consistent
    // through the direction reversal. At frame 3, position is 2.5 words:
    // the interpolated value must be halfway between words 2 and 3, not
    // between words 2 and 1 (the old reverse-neighbour bug).
    std::array<std::uint8_t, 10> alternatingPcm{};
    writeLittleEndian(alternatingPcm, 0, 0);
    writeLittleEndian(alternatingPcm, 1, -10000);
    writeLittleEndian(alternatingPcm, 2, 5000);
    writeLittleEndian(alternatingPcm, 3, 9000);
    writeLittleEndian(alternatingPcm, 4, 12000);
    MASSWaveformMemoryView alternatingMemory;
    assert(alternatingMemory.attach(alternatingPcm.data(), alternatingPcm.size()));
    SAM9407VoiceBackend alternatingBackend;
    SAM9407SampleMapEntry alternatingEntry{};
    alternatingEntry.startWord = 0;
    alternatingEntry.endWord = 5;
    alternatingEntry.sampleRate = 44100;
    alternatingEntry.rootNote = 60;
    alternatingEntry.gain = 127;
    alternatingEntry.pan = 64;
    alternatingEntry.loopMode = SAM9407LoopMode::alternating;
    alternatingEntry.loopStartWord = 1;
    alternatingEntry.loopEndWord = 4;
    assert(alternatingBackend.addSampleMapping(alternatingEntry));
    alternatingBackend.setWaveformMemoryView(&alternatingMemory);
    MASSAudioEngine alternatingEngine(&alternatingBackend);
    alternatingEngine.prepare(29400.0, 8); // 1.5 source words per output frame
    assert(alternatingEngine.setPartParameter(0, PartParameter::level, 127));
    alternatingEngine.noteOn(0, 1, 60, 127);
    std::array<float, 8> alternatingLeft{}, alternatingRight{};
    float* alternatingChannels[] = {
        alternatingLeft.data(), alternatingRight.data()
    };
    alternatingEngine.render(alternatingChannels, 2, 4);
    assert(alternatingLeft[3] > 0.08f && alternatingRight[3] > 0.08f);

    // A high transposition can advance by more than one full alternating
    // loop period in a single output frame. The phase fold must preserve the
    // active voice instead of reflecting once and leaving the sample range.
    SAM9407VoiceBackend fastAlternatingBackend;
    assert(fastAlternatingBackend.addSampleMapping(alternatingEntry));
    fastAlternatingBackend.setWaveformMemoryView(&alternatingMemory);
    MASSAudioEngine fastAlternatingEngine(&fastAlternatingBackend);
    fastAlternatingEngine.prepare(44100.0, 8);
    assert(fastAlternatingEngine.setPartParameter(0, PartParameter::level, 127));
    fastAlternatingEngine.noteOn(0, 1, 100, 127); // 10x+ source-rate step
    std::array<float, 8> fastLeft{}, fastRight{};
    float* fastChannels[] = {fastLeft.data(), fastRight.data()};
    fastAlternatingEngine.render(fastChannels, 2, 1);
    fastLeft.fill(0.0f);
    fastRight.fill(0.0f);
    fastAlternatingEngine.render(fastChannels, 2, 1);
    assert(fastAlternatingEngine.voiceManager().activeVoiceCount() == 1);
    assert(energy(fastLeft, fastRight) > 0.001);

    // Big-endian samples are explicitly selectable; the renderer never
    // guesses byte order from a raw waveform dump.
    std::array<std::uint8_t, 4> bigEndian{{0x40, 0x00, 0xC0, 0x00}};
    MASSWaveformMemoryView bigEndianMemory;
    assert(bigEndianMemory.attach(bigEndian.data(), bigEndian.size()));
    SAM9407VoiceBackend bigEndianBackend;
    SAM9407SampleMapEntry bigEntry{};
    bigEntry.startWord = 0;
    bigEntry.endWord = 2;
    bigEntry.sampleRate = 44100;
    bigEntry.rootNote = 60;
    bigEntry.gain = 127;
    bigEntry.pan = 64;
    bigEntry.byteOrder = SAM9407SampleByteOrder::bigEndian;
    assert(bigEndianBackend.addSampleMapping(bigEntry));
    bigEndianBackend.setWaveformMemoryView(&bigEndianMemory);
    MASSAudioEngine bigEngine(&bigEndianBackend);
    bigEngine.prepare(44100.0, 8);
    assert(bigEngine.setPartParameter(0, PartParameter::level, 127));
    bigEngine.noteOn(0, 1, 60, 127);
    std::array<float, 8> bigLeft{}, bigRight{};
    float* bigChannels[] = {bigLeft.data(), bigRight.data()};
    bigEngine.render(bigChannels, 2, 2);
    assert(bigLeft[0] > 0.1f && bigRight[0] > 0.1f);
    assert(bigLeft[1] < -0.1f && bigRight[1] < -0.1f);

    // A mono/legato retarget can change the selected program without a
    // second backend noteOn. The active voice must follow the new mapping.
    std::array<std::uint8_t, 8> retargetPcm{{
        0x00, 0x20, 0x00, 0x20, 0x00, 0xe0, 0x00, 0xe0
    }};
    MASSWaveformMemoryView retargetMemory;
    assert(retargetMemory.attach(retargetPcm.data(), retargetPcm.size()));
    SAM9407VoiceBackend retargetBackend;
    SAM9407SampleMapEntry positiveEntry{};
    positiveEntry.bank = 0;
    positiveEntry.program = 0;
    positiveEntry.startWord = 0;
    positiveEntry.endWord = 2;
    positiveEntry.sampleRate = 44100;
    positiveEntry.rootNote = 60;
    positiveEntry.gain = 127;
    positiveEntry.pan = 64;
    assert(retargetBackend.addSampleMapping(positiveEntry));
    SAM9407SampleMapEntry negativeEntry = positiveEntry;
    negativeEntry.program = 1;
    negativeEntry.startWord = 2;
    negativeEntry.endWord = 4;
    assert(retargetBackend.addSampleMapping(negativeEntry));
    retargetBackend.setWaveformMemoryView(&retargetMemory);
    retargetBackend.prepare(44100.0, 8);

    VoiceManager retargetVoices;
    const int retargetIndex = retargetVoices.noteOn(0, 1, 60, 127, -1, 0, 0);
    assert(retargetIndex >= 0);
    retargetBackend.noteOn(retargetIndex,
        retargetVoices.voices()[static_cast<std::size_t>(retargetIndex)]);
    MASSPartParameterStore retargetParts;
    assert(retargetParts.set(0, PartParameter::level, 127));
    MASSModulationSourceStore retargetSources;
    MASSPartRuntimeControlStore retargetControls;
    MASSCommonControlStore retargetCommon;
    MASSPerformanceImageStore retargetPerformance;
    MASSDrumParameterStore retargetDrums;
    ModulationMatrix retargetMatrix;
    std::array<float, 2> retargetLeft{}, retargetRight{};
    float* retargetChannels[] = {retargetLeft.data(), retargetRight.data()};
    retargetBackend.render(retargetChannels, 2, 1, retargetVoices,
        retargetMatrix, retargetParts, retargetSources, retargetControls,
        retargetCommon, retargetPerformance, retargetDrums);
    assert(retargetLeft[0] > 0.05f);

    assert(retargetVoices.retargetVoice(static_cast<std::size_t>(retargetIndex),
        1, 60, 127, 0, 1));
    retargetLeft.fill(0.0f);
    retargetRight.fill(0.0f);
    retargetBackend.render(retargetChannels, 2, 1, retargetVoices,
        retargetMatrix, retargetParts, retargetSources, retargetControls,
        retargetCommon, retargetPerformance, retargetDrums);
    assert(retargetLeft[0] < -0.05f);

    // Resources may be attached after MIDI Note On (for example, after a
    // user loads a waveform image/sample map while the processor is stopped).
    // The active voice must recover instead of remaining permanently silent.
    SAM9407VoiceBackend lateBackend;
    MASSAudioEngine lateEngine(&lateBackend);
    lateEngine.prepare(44100.0, 8);
    assert(lateEngine.setPartParameter(0, PartParameter::level, 127));
    lateEngine.noteOn(0, 1, 60, 127);
    std::array<float, 8> lateLeft{}, lateRight{};
    float* lateChannels[] = {lateLeft.data(), lateRight.data()};
    lateEngine.render(lateChannels, 2, 4);
    assert(energy(lateLeft, lateRight) == 0.0);

    std::array<std::uint8_t, 8> latePcm{{
        0x00, 0x40, 0x00, 0x40, 0x00, 0xc0, 0x00, 0xc0
    }};
    MASSWaveformMemoryView lateMemory;
    assert(lateMemory.attach(latePcm.data(), latePcm.size()));
    SAM9407SampleMapEntry lateEntry{};
    lateEntry.bank = 0;
    lateEntry.program = 0;
    lateEntry.startWord = 0;
    lateEntry.endWord = 4;
    lateEntry.sampleRate = 44100;
    lateEntry.rootNote = 60;
    lateEntry.gain = 127;
    lateEntry.pan = 64;
    assert(lateBackend.addSampleMapping(lateEntry));
    lateEngine.setWaveformMemoryView(&lateMemory);
    lateLeft.fill(0.0f);
    lateRight.fill(0.0f);
    lateEngine.render(lateChannels, 2, 4);
    assert(energy(lateLeft, lateRight) > 0.2);

    // Invalid loops and out-of-range sample descriptors are rejected early.
    SAM9407VoiceBackend validationBackend;
    SAM9407SampleMapEntry invalid = entry;
    invalid.loopStartWord = 5;
    invalid.loopEndWord = 4;
    assert(!validationBackend.addSampleMapping(invalid));
    invalid = entry;
    invalid.endWord = static_cast<std::uint32_t>(
        SAM9407ExternalMemoryBus::kAddressSpaceWordCount + 1u);
    assert(!validationBackend.addSampleMapping(invalid));

    // end_word is exclusive, so the final legal sample word is address
    // 0x01FFFFFF and its exclusive end is 0x02000000.
    SAM9407VoiceBackend maximumAddressBackend;
    SAM9407SampleMapEntry maximumAddressEntry{};
    maximumAddressEntry.startWord =
        SAM9407ExternalMemoryBus::kMaximumWordAddress;
    maximumAddressEntry.endWord = static_cast<std::uint32_t>(
        SAM9407ExternalMemoryBus::kAddressSpaceWordCount);
    assert(maximumAddressBackend.addSampleMapping(maximumAddressEntry));

    return 0;
}
