#include "asc/RenderCostProbe.h"
#include <cassert>
#include <cstdint>
using namespace asc::render_cost;

int main() {
    StageProbe stages;
    stages.Add(Stage::GpuFrame,120);
    stages.Add(Stage::GpuFrame,80);
    stages.Add(Stage::Cleanup,900);
    stages.Add(Stage::Cleanup,10);
    const auto first=stages.TakeWindow();
    assert(first.stages[static_cast<unsigned>(Stage::GpuFrame)].calls==2);
    assert(first.stages[static_cast<unsigned>(Stage::GpuFrame)].Average()==100);
    assert(first.stages[static_cast<unsigned>(Stage::GpuFrame)].peakUs==120);
    assert(first.stages[static_cast<unsigned>(Stage::Cleanup)].peakUs==900);
    assert(stages.TakeWindow().stages[static_cast<unsigned>(Stage::GpuFrame)].calls==0);
    stages.Add(Stage::StateCapture,55);
    assert(stages.TakeWindow().stages[static_cast<unsigned>(Stage::StateCapture)].Average()==55);

    DrawProbe draws;
    for(int i=0;i<257;++i) {
        const bool sampled=draws.Begin(DrawKind::Primitive);
        assert(sampled==((i+1)%DrawSampleRate==0));
        if(sampled)draws.RecordSample(DrawKind::Primitive,500,true);
    }
    for(int i=0;i<128;++i) {
        const bool sampled=draws.Begin(DrawKind::IndexedPrimitive);
        if(sampled)draws.RecordSample(DrawKind::IndexedPrimitive,8000,false,1000000);
    }
    auto window=draws.TakeWindow();
    const auto& prim=window.kinds[static_cast<unsigned>(DrawKind::Primitive)];
    const auto& indexed=window.kinds[static_cast<unsigned>(DrawKind::IndexedPrimitive)];
    assert(prim.calls==257 && prim.samples==2 && prim.totalTicks==1000);
    assert(prim.peakTicks==500 && prim.failures==0);
    assert(indexed.calls==128 && indexed.samples==1 && indexed.totalTicks==8000);
    assert(indexed.failures==1 && indexed.peakTicks==8000);
    assert(indexed.slow1ms==1 && indexed.slow5ms==1 && indexed.slow15ms==0);
    window=draws.TakeWindow();
    assert(window.kinds[static_cast<unsigned>(DrawKind::Primitive)].calls==0);
    // The 128-count sample phase restarts with an empty reporting window.
    for(int i=0;i<128;++i) {
        if(draws.Begin(DrawKind::Primitive))
            draws.RecordSample(DrawKind::Primitive,1);
    }
    window=draws.TakeWindow();
    assert(window.kinds[static_cast<unsigned>(DrawKind::Primitive)].calls==128);
    assert(window.kinds[static_cast<unsigned>(DrawKind::Primitive)].samples==1);
    assert(TicksToUs(50000,10000000)==5000);
    assert(TicksToUs(10000000,10000000)==1000000);
    assert(TicksToUs(1,0)==0);
    assert(TicksToUs(0,1000000)==0);
}
