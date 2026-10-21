#pragma once
#include "Hooks.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace readiness {
enum class ModelClass { World, Ped, Vehicle, Other };
struct Summary {
    unsigned loaded=0, timedOut=0, pending=0;
    unsigned pedReady=0, vehicleReady=0;
    uint32_t p50Ms=0,p95Ms=0,queueP95Ms=0;
    uint32_t maxPendingMs=0;
};
class Tracker {
public:
    void OnRequest(int id,uint32_t now,ModelClass type);
    // Must be invoked on the main game thread only.
    void Sample(uint32_t now);
    Summary TakeSummary();
    void Reset();
private:
    struct Entry {
        uint32_t requestedAt=0,channeledAt=0;
        ModelClass type=ModelClass::Other;
    };
    std::unordered_map<int,Entry> active_;
    std::vector<uint32_t> times_, queueTimes_;
    Summary aggregate_{};
};
}
