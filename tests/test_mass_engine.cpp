#include "../Source/MASSAudioEngine.h"
#include <cassert>
#include <cmath>
using namespace technox;
int main() {
    MASSAudioEngine engine;
    engine.prepare(44100.0, 512);
    assert(engine.setPartParameter(12, PartParameter::cutoffFrequency, 91));
    assert(engine.getPartParameter(12, PartParameter::cutoffFrequency) == 91);
    assert(engine.getCommonControl(MASSCommonControl::fx1Type) == -1);
    assert(engine.setCommonControl(MASSCommonControl::fx1Type, 21));
    assert(engine.getCommonControl(MASSCommonControl::fx1Type) == 21);
    assert(engine.setCommonControl(MASSCommonControl::arpResolution, 3));
    assert(!engine.setCommonControl(MASSCommonControl::arpResolution, 4));
    engine.clearCommonControl(MASSCommonControl::fx1Type);
    assert(engine.getCommonControl(MASSCommonControl::fx1Type) == -1);
    assert(!engine.setPartParameter(16, PartParameter::cutoffFrequency, 91));
    assert(engine.setModWheel(15, 127));
    assert(engine.getModWheel(15) == 127);
    assert(!engine.setModWheel(16, 127));
    assert(engine.setRpn(12, 0, 12) && engine.getRpn(12, 0) == 12);
    assert(engine.setRpn(12, 1, 64) && engine.getRpn(12, 1) == 64);
    assert(!engine.setRpn(12, 3, 1));
    assert(engine.setPortamento(12, true) && engine.portamento(12));
    assert(engine.setSoftPedal(12, true) && engine.softPedal(12));
    engine.resetPartControllers(12);
    assert(!engine.portamento(12) && !engine.softPedal(12) && engine.getModWheel(12) == 0);
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
    float left[8], right[8]; for (auto& v:left) v=1; for (auto& v:right) v=1;
    float* outputs[]={left,right}; engine.render(outputs,2,8);
    for(int i=0;i<8;++i) assert(left[i]==0 && right[i]==0);
    return 0;
}