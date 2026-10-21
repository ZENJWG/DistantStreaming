#pragma once
#include <algorithm>
#include <cstdint>

namespace asc::stability {
// This policy only paces DistantStreaming's speculative controller.
// GTA-owned model loads, renderer calls, and normal streaming are never
// moved off the main thread or throttled by this policy.
struct Settings {
    bool enabled=true;
    bool allowBlockingCameraLoads=false;
    unsigned controllerIntervalMs=120;
    unsigned slowControllerUs=4000;
    unsigned slowRetryMs=350;
    unsigned maxPredictiveCells=24;
    unsigned maxPredictiveCandidates=96;
};
inline bool Valid(const Settings& s) {
    return s.controllerIntervalMs>=40 && s.controllerIntervalMs<=2000 &&
           s.slowControllerUs>=500 && s.slowControllerUs<=100000 &&
           s.slowRetryMs>=s.controllerIntervalMs && s.slowRetryMs<=5000 &&
           s.maxPredictiveCells>=2 && s.maxPredictiveCells<=128 &&
           s.maxPredictiveCandidates>=8 && s.maxPredictiveCandidates<=512;
}
// Wrap-safe elapsed-time calculation; avoids sleeping or blocking the game.
class ControllerGate {
public:
    bool Due(uint32_t now, const Settings& s,unsigned fastIntervalMs=0) {
        if(!s.enabled) return true;
        if(!hasRun_) return true;
        const unsigned normal=fastIntervalMs?
            std::min(s.controllerIntervalMs,fastIntervalMs):s.controllerIntervalMs;
        const unsigned interval=lastCostUs_>=s.slowControllerUs
            ? s.slowRetryMs:normal;
        if(uint32_t(now-lastRunMs_)<interval) {
            ++skips_;
            return false;
        }
        return true;
    }
    void Record(uint32_t now, uint64_t costUs, const Settings& s) {
        if(!s.enabled)return;
        hasRun_=true;
        lastRunMs_=now;
        lastCostUs_=static_cast<unsigned>(std::min<uint64_t>(costUs,1000000u));
        ++runs_;
        if(lastCostUs_>=s.slowControllerUs) ++slowRuns_;
    }
    uint64_t TakeSkips() { const auto n=skips_;skips_=0;return n; }
    uint64_t TakeRuns() { const auto n=runs_;runs_=0;return n; }
    uint64_t TakeSlowRuns() { const auto n=slowRuns_;slowRuns_=0;return n; }
    unsigned LastCostUs() const {return lastCostUs_;}
private:
    bool hasRun_=false;
    uint32_t lastRunMs_=0;
    unsigned lastCostUs_=0;
    uint64_t skips_=0,runs_=0,slowRuns_=0;
};
}
