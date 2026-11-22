#include "../Source/MASSAudioEngine.h"
#include "../Source/BasicMASSVoiceBackend.h"
#include <cassert>
#include <cmath>
using namespace technox;
int main() {
    MASSAudioEngine engine;
    engine.prepare(44100.0, 512);
    assert(engine.setPartParameter(12, PartParameter::cutoffFrequency, 91));
    assert(engine.getPartParameter(12, PartParameter::cutoffFrequency) == 91);
    assert(engine.getCommonControl(MASSCommonControl::performanceMode) == -1);
    assert(engine.setCommonControl(MASSCommonControl::performanceMode, 3));
    assert(engine.getCommonControl(MASSCommonControl::performanceMode) == 3);
    assert(!engine.setCommonControl(MASSCommonControl::performanceMode, 4));
    assert(engine.getCommonControl(MASSCommonControl::fx1Type) == -1);
    assert(engine.setCommonControl(MASSCommonControl::fx1Type, 21));
    assert(engine.getCommonControl(MASSCommonControl::fx1Type) == 21);
    assert(engine.setCommonControl(MASSCommonControl::arpResolution, 3));
    assert(!engine.setCommonControl(MASSCommonControl::arpResolution, 4));
    engine.clearCommonControl(MASSCommonControl::fx1Type);
    assert(engine.getCommonControl(MASSCommonControl::fx1Type) == -1);
    assert(!engine.setPartParameter(16, PartParameter::cutoffFrequency, 91));
    assert(engine.setDrumParameter(9, 60, 0, 48));
    assert(engine.getDrumParameter(9, 60, 0) == 48);
    assert(!engine.setDrumParameter(9, 60, 0, 49));
    assert(!engine.setDrumParameter(16, 60, 0, 1));
    assert(engine.setModWheel(15, 127));
    assert(engine.getModWheel(15) == 127);
    assert(!engine.setModWheel(16, 127));
    assert(engine.setPitchBend(15, 8191) && engine.getPitchBend(15) == 8191);
    assert(!engine.setPitchBend(15, 8192));
    assert(engine.setExpression(15, 64) && engine.getExpression(15) == 64);
    assert(!engine.setExpression(15, 128));
    assert(engine.setRpn(12, 0, 12) && engine.getRpn(12, 0) == 12);
    assert(engine.setRpn(12, 1, 64) && engine.getRpn(12, 1) == 64);
    assert(!engine.setRpn(12, 3, 1));
    assert(engine.setPortamento(12, true) && engine.portamento(12));
    assert(engine.setSoftPedal(12, true) && engine.softPedal(12));
    engine.resetPartControllers(12);
    assert(!engine.portamento(12) && !engine.softPedal(12) && engine.getModWheel(12) == 0);
    assert(engine.getPitchBend(12) == 0 && engine.getExpression(12) == 127);
    assert(engine.getRpn(12, 0) == -1 && engine.getRpn(12, 1) == -1);

    // Sustain is independent per Part even when Parts share the same MIDI channel.
    BasicMASSVoiceBackend sustainBackend;
    MASSAudioEngine sustainEngine(&sustainBackend);
    sustainEngine.prepare(48000.0, 128);
    assert(sustainEngine.setSustainPedal(12, true));
    assert(sustainEngine.sustainPedal(12));
    assert(!sustainEngine.sustainPedal(13));
    sustainEngine.noteOn(12, 1, 60, 100);
    sustainEngine.noteOn(13, 1, 60, 100);
    sustainEngine.noteOff(1, 60);
    bool part12Held = false;
    bool part13Released = false;
    for (const auto& voice : sustainEngine.voiceManager().voices()) {
        if (!voice.active || voice.note != 60) continue;
        if (voice.part == 12)
            part12Held = voice.sustained && !voice.keyDown && !voice.releasing;
        if (voice.part == 13)
            part13Released = voice.releasing && !voice.sustained;
    }
    assert(part12Held && part13Released);
    assert(sustainEngine.setSustainPedal(12, false));
    bool part12Released = false;
    for (const auto& voice : sustainEngine.voiceManager().voices())
        if (voice.active && voice.part == 12 && voice.note == 60)
            part12Released = voice.releasing && !voice.sustained;
    assert(part12Released);
    sustainEngine.resetPartControllers(12);
    assert(!sustainEngine.sustainPedal(12));
    for (int i = 0; i < 21; ++i) engine.noteOn(0, 1, static_cast<std::uint8_t>(48+i), 100);
    assert(engine.voiceManager().activeVoiceCount() == 21);
    engine.noteOff(1, 48);
    bool releasing = false;
    for (const auto& v : engine.voiceManager().voices())
        releasing = releasing || (v.active && v.releasing && v.note == 48);
    assert(releasing);
    const int stolen = engine.voiceManager().noteOn(1, 2, 72, 110);
    assert(stolen >= 0 && engine.voiceManager().activeVoiceCount() == 21);
    engine.allSoundsOff(1);
    assert(engine.voiceManager().activeVoiceCount() == 1);
    auto& matrix = engine.modulationMatrix();
    assert(!matrix.setRoute(16, ModSource::modWheel, ModDestination::cutoff, 0.5f));
    assert(matrix.setRoute(0, ModSource::modWheel, ModDestination::cutoff, 0.5f));
    assert(std::abs(matrix.contribution(ModDestination::cutoff, 0.8f, 0, 0, 0)-0.4f) < 0.0001f);
    matrix.clearRoute(0);
    assert(matrix.contribution(ModDestination::cutoff, 1, 0, 0, 0) == 0.0f);
    // The production processor uses the audible prototype backend; the engine's
    // default null backend remains available for isolated routing/state tests.
    BasicMASSVoiceBackend synth;
    MASSAudioEngine audible(&synth);
    audible.prepare(44100.0, 512);
    audible.setPartParameter(12, PartParameter::level, 127);
    audible.setPartParameter(12, PartParameter::cutoffFrequency, 127);
    audible.setPartParameter(12, PartParameter::egAttack, 0);
    audible.setPartParameter(12, PartParameter::egDecay, 0);
    audible.setPartParameter(12, PartParameter::egRelease, 0);
    for (int part = 12; part < 16; ++part) {
        audible.setPartParameter(part, PartParameter::level, 127);
        audible.setPartParameter(part, PartParameter::cutoffFrequency, 127);
        audible.setPartParameter(part, PartParameter::egAttack, 0);
        audible.setPartParameter(part, PartParameter::egDecay, 0);
        audible.setPartParameter(part, PartParameter::egRelease, 0);
        audible.noteOn(static_cast<std::uint8_t>(part), 1, 69, 127);
    }
    assert(audible.voiceManager().activeVoiceCount() == 4);
    float left[512]{}, right[512]{};
    float* outputs[]={left,right};
    audible.render(outputs,2,512);
    bool audibleSignal = false;
    for (int i=0;i<512;++i) audibleSignal = audibleSignal || std::abs(left[i]) > 0.00001f;
    assert(audibleSignal);
    audible.noteOff(1,69);
    float releaseLeft[4096]{}, releaseRight[4096]{};
    float* releaseOutputs[]={releaseLeft,releaseRight};
    audible.render(releaseOutputs,2,4096);
    assert(audible.voiceManager().activeVoiceCount() == 0);
    return 0;
}