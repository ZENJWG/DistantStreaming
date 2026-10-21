#pragma once
// Portable, allocation-free frame-pacing observations.
// This is NOT a GPU timer. All values are CPU wall-clock microseconds.
// A caller owns synchronization if called from more than one thread.
#include <algorithm>
#include <array>
#include <cstdint>

namespace asc::frame_pacing {
struct Timing {
    uint64_t count=0, sumUs=0, maxUs=0;
    void Add(uint64_t us) noexcept {
        ++count;
        sumUs=(us>UINT64_MAX-sumUs)?UINT64_MAX:sumUs+us;
        maxUs=std::max(maxUs,us);
    }
    uint64_t Average() const noexcept { return count?sumUs/count:0; }
};
inline unsigned IntervalBucket(uint64_t us) noexcept {
    // <8ms, 8..25ms, 25..42ms, 42..58ms, 58..83ms, >=83ms.
    if(us<8000)return 0;
    if(us<25000)return 1;
    if(us<42000)return 2;
    if(us<58000)return 3;
    if(us<83000)return 4;
    return 5;
}
struct Snapshot {
    Timing presentInterval, presentCall, outsidePresent, endSceneCall,
           endSceneToPresent, drawingCallback, processCallback;
    std::array<uint64_t,6> presentBuckets{};
    uint64_t presentCalls=0, presentFailures=0, endSceneCalls=0,
             resets=0, backwardClock=0;
};
class Probe {
    Snapshot window_{};
    uint64_t previousPresentEntry_=0, previousPresentReturn_=0;
    uint64_t lastEndSceneReturn_=0, currentPresentEntry_=0, currentEndSceneEntry_=0;
    bool hasPresentEntry_=false, hasPresentReturn_=false;
    bool hasEndSceneReturn_=false, activePresent_=false, activeEndScene_=false;
    static void AddDelta(Timing& timing,uint64_t from,uint64_t to,
                         uint64_t& backward) noexcept {
        if(to>=from)timing.Add(to-from);
        else ++backward;
    }
public:
    void PresentEnter(uint64_t atUs) noexcept {
        ++window_.presentCalls;
        if(hasPresentEntry_) {
            if(atUs>=previousPresentEntry_) {
                const uint64_t us=atUs-previousPresentEntry_;
                window_.presentInterval.Add(us);
                ++window_.presentBuckets[IntervalBucket(us)];
            } else ++window_.backwardClock;
        }
        if(hasPresentReturn_)AddDelta(window_.outsidePresent,
            previousPresentReturn_,atUs,window_.backwardClock);
        if(hasEndSceneReturn_)AddDelta(window_.endSceneToPresent,
            lastEndSceneReturn_,atUs,window_.backwardClock);
        lastEndSceneReturn_=0;
        hasEndSceneReturn_=false;
        previousPresentEntry_=atUs;
        hasPresentEntry_=true;
        currentPresentEntry_=atUs;
        activePresent_=true;
    }
    void PresentExit(uint64_t atUs,bool success) noexcept {
        if(!success)++window_.presentFailures;
        if(activePresent_)
            AddDelta(window_.presentCall,currentPresentEntry_,atUs,window_.backwardClock);
        activePresent_=false;
        previousPresentReturn_=atUs;
        hasPresentReturn_=true;
    }
    void EndSceneEnter(uint64_t atUs) noexcept {
        ++window_.endSceneCalls;
        currentEndSceneEntry_=atUs;
        activeEndScene_=true;
    }
    void EndSceneExit(uint64_t atUs) noexcept {
        if(activeEndScene_)AddDelta(window_.endSceneCall,
            currentEndSceneEntry_,atUs,window_.backwardClock);
        lastEndSceneReturn_=atUs;
        hasEndSceneReturn_=true;
        activeEndScene_=false;
    }
    void DrawingCallback(uint64_t durationUs) noexcept {
        window_.drawingCallback.Add(durationUs);
    }
    void ProcessCallback(uint64_t durationUs) noexcept {
        window_.processCallback.Add(durationUs);
    }
    void DeviceReset() noexcept {
        ++window_.resets;
        previousPresentEntry_=previousPresentReturn_=lastEndSceneReturn_=0;
        hasPresentEntry_=hasPresentReturn_=hasEndSceneReturn_=false;
        activePresent_=activeEndScene_=false;
    }
    Snapshot TakeWindow() noexcept {
        const Snapshot out=window_;
        window_={};
        return out;
    }
};
} // namespace asc::frame_pacing
