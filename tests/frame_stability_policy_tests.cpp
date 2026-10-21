#include "asc/FrameStabilityPolicy.h"
#include "asc/Core.h"
#include <cassert>
#include <cstdint>
#include <vector>
#include <iostream>

int main() {
    using namespace asc::stability;
    Settings s;
    assert(Valid(s));
    assert(s.enabled && !s.allowBlockingCameraLoads);
    ControllerGate gate;
    assert(gate.Due(100u,s));
    gate.Record(100u,700u,s);
    assert(!gate.Due(200u,s)); // 100 ms < 120 ms regular cadence
    assert(gate.Due(220u,s));
    gate.Record(220u,12000u,s);
    assert(!gate.Due(500u,s)); // slow controller uses 350 ms
    assert(gate.Due(570u,s));
    assert(gate.TakeRuns()==2);
    assert(gate.TakeSlowRuns()==1);
    assert(gate.TakeSkips()==2);
    assert(gate.TakeRuns()==0 && gate.TakeSkips()==0);
    s.enabled=false;
    assert(gate.Due(571u,s)); // old behavior opt-out
    // Wrap-around arithmetic must not accidentally extend a cooldown.
    s.enabled=true;
    ControllerGate wrapped;
    wrapped.Record(UINT32_MAX-50u,2000u,s);
    assert(!wrapped.Due(25u,s));
    assert(wrapped.Due(80u,s));
    auto bad=s;
    bad.maxPredictiveCells=0;
    assert(!Valid(bad));
    bad=s;bad.slowRetryMs=40;
    assert(!Valid(bad));

    // Regression: a bounded controller still preserves the camera/current
    // and future endpoint cells; native world scan is a separate subsystem.
    auto cfg=asc::Config{};
    cfg.prefetch=true;
    cfg.maxCells=96;
    cfg.maxCandidates=384;
    const auto cells=asc::Controller::SelectCells(
        {-120.f,100.f,0.f},{300.f,250.f,0.f},160.f,s.maxPredictiveCells);
    assert(cells.size()<=s.maxPredictiveCells);
    bool startFound=false,endFound=false;
    for(const auto& cell:cells) {
        if(cell.x==57 && cell.y==62)startFound=true;
        if(cell.x==66 && cell.y==65)endFound=true;
    }
    assert(startFound && endFound);
    std::cout<<"PASS: frame stability gate, backoff, wrap, and bounded scan\n";
}
