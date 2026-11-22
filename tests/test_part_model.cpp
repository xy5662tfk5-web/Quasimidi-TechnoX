#include "../Source/TechnoXPartModel.h"
#include <cassert>
#include <cstdint>
#include <vector>
using namespace technox;
int main() {
    std::vector<std::uint8_t> frame(33, 0);
    frame[0]=0xf0; frame[1]=0x3f; frame[2]=0; frame[3]=0x22; frame[4]=0x44;
    frame[5]=5; frame[6]=1; frame[7]=0; frame[32]=0xf7;
    PartEditModel model(frame);
    assert(model.isLoaded() && !model.isDirty() && model.partNumber()==13);
    assert(model.runtimeEventId()==0x0C);
    for (std::uint8_t selector = 1; selector <= 4; ++selector) {
        frame[6] = selector;
        const PartEditModel mapped(frame);
        assert(mapped.partNumber() == 12 + selector);
        assert(mapped.runtimeEventId() == 11 + selector);
    }
    frame[6] = 1;
    assert(model.setParameterValue(PartParameter::bankNumber, 3));
    assert(!model.setParameterValue(PartParameter::bankNumber, 4));
    assert(model.setDisplayValue(PartParameter::transpose, 0));
    assert(model.rawValue(static_cast<std::size_t>(PartParameter::transpose))==24);
    assert(model.parameterValue(PartParameter::transpose)==0);
    assert(model.isDirty());
    const auto edited=model.editedSysEx();
    assert(edited.size()==33 && edited[0]==0xf0 && edited[7]==0 && edited[32]==0xf7);
    model.revert();
    assert(!model.isDirty() && model.editedSysEx()==frame);
    frame[7]=0x80;
    assert(!PartEditModel(frame).isLoaded());
    return 0;
}