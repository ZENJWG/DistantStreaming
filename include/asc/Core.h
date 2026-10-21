#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace asc {
// Vanilla-only bound. Not intended for limit adjusters / expanded IDs.
constexpr unsigned kVanillaModelInfoCountCap = 20000;
struct Vec3 { float x=0, y=0, z=0; };
struct Cell { int x=0, y=0; float score=0; };
struct Candidate {
    int modelId=-1;
    float score=0;
    unsigned estimatedSectors=0; // optional archive units; cost hint only
    bool dynamic=false; // observed local ped/vehicle demand (opt-in)
    bool urgent=false; // nearby missing world geometry; eligible for priority
};

struct Config {
    bool prefetch=true;
    unsigned tickMs=100;
    unsigned idleScanIntervalMs=250; // 0 keeps predictive scans at every controller tick
    unsigned memoryHighPercent=85;
    unsigned memoryThrottlePercent=75;
    unsigned memoryResumePercent=70;
    unsigned maxRequestsPerTick=4;
    unsigned maxOutstanding=12;
    unsigned backlogLimit=12;
    unsigned maxCells=32;
    unsigned maxCandidates=128;
    unsigned expiryMs=3500;
    unsigned cooldownMs=6000;
    unsigned targetFrameMs=25;
    unsigned frameRecoveryMs=18;
    unsigned slowLoadUs=12000;
    unsigned maxAdaptiveRequests=4;
    unsigned maxPriorityPerTick=2;
    float priorityRadiusUnits=100.0f;
    bool dynamicWarmup=false; // independently bounded, observational warmup
    unsigned dynamicSlotsPerTick=1;
    unsigned dynamicHistoryMs=10000;
    unsigned dynamicMinObservations=2;   // require repeated natural vanilla demand
    unsigned dynamicMinRepeatGapMs=350; // dedupe repeated same-frame requests
    unsigned dynamicRewarmCooldownMs=15000; // per model, survives completed loads
    float dynamicLocalRadiusUnits=120.0f;
    unsigned dynamicMaxHistory=32;
    float lookAheadSeconds=1.5f;
    float predictClampUnits=180.0f;
    float scanRadiusUnits=90.0f;
    float maxTeleportUnits=120.0f;
    unsigned workingSetLimitMiB=1400;
    unsigned privateLimitMiB=1600;
    unsigned minAvailablePhysicalMiB=256;
};

enum class Pressure : uint8_t { Normal, Throttled, Critical };

struct Snapshot {
    uint32_t nowMs=0;
    Vec3 player{}, camera{}, cameraForward{};
    bool cameraValid=false;
    bool playerValid=false;
    bool gameplaySafe=false;
    unsigned area=0;
    uint64_t streamingUsed=0, streamingBudget=0;
    uint64_t privateBytes=0, workingSetBytes=0, availablePhysicalBytes=0;
    uint64_t totalVirtualBytes=0,availableVirtualBytes=0;
    bool largeAddressAware=false,addressSpaceLow=false;
    bool gpuBudgetCritical=false; // WDDM per-process local GPU memory pressure
    bool fastTravel=false,fastCamera=false; // Bounded motion-triggered scheduling hints
    unsigned gameRequests=0;
    unsigned gamePriorityRequests=0;
    // Measured from a *render* callback; 0 means unavailable.
    float recentFrameMs=0;
    // Average time spent inside a vanilla loading call, microseconds.
    uint64_t recentLoadAvgUs=0;
};

struct Stats {
    Pressure state=Pressure::Normal;
    unsigned issued=0, promotions=0, outstanding=0, candidates=0;
    unsigned cells=0, expired=0, adaptiveQuota=0, denied=0, loaded=0;
    unsigned dynamicEligible=0, dynamicIssued=0, dynamicDeferred=0;
    unsigned priorityIssued=0;
    bool teleported=false, memoryGuard=false, frameGuard=false;
};

class IGame {
public:
    virtual ~IGame()=default;
    virtual std::vector<Candidate> Collect(
        const std::vector<Cell>& cells, Vec3 current, Vec3 future,
        Vec3 camera, Vec3 forward, bool cameraValid, unsigned maxCandidates)=0;
    // Adapters may fill reusable storage. True promises finite, unique IDs
    // ordered by urgency, descending score, then ascending model ID.
    virtual bool CollectInto(const std::vector<Cell>& cells, Vec3 current,
        Vec3 future, Vec3 camera, Vec3 forward, bool cameraValid,
        unsigned maxCandidates, std::vector<Candidate>& out) {
        out=Collect(cells,current,future,camera,forward,cameraValid,maxCandidates);
        return false;
    }
    virtual bool CanRequest(int id) const=0;
    virtual unsigned RequestAllowance() const {return ~0u;}
    virtual bool IsLoaded(int id) const=0;
    virtual bool IsStillRequested(int id) const=0;
    virtual void RequestDefault(int id)=0;
    virtual void RequestPriority(int id){RequestDefault(id);}
};

struct Tracked {
    uint32_t firstRequest=0, lastWanted=0;
    bool promoted=false, loaded=false;
};

class Controller {
public:
    explicit Controller(Config cfg={});
    void Tick(const Snapshot& state, IGame& game,
              unsigned cellBudget=0, unsigned candidateBudget=0,
              unsigned tickIntervalOverrideMs=0);
    void OnVanillaRequest(int modelId, int flags);
    // Only model IDs previously requested by VANILLA within local radius.
    void OnDynamicVanillaDemand(int modelId, uint32_t now, Vec3 location);
    const Stats& GetStats() const { return stats_; }
    bool IsTracked(int id) const {return ledger_.find(id)!=ledger_.end();}
    void Reset();
    static std::vector<Cell> SelectCells(Vec3 here, Vec3 future,
                                        float radius, unsigned maxCells);
    static void SelectCells(Vec3 here, Vec3 future,float radius,
                            unsigned maxCells,std::vector<Cell>& out);
    static bool Valid(const Config&);
private:
    void Expire(uint32_t now, const IGame&);
    void UpdatePressure(const Snapshot&);
    void ObserveDemand(const std::vector<Candidate>&, uint32_t now);
    unsigned ComputeQuota(const Snapshot&) const;
    Config cfg_;
    Stats stats_{};
    std::unordered_map<int, Tracked> ledger_;
    std::unordered_map<int, uint32_t> cooldown_;
    struct DynamicHistory {
        int id;
        uint32_t time;
        Vec3 position;
        unsigned hits;
        uint32_t lastWarmAt; // zero means never warmed
        bool warmed=false;
    };
    std::vector<DynamicHistory> dynamicHistory_;
    std::vector<Cell> selectedCells_;
    std::vector<Candidate> dynamicCandidates_;
    std::vector<Candidate> staticCandidates_;
    Snapshot lastStaticScan_{};
    uint64_t demandSerial_=0, scannedDemandSerial_=0;
    bool haveStaticScan_=false;
    Vec3 previous_{};
    uint32_t previousTime_=0, lastTick_=0;
    unsigned previousArea_=0;
    unsigned pendingPromotions_=0;
    bool havePrevious_=false, haveTick_=false;
};
}
