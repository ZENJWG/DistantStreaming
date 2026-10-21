#include "asc/StreamingFlowProbe.h"
#include <cassert>
using namespace asc::stream_flow;
int main() {
    StateSweep<8> probe;
    State states[4]={State::Requested,State::Loaded,State::NotLoaded,State::Channeled};
    auto source=[&](std::size_t id){return states[id];};
    assert(!probe.Sample(4,2,source));
    assert(probe.Cursor()==2);
    assert(probe.Sample(4,2,source));
    auto first=probe.Completed();
    assert(first.number==1 && first.inspected==4);
    assert(first.requested==1 && first.channeled==1 && first.loaded==1);
    assert(first.observedCompleted==0);
    states[0]=State::Loaded;
    states[1]=State::NotLoaded;
    states[3]=State::NotLoaded;
    assert(probe.Sample(4,4,source));
    auto second=probe.Completed();
    assert(second.number==2);
    assert(second.observedCompleted==1);
    assert(second.observedEvicted==1);
    assert(second.observedCancelled==1);
    assert(!probe.Sample(9,100,source));
    assert(!probe.Sample(4,0,source));
    BacklogMeter meter;
    meter.Add(100,100,9);
    meter.Add(140,110,7);
    meter.Add(180,110,7);
    meter.Add(220,80,3);
    auto b=meter.Take();
    assert(b.samples==4 && b.peak==110 && b.low==80);
    assert(b.increases==1 && b.decreases==1 && b.unchanged==1);
    meter.Add(260,70,1);
    b=meter.Take();
    assert(b.samples==1 && b.decreases==1 && b.low==70);
    StageCounter c;
    c.Add(3);c.Add(7);
    assert(c.Average()==5 && c.peakUs==7);
    return 0;
}
