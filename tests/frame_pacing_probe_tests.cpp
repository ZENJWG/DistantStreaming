#include "asc/FramePacingProbe.h"
#include <cassert>
using asc::frame_pacing::Probe;
int main() {
    Probe p;
    p.EndSceneEnter(100);
    p.EndSceneExit(110);
    p.PresentEnter(120);
    p.PresentExit(12120,true);   // 12ms inside Present
    p.DrawingCallback(310);
    p.ProcessCallback(140);
    p.EndSceneEnter(29000);
    p.EndSceneExit(29300);
    p.PresentEnter(33453);       // 33.333ms interval
    p.PresentExit(33463,false);
    auto s=p.TakeWindow();
    assert(s.presentCalls==2 && s.presentFailures==1);
    assert(s.presentInterval.count==1 && s.presentInterval.sumUs==33333);
    assert(s.presentBuckets[2]==1); // 25..42ms
    assert(s.presentCall.count==2 && s.presentCall.sumUs==12010);
    assert(s.outsidePresent.count==1 && s.outsidePresent.sumUs==21333);
    assert(s.endSceneCall.count==2 && s.endSceneCall.sumUs==310);
    assert(s.endSceneToPresent.count==2 && s.endSceneToPresent.sumUs==4163);
    assert(s.drawingCallback.Average()==310 && s.processCallback.Average()==140);
    s=p.TakeWindow();
    assert(s.presentCalls==0 && s.presentInterval.count==0);
    // Window reset retains the cross-window frame interval reference.
    p.EndSceneEnter(45000);p.EndSceneExit(45050);
    p.PresentEnter(50000);p.PresentExit(50100,true);
    s=p.TakeWindow();
    assert(s.presentInterval.count==1 && s.presentInterval.sumUs==16547);
    assert(s.presentBuckets[1]==1);
    assert(s.outsidePresent.count==1 && s.outsidePresent.sumUs==16537);
    p.DeviceReset();
    p.PresentEnter(100000);p.PresentExit(100020,true);
    s=p.TakeWindow();
    assert(s.resets==1 && s.presentInterval.count==0 && s.outsidePresent.count==0);
    // Clock regression should be counted but not recorded as an enormous duration.
    p.PresentEnter(90000);p.PresentExit(90010,true);
    s=p.TakeWindow();
    assert(s.backwardClock>=1 && s.presentInterval.count==0);
    assert(asc::frame_pacing::IntervalBucket(7999)==0);
    assert(asc::frame_pacing::IntervalBucket(8000)==1);
    assert(asc::frame_pacing::IntervalBucket(25000)==2);
    assert(asc::frame_pacing::IntervalBucket(42000)==3);
    assert(asc::frame_pacing::IntervalBucket(58000)==4);
    assert(asc::frame_pacing::IntervalBucket(83000)==5);
}
