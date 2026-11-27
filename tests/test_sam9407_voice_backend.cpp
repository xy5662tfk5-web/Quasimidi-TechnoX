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

    // Reattaching a new file through the same MASSWaveformMemoryView object
    // must rebuild the mapped word window and rebind the active voice.
    SAM9407VoiceBackend resizeBackend;
    MASSAudioEngine resizeEngine(&resizeBackend);
    resizeEngine.prepare(44100.0, 8);
    assert(resizeEngine.setPartParameter(0, PartParameter::level, 127));
    std::array<std::uint8_t, 4> shortImage{{0x00, 0x40, 0x00, 0x40}};
    std::array<std::uint8_t, 8> expandedImage{{
        0x00, 0x40, 0x00, 0x40, 0x00, 0xc0, 0x00, 0xc0
    }};
    MASSWaveformMemoryView reusableMemory;
    assert(reusableMemory.attach(shortImage.data(), shortImage.size()));
    resizeEngine.setWaveformMemoryView(&reusableMemory);
    SAM9407SampleMapEntry expandedEntry{};
    expandedEntry.bank = 0;
    expandedEntry.program = 0;
    expandedEntry.startWord = 0;
    expandedEntry.endWord = 4;
    expandedEntry.sampleRate = 44100;
    expandedEntry.rootNote = 60;
    expandedEntry.gain = 127;
    expandedEntry.pan = 64;
    assert(resizeBackend.addSampleMapping(expandedEntry));
    resizeEngine.noteOn(0, 1, 60, 127);
    std::array<float, 8> resizeLeft{}, resizeRight{};
    float* resizeChannels[] = {resizeLeft.data(), resizeRight.data()};
    resizeEngine.render(resizeChannels, 2, 4);
    assert(energy(resizeLeft, resizeRight) == 0.0);

    assert(reusableMemory.attach(expandedImage.data(), expandedImage.size()));
    resizeEngine.setWaveformMemoryView(&reusableMemory);
    resizeLeft.fill(0.0f);
    resizeRight.fill(0.0f);
    resizeEngine.render(resizeChannels, 2, 4);
    assert(energy(resizeLeft, resizeRight) > 0.2);
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

    // One program can contain key zones and velocity layers. Select the
    // most specific matching region at Note On rather than a single program-wide sample.
    std::array<std::uint8_t, 12> zonedPcm{};
    writeLittleEndian(zonedPcm, 0, 4096);
    writeLittleEndian(zonedPcm, 1, 4096);
    writeLittleEndian(zonedPcm, 2, -8192);
    writeLittleEndian(zonedPcm, 3, -8192);
    writeLittleEndian(zonedPcm, 4, 16384);
    writeLittleEndian(zonedPcm, 5, 16384);
    MASSWaveformMemoryView zonedMemory;
    assert(zonedMemory.attach(zonedPcm.data(), zonedPcm.size()));

    auto renderZonedFirstSample = [&](int note, int velocity) {
        SAM9407VoiceBackend zonedBackend;
        SAM9407SampleMapEntry fullRange{};
        fullRange.bank = 0;
        fullRange.program = 0; // MASSAudioEngine test Part defaults to program 0.
        fullRange.startWord = 0;
        fullRange.endWord = 2;
        fullRange.sampleRate = 44100;
        fullRange.rootNote = 60;
        fullRange.gain = 127;
        assert(zonedBackend.addSampleMapping(fullRange));

        SAM9407SampleMapEntry keyZone = fullRange;
        keyZone.keyLow = 60;
        keyZone.keyHigh = 72;
        keyZone.startWord = 2;
        keyZone.endWord = 4;
        assert(zonedBackend.addSampleMapping(keyZone));

        SAM9407SampleMapEntry velocityZone = fullRange;
        velocityZone.velocityLow = 100;
        velocityZone.velocityHigh = 127;
        velocityZone.startWord = 4;
        velocityZone.endWord = 6;
        assert(zonedBackend.addSampleMapping(velocityZone));
        assert(zonedBackend.sampleMappingCount() == 3);
        zonedBackend.setWaveformMemoryView(&zonedMemory);

        MASSAudioEngine zonedEngine(&zonedBackend);
        zonedEngine.prepare(44100.0, 8);
        assert(zonedEngine.setPartParameter(0, PartParameter::level, 127));
        zonedEngine.noteOn(0, 1, note, velocity);
        std::array<float, 2> zonedLeft{}, zonedRight{};
        float* zonedChannels[] = {zonedLeft.data(), zonedRight.data()};
        zonedEngine.render(zonedChannels, 2, 1);
        return zonedLeft[0];
    };
    // Key region is more specific than the overlapping high-velocity layer.
    assert(renderZonedFirstSample(64, 120) < -0.05f);
    // Outside that key region, high velocity selects its own layer.
    assert(renderZonedFirstSample(48, 120) > 0.15f);
    // Outside both special regions, the full-range fallback stays active.
    assert(renderZonedFirstSample(48, 80) > 0.03f);

    // Mono/legato retargeting within the same program must reselect a
    // key zone even though the backend does not receive a second noteOn().
    SAM9407VoiceBackend legatoZoneBackend;
    SAM9407SampleMapEntry legatoFull{};
    legatoFull.bank = 0;
    legatoFull.program = 0;
    legatoFull.startWord = 0;
    legatoFull.endWord = 2;
    legatoFull.sampleRate = 44100;
    legatoFull.rootNote = 60;
    legatoFull.gain = 127;
    assert(legatoZoneBackend.addSampleMapping(legatoFull));
    auto legatoKeyZone = legatoFull;
    legatoKeyZone.keyLow = 60;
    legatoKeyZone.keyHigh = 72;
    legatoKeyZone.startWord = 2;
    legatoKeyZone.endWord = 4;
    assert(legatoZoneBackend.addSampleMapping(legatoKeyZone));
    auto legatoVelocityZone = legatoFull;
    legatoVelocityZone.velocityLow = 100;
    legatoVelocityZone.velocityHigh = 127;
    legatoVelocityZone.startWord = 4;
    legatoVelocityZone.endWord = 6;
    assert(legatoZoneBackend.addSampleMapping(legatoVelocityZone));
    legatoZoneBackend.setWaveformMemoryView(&zonedMemory);
    MASSAudioEngine legatoZoneEngine(&legatoZoneBackend);
    legatoZoneEngine.prepare(44100.0, 8);
    assert(legatoZoneEngine.setPartParameter(0, PartParameter::level, 127));
    assert(legatoZoneEngine.setPartParameter(0, PartParameter::trackMode, 2));
    std::array<float, 2> legatoZoneLeft{}, legatoZoneRight{};
    float* legatoZoneChannels[] = {legatoZoneLeft.data(), legatoZoneRight.data()};
    legatoZoneEngine.noteOn(0, 1, 48, 80);
    legatoZoneEngine.render(legatoZoneChannels, 2, 1);
    assert(legatoZoneLeft[0] > 0.03f);
    legatoZoneLeft.fill(0.0f);
    legatoZoneRight.fill(0.0f);
    legatoZoneEngine.noteOn(0, 1, 64, 80);
    legatoZoneEngine.render(legatoZoneChannels, 2, 1);
    assert(legatoZoneLeft[0] < -0.05f);
    legatoZoneLeft.fill(0.0f);
    legatoZoneRight.fill(0.0f);
    legatoZoneEngine.noteOn(0, 1, 48, 120);
    legatoZoneEngine.render(legatoZoneChannels, 2, 1);
    assert(legatoZoneLeft[0] > 0.15f);

    // An identical region is replaced rather than duplicated.
    SAM9407VoiceBackend replacementBackend;
    SAM9407SampleMapEntry replacement{};
    replacement.bank = 3;
    replacement.program = 9;
    replacement.endWord = 2;
    assert(replacementBackend.addSampleMapping(replacement));
    replacement.startWord = 1;
    assert(replacementBackend.addSampleMapping(replacement));
    assert(replacementBackend.sampleMappingCount() == 1);
    replacement.keyLow = 90;
    replacement.keyHigh = 80;
    assert(!replacementBackend.addSampleMapping(replacement));
    replacement.keyLow = 0;
    replacement.keyHigh = 128;
    assert(!replacementBackend.addSampleMapping(replacement));
    replacement.keyHigh = 127;
    replacement.velocityHigh = 128;
    assert(!replacementBackend.addSampleMapping(replacement));

    return 0;
}
