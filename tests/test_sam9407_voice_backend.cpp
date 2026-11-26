#include "../Source/SAM9407VoiceBackend.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>

using namespace technox;

static void writeLittleEndian(std::array<std::uint8_t, 12>& bytes,
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

    // Invalid loops and out-of-range sample descriptors are rejected early.
    SAM9407VoiceBackend validationBackend;
    SAM9407SampleMapEntry invalid = entry;
    invalid.loopStartWord = 5;
    invalid.loopEndWord = 4;
    assert(!validationBackend.addSampleMapping(invalid));
    invalid = entry;
    invalid.endWord = SAM9407VoiceBackend::kMaximumWordAddress + 1u;
    assert(!validationBackend.addSampleMapping(invalid));

    return 0;
}
