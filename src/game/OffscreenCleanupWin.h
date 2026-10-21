#pragma once
#include "asc/OffscreenCleanup.h"
#include <ostream>
#include <vector>
#include <array>

class OffscreenCleanup {
public:
    struct Counters {
        uint64_t scanned=0,instances=0,models=0,textures=0,streamBytes=0;
        uint64_t totalUs=0,peakUs=0,budgetStops=0;
        uint64_t nativeTrimCandidates=0,nativeTrimEvicted=0,nativeTrimBytes=0,
                 nativeTrimScanned=0,nativeTrimActiveFrames=0;
    };
    void Configure(asc::offscreen::Settings,std::ostream&);
    void Frame(asc::Snapshot,const asc::Config&);
    void Reset();
    // Runs only at the game's safe drawing callback, after object reachability checks.
    bool TryRemoveUnreferenced(int id,uint32_t now,bool trimOrigin);
    Counters TakeCounters(){auto out=counters_;counters_={};return out;}
private:
    asc::offscreen::Settings cfg_{};
    std::vector<asc::offscreen::Stamp> buildingStamps_,dummyStamps_;
    uintptr_t buildingPool_=0,dummyPool_=0;
    unsigned cursor_=0;
    int64_t frequency_=0;
    bool available_=false,removeAvailable_=false,haveDirection_=false,turnHold_=false;
    asc::Vec3 direction_{};
    uint32_t turnAt_=0;
    Counters counters_{};
    asc::offscreen::PendingModels pendingModels_;
    std::array<uint32_t,asc::kVanillaModelInfoCountCap> eligibleSince_{};
    unsigned trimCursor_=0;
    uint32_t lastTrimAt_=0;
    bool trimActive_=false;
};
