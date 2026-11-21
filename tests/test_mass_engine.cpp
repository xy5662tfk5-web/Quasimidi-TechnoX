#include "../Source/MASSAudioEngine.h"
#include <cassert>
#include <cmath>

using namespace technox;

int main() {
    MASSAudioEngine engine;
    engine.prepare(44100.0, 512);
    for (int i = 0; i < 21; ++i)
        engine.noteOn(0, 1, static_cast<std::uint8_t>(48 + i), 100);
    assert(engine.voiceManager().activeVoiceCount() == 21);

    engine.noteOff(1, 48);
    bool hasReleasing = false;
    for (const auto& voice : engine.voiceManager().voices())
        hasReleasing = hasReleasing || (voice.active && voice.releasing && voice.note == 48);
    assert(hasReleasing);

    const int stolen = engine.voiceManager().noteOn(1, 2, 72, 110);
    assert(stolen >= 0);
    assert(engine.voiceManager().activeVoiceCount() == 21);

    auto& matrix = engine.modulationMatrix();
    assert(!matrix.setRoute(16, ModSource::modWheel, ModDestination::cutoff, 0.5f));
    assert(matrix.setRoute(0, ModSource::modWheel, ModDestination::cutoff, 0.5f));
    const float amount = matrix.contribution(ModDestination::cutoff, 0.8f, 0, 0, 0);
    assert(std::abs(amount - 0.4f) < 0.0001f);
    matrix.clearRoute(0);
    assert(matrix.contribution(ModDestination::cutoff, 1, 0, 0, 0) == 0.0f);

    float left[8], right[8];
    for (auto& v : left) v = 1.0f;
    for (auto& v : right) v = 1.0f;
    float* outputs[] = {left, right};
    engine.render(outputs, 2, 8);
    for (int i = 0; i < 8; ++i) assert(left[i] == 0.0f && right[i] == 0.0f);
    return 0;
}
