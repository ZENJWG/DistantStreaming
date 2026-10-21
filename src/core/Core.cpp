#include "asc/Core.h"
#include <algorithm>
#include <bitset>
#include <cmath>
#include <limits>

namespace asc {
namespace {
constexpr float kWorldMin=-3000.0f, kCellSize=50.0f;
constexpr int kCells=120;
constexpr uint64_t kMiB=1048576ULL;
bool finite(Vec3 p) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}
float sq(float n) {return n*n;}
float dot2(float ax,float ay,float bx,float by){return ax*bx+ay*by;}
int sector(float n) {
    // Clamp before conversion: finite world coordinates can still exceed int.
    return int(std::clamp(std::floor((double(n)-kWorldMin)/kCellSize),
                          0.0,double(kCells-1)));
}
bool atLeastPercent(uint64_t used,uint64_t budget,unsigned percent) {
    // ceil(budget * percent / 100), without overflowing uint64_t.
    // Config validation keeps all pressure thresholds within [0,100].
    const uint64_t threshold=(budget/100)*percent+
        ((budget%100)*percent+99)/100;
    return !budget || used>=threshold;
}
float segmentDistanceSquared(float x,float y,Vec3 a,Vec3 b) {
    const float dx=b.x-a.x,dy=b.y-a.y, len2=dx*dx+dy*dy;
    const float t=len2>0.01f?
        std::clamp(dot2(x-a.x,y-a.y,dx,dy)/len2,0.0f,1.0f):0.0f;
    return sq(x-(a.x+t*dx))+sq(y-(a.y+t*dy));
}
}

Controller::Controller(Config cfg):cfg_(cfg) {
    ledger_.reserve(128);
    cooldown_.reserve(128);
    dynamicHistory_.reserve(std::min(cfg_.dynamicMaxHistory,64u));
    dynamicCandidates_.reserve(std::min(cfg_.dynamicMaxHistory,64u));
    staticCandidates_.reserve(cfg_.maxCandidates);
}
bool Controller::Valid(const Config& c) {
    return c.tickMs>=16&&c.tickMs<=1000
        && c.memoryResumePercent>=30
        && c.memoryResumePercent<c.memoryThrottlePercent
        && c.memoryThrottlePercent<c.memoryHighPercent
        && c.memoryHighPercent<=95
        && c.maxRequestsPerTick>=1&&c.maxRequestsPerTick<=16
        && c.maxAdaptiveRequests>=1&&c.maxAdaptiveRequests<=c.maxRequestsPerTick
        && c.maxPriorityPerTick<=4
        && c.priorityRadiusUnits>=25&&c.priorityRadiusUnits<=200
        && c.maxOutstanding>=c.maxRequestsPerTick&&c.maxOutstanding<=128
        && c.maxCells>=2&&c.maxCells<=128
        && c.maxCandidates>=c.maxRequestsPerTick&&c.maxCandidates<=512
        && c.backlogLimit>=c.maxRequestsPerTick&&c.backlogLimit<=128
        && c.expiryMs>=c.tickMs&&c.cooldownMs>=c.tickMs
        && c.lookAheadSeconds>=0&&c.lookAheadSeconds<=4
        && c.predictClampUnits>=0&&c.predictClampUnits<=400
        && c.scanRadiusUnits>=25&&c.scanRadiusUnits<=200
        && std::isfinite(c.maxTeleportUnits)&&c.maxTeleportUnits>10
        && c.targetFrameMs>=10&&c.targetFrameMs<=200
        && c.frameRecoveryMs>=5&&c.frameRecoveryMs<c.targetFrameMs
        && c.slowLoadUs>=1000&&c.slowLoadUs<=500000
        && c.dynamicSlotsPerTick>=1&&c.dynamicSlotsPerTick<=2
        && c.dynamicHistoryMs>=1000&&c.dynamicHistoryMs<=30000
        && c.dynamicMinObservations>=1&&c.dynamicMinObservations<=10
        && c.dynamicMinRepeatGapMs>=c.tickMs
        && c.dynamicMinRepeatGapMs<=c.dynamicHistoryMs
        && c.dynamicRewarmCooldownMs>=c.tickMs
        && c.dynamicRewarmCooldownMs<=120000
        && c.dynamicLocalRadiusUnits>=25.0f
        && c.dynamicLocalRadiusUnits<=250.0f
        && c.dynamicMaxHistory>=4&&c.dynamicMaxHistory<=64
        && (c.privateLimitMiB==0||(c.privateLimitMiB>=512&&c.privateLimitMiB<=4096))
        && (c.workingSetLimitMiB==0||(c.workingSetLimitMiB>=256&&c.workingSetLimitMiB<=4096))
        && c.minAvailablePhysicalMiB>=64&&c.minAvailablePhysicalMiB<=2048;
}

std::vector<Cell> Controller::SelectCells(Vec3 here, Vec3 future,
                                           float radius,unsigned maxCells) {
    std::vector<Cell> out;
    SelectCells(here,future,radius,maxCells,out);
    return out;
}
void Controller::SelectCells(Vec3 here,Vec3 future,float radius,
                              unsigned maxCells,std::vector<Cell>& out) {
    out.clear();
    if(!finite(here)||!finite(future)||!std::isfinite(radius)
       ||radius<0||maxCells<1)return;
    // Capsule covering *both* current and future position, rather than just
    // an endpoint rectangle. Bounds ensure at most 120*120 candidate cells.
    const int x0=sector(std::min(here.x,future.x)-radius);
    const int y0=sector(std::min(here.y,future.y)-radius);
    const int x1=sector(std::max(here.x,future.x)+radius);
    const int y1=sector(std::max(here.y,future.y)+radius);
    // All accepted cells fit in this bounded rectangle. Allocate once rather
    // than repeatedly growing/copying the planning buffer on the game thread.
    out.reserve(std::size_t(x1-x0+1)*std::size_t(y1-y0+1));
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
        const float cx=kWorldMin+(x+0.5f)*kCellSize;
        const float cy=kWorldMin+(y+0.5f)*kCellSize;
        const float corridor=segmentDistanceSquared(cx,cy,here,future);
        // Cell diagonal margin: include sectors intersecting capsule.
        const float expanded=radius+35.36f;
        if(corridor>expanded*expanded)continue;
        const float near=sq(cx-here.x)+sq(cy-here.y);
        const float target=sq(cx-future.x)+sq(cy-future.y);
        const float score=0.50f/(1.0f+corridor*0.0004f)
                         +0.35f/(1.0f+near*0.0004f)
                         +0.15f/(1.0f+target*0.0004f);
        if(!std::isfinite(score))continue;
        out.push_back({x,y,score});
    }
    const auto rankCell=[](const Cell&a,const Cell&b){
        if(a.score!=b.score)return a.score>b.score;
        return a.y==b.y?a.x<b.x:a.y<b.y;
    };
    if(out.size()>maxCells){
        std::nth_element(out.begin(),out.begin()+maxCells,out.end(),rankCell);
        out.resize(maxCells);
    }
    std::sort(out.begin(),out.end(),rankCell);
    // Guarantee both trajectory endpoints even if the top-K ranking would
    // have filled the budget with nearer intermediate cells.
    const int curX=sector(here.x),curY=sector(here.y);
    const int endX=sector(future.x),endY=sector(future.y);
    auto contains=[&](int x,int y){
        return std::any_of(out.begin(),out.end(),[&](const Cell&c){
            return c.x==x&&c.y==y;
        });
    };
    if(!contains(curX,curY)&&!out.empty())
        out.back()={curX,curY,1.0f};
    if((curX!=endX||curY!=endY)&&!contains(endX,endY)&&out.size()>=2) {
        auto replace=std::find_if(out.rbegin(),out.rend(),[&](const Cell& c){
            return c.x!=curX||c.y!=curY;
        });
        *replace={endX,endY,0.9f};
    }
}
void Controller::Reset() {
    ledger_.clear();cooldown_.clear();dynamicHistory_.clear();
    selectedCells_.clear();dynamicCandidates_.clear();
    staticCandidates_.clear();haveStaticScan_=false;
    demandSerial_=scannedDemandSerial_=0;
    havePrevious_=haveTick_=false;
    pendingPromotions_=0;
    stats_=Stats{};
}
void Controller::OnVanillaRequest(int id,int) {
    ++demandSerial_;
    auto it=ledger_.find(id);
    if(it!=ledger_.end()&&!it->second.promoted){
        it->second.promoted=true;
        ++pendingPromotions_;
    }
}
void Controller::OnDynamicVanillaDemand(int id,uint32_t now,Vec3 p) {
    if(!cfg_.dynamicWarmup||id<0||
       id>=int(kVanillaModelInfoCountCap)||!finite(p))return;
    for(auto& item:dynamicHistory_) {
        if(item.id!=id)continue;
        const uint32_t age=uint32_t(now-item.time);
        const float d2=sq(item.position.x-p.x)+sq(item.position.y-p.y);
        if(age>cfg_.dynamicHistoryMs ||
           d2>sq(cfg_.dynamicLocalRadiusUnits)) {
            // A request far away, or outside the rolling learning window,
            // starts a new observation sequence; it cannot inherit confidence.
            item.hits=1;
            // Confidence expires independently of the last warmup. Preserve
            // the cooldown even when HistoryMs is shorter than that cooldown.
        } else {
            if(age<cfg_.dynamicMinRepeatGapMs)
                return; // duplicate requests within one sampling window
            item.hits=std::min(item.hits+1,cfg_.dynamicMinObservations+8);
        }
        item.time=now;
        item.position=p;
        return;
    }
    if(dynamicHistory_.size()>=cfg_.dynamicMaxHistory) {
        // Wrap-safe by selecting greatest age relative to "now".
        auto oldest=std::max_element(
            dynamicHistory_.begin(),dynamicHistory_.end(),
            [now](const DynamicHistory&a,const DynamicHistory&b) {
                return uint32_t(now-a.time)<uint32_t(now-b.time);
            });
        *oldest={id,now,p,1,0,false};
    } else dynamicHistory_.push_back({id,now,p,1,0,false});
}
void Controller::UpdatePressure(const Snapshot&s) {
    stats_.memoryGuard=
        s.addressSpaceLow || s.gpuBudgetCritical ||
        (cfg_.privateLimitMiB&&s.privateBytes>uint64_t(cfg_.privateLimitMiB)*kMiB) ||
        (cfg_.workingSetLimitMiB&&s.workingSetBytes>uint64_t(cfg_.workingSetLimitMiB)*kMiB) ||
        (s.availablePhysicalBytes &&
         s.availablePhysicalBytes<uint64_t(cfg_.minAvailablePhysicalMiB)*kMiB);
    if(stats_.memoryGuard||atLeastPercent(s.streamingUsed,s.streamingBudget,
                                         cfg_.memoryHighPercent))
        stats_.state=Pressure::Critical;
    else if(!atLeastPercent(s.streamingUsed,s.streamingBudget,
                            cfg_.memoryResumePercent+1)&&!stats_.memoryGuard)
        stats_.state=Pressure::Normal;
    else if(stats_.state==Pressure::Normal&&
            atLeastPercent(s.streamingUsed,s.streamingBudget,
                           cfg_.memoryThrottlePercent))
        stats_.state=Pressure::Throttled;
}

void Controller::ObserveDemand(const std::vector<Candidate>& candidates,
                               uint32_t now) {
    // Refresh ALL observed demand *before* applying request quota.
    for(const auto& c:candidates) {
        auto it=ledger_.find(c.modelId);
        if(it!=ledger_.end())it->second.lastWanted=now;
    }
}
void Controller::Expire(uint32_t now,const IGame& game) {
    stats_.outstanding=0;
    for(auto it=ledger_.begin();it!=ledger_.end();) {
        auto& v=it->second;
        const bool stale=uint32_t(now-v.lastWanted)>cfg_.expiryMs;
        const bool wasLoaded=v.loaded;
        v.loaded=game.IsLoaded(it->first);
        const bool pending=!v.loaded&&game.IsStillRequested(it->first);
        // Expiry releases observational history, not the engine request.
        // Keep counting our queued work until it completes or vanilla takes it.
        if((stale&&!pending)||v.promoted) {
            if(stale&&!v.promoted){
                // A completed load isn't failed speculation. Adding cooldown
                // here would delay a later real unload/reload by six seconds.
                if(!v.loaded&&!wasLoaded)cooldown_[it->first]=now;
                ++stats_.expired;
            }
            it=ledger_.erase(it);continue;
        }
        if(pending)++stats_.outstanding;
        else if(!v.loaded) {
            if(!wasLoaded)cooldown_[it->first]=now;
            it=ledger_.erase(it);continue;
        } else ++stats_.loaded;
        ++it;
    }
    for(auto it=cooldown_.begin();it!=cooldown_.end();)
        if(uint32_t(now-it->second)>cfg_.cooldownMs)
            it=cooldown_.erase(it);
        else ++it;
}
unsigned Controller::ComputeQuota(const Snapshot&s) const {
    if(stats_.state==Pressure::Critical||stats_.memoryGuard
       ||s.gameRequests>=cfg_.backlogLimit)return 0;
    unsigned quota=cfg_.maxRequestsPerTick;
    // Moderate streaming pressure reduces admission rather than stopping it
    // indefinitely while vanilla keeps its cache above the resume threshold.
    if(stats_.state==Pressure::Throttled)quota=1;
    // Frame and loading costs throttle *our* speculation, never vanilla.
    if(s.recentFrameMs>=cfg_.targetFrameMs ||
       s.recentLoadAvgUs>=cfg_.slowLoadUs) {
        return 0;
    }
    if(s.recentFrameMs>=cfg_.frameRecoveryMs)
        quota=std::min(quota,1u);
    if(s.gameRequests>=cfg_.backlogLimit/2)
        quota=std::min(quota,1u);
    return std::min(quota,cfg_.maxAdaptiveRequests);
}

void Controller::Tick(const Snapshot&s,IGame&game,
                      unsigned cellBudget,unsigned candidateBudget,
                      unsigned tickIntervalOverrideMs) {
    if(!Valid(cfg_))return;
    const unsigned interval=tickIntervalOverrideMs?
        std::min(cfg_.tickMs,tickIntervalOverrideMs):cfg_.tickMs;
    if(haveTick_&&uint32_t(s.nowMs-lastTick_)<interval)return;
    haveTick_=true;lastTick_=s.nowMs;
    // Promotions arrive before Tick; publish them once in this tick's stats.
    stats_.promotions=pendingPromotions_;
    pendingPromotions_=0;
    stats_.issued=stats_.candidates=stats_.cells=stats_.expired=
        stats_.loaded=stats_.denied=stats_.dynamicEligible=
        stats_.dynamicIssued=stats_.dynamicDeferred=0;
    stats_.teleported=false;stats_.frameGuard=false;
    stats_.priorityIssued=0;
    UpdatePressure(s);
    if(!s.gameplaySafe||!s.playerValid||!finite(s.player)
       ||s.area!=0||s.streamingBudget==0){
        ledger_.clear();cooldown_.clear();dynamicHistory_.clear();
        havePrevious_=false;
        haveStaticScan_=false;
        stats_.outstanding=0;stats_.adaptiveQuota=0;return;
    }
    Vec3 future=s.player;
    if(havePrevious_) {
        const float travel2=sq(s.player.x-previous_.x)+
                            sq(s.player.y-previous_.y)+
                            sq(s.player.z-previous_.z);
        if(s.area!=previousArea_||travel2>sq(cfg_.maxTeleportUnits)){
            ledger_.clear();cooldown_.clear();dynamicHistory_.clear();
            havePrevious_=false;stats_.teleported=true;
            haveStaticScan_=false;
            stats_.outstanding=stats_.adaptiveQuota=0;
            previous_=s.player;previousArea_=s.area;return;
        }
        const uint32_t elapsed=uint32_t(s.nowMs-previousTime_);
        if(elapsed>=16&&elapsed<=1000) {
            const float dt=elapsed/1000.0f;
            future.x+=std::clamp((s.player.x-previous_.x)/dt*
                                cfg_.lookAheadSeconds,
                                -cfg_.predictClampUnits,cfg_.predictClampUnits);
            future.y+=std::clamp((s.player.y-previous_.y)/dt*
                                cfg_.lookAheadSeconds,
                                -cfg_.predictClampUnits,cfg_.predictClampUnits);
        }
    }
    previous_=s.player;previousTime_=s.nowMs;previousArea_=s.area;
    havePrevious_=true;
    // Prefetch and dynamic readiness are independent admission sources.
    // Both must respect the same engine backlog, memory, and frame caps.
    // Every admission below costs at least 1 MiB. If reserved native headroom
    // leaves less than that, no candidate can be issued, regardless of rank.
    // Archive size remains a cost hint rather than a resident-memory measure.
    const uint64_t freeBudget=s.streamingBudget>s.streamingUsed?
        s.streamingBudget-s.streamingUsed:0;
    const uint64_t reserve=std::min<uint64_t>(64*kMiB,s.streamingBudget/8);
    uint64_t admissionBytes=freeBudget>reserve?freeBudget-reserve:0;
    const unsigned quota=admissionBytes>=kMiB?
        std::min(ComputeQuota(s),game.RequestAllowance()):0;
    if(!quota){
        // Do not traverse world sectors or rank speculative candidates while
        // memory, loading backlog, or frame time already forbids requests.
        // Expiry still accounts for pending work and vanilla promotions.
        Expire(s.nowMs,game);
        stats_.adaptiveQuota=0;
        stats_.frameGuard=s.recentFrameMs>=cfg_.targetFrameMs ||
                          s.recentLoadAvgUs>=cfg_.slowLoadUs;
        return;
    }
    const bool collectStatic=cfg_.prefetch;
    const bool considerDynamic=cfg_.dynamicWarmup;
    if(!collectStatic&&!considerDynamic) {
        Expire(s.nowMs,game);
        stats_.adaptiveQuota=0;
        stats_.frameGuard=(s.recentFrameMs>=cfg_.targetFrameMs||
                           s.recentLoadAvgUs>=cfg_.slowLoadUs);
        return;
    }
    auto& candidates=staticCandidates_;
    candidates.clear();
    bool canonicalStatic=false;
    const auto moved=[](Vec3 a,Vec3 b,float threshold) {
        return sq(a.x-b.x)+sq(a.y-b.y)+sq(a.z-b.z)>sq(threshold);
    };
    // Revisit stationary scenes on the idle cadence, without retaining
    // entities or old candidates. Live safety checks and expiry still run.
    const bool scanStatic=s.fastTravel || s.fastCamera || !haveStaticScan_ ||
        uint32_t(s.nowMs-lastStaticScan_.nowMs)>=cfg_.idleScanIntervalMs ||
        demandSerial_!=scannedDemandSerial_ ||
        s.area!=lastStaticScan_.area ||
        s.gameRequests!=lastStaticScan_.gameRequests ||
        s.streamingUsed!=lastStaticScan_.streamingUsed ||
        s.cameraValid!=lastStaticScan_.cameraValid ||
        moved(s.player,lastStaticScan_.player,1.0f) ||
        (s.cameraValid && (moved(s.camera,lastStaticScan_.camera,1.0f) ||
                          moved(s.cameraForward,lastStaticScan_.cameraForward,0.025f)));
    if(collectStatic && scanStatic) {
        // Budget only our predictive scan; never reduce native renderer sectors.
        const unsigned cellsAllowed=cellBudget
            ? std::min(cfg_.maxCells,cellBudget):cfg_.maxCells;
        const unsigned candidatesAllowed=candidateBudget
            ? std::min(cfg_.maxCandidates,candidateBudget):cfg_.maxCandidates;
        auto& cells=selectedCells_;
        SelectCells(s.player,future,cfg_.scanRadiusUnits,cellsAllowed,cells);
        stats_.cells=unsigned(cells.size());
        canonicalStatic=game.CollectInto(cells,s.player,future,
                                s.camera,s.cameraForward,
                                s.cameraValid,candidatesAllowed,candidates);
        lastStaticScan_=s;scannedDemandSerial_=demandSerial_;
        haveStaticScan_=true;
    }

    // Keep dynamic hints separate from static ranking. A separate
    // per-tick quota prevents a large static candidate pool from starving
    // learned traffic, while the shared total quota prevents extra I/O.
    auto& dynamicCandidates=dynamicCandidates_;
    dynamicCandidates.clear();
    if(considerDynamic) {
        for(const auto& item:dynamicHistory_) {
            const uint32_t age=uint32_t(s.nowMs-item.time);
            if(age>cfg_.dynamicHistoryMs||
               item.hits<cfg_.dynamicMinObservations)continue;
            const float d2=sq(item.position.x-s.player.x)+
                           sq(item.position.y-s.player.y);
            if(d2>sq(cfg_.dynamicLocalRadiusUnits))continue;
            if(item.warmed &&
               uint32_t(s.nowMs-item.lastWarmAt)<
                   cfg_.dynamicRewarmCooldownMs)continue;
            dynamicCandidates.push_back({
                item.id,0.20f+0.20f/(1.0f+d2*.0001f)
                         +0.04f*float(std::min(item.hits,5u)),0,true
            });
        }
    }
    auto rank=[](std::vector<Candidate>&c) {
        // NaN scores violate the sort comparator's ordering contract.
        c.erase(std::remove_if(c.begin(),c.end(),[](const Candidate& v){
            return v.modelId<0||v.modelId>=int(kVanillaModelInfoCountCap)||
                   !std::isfinite(v.score);
        }),c.end());
        const auto better=[](const Candidate&a,const Candidate&b) {
            if(a.urgent!=b.urgent)return a.urgent>b.urgent;
            if(a.score!=b.score)return a.score>b.score;
            return a.modelId<b.modelId;
        };
        if(!std::is_sorted(c.begin(),c.end(),better))
            std::sort(c.begin(),c.end(),better);
        std::bitset<kVanillaModelInfoCountCap> seen;
        c.erase(std::remove_if(c.begin(),c.end(),
            [&](const Candidate&v){
                if(seen.test(unsigned(v.modelId)))return true;
                seen.set(unsigned(v.modelId));return false;
            }),
            c.end());
    };
    if(!canonicalStatic)rank(candidates);
    rank(dynamicCandidates);
    // If the same model is already in the static path, don't submit it
    // again as a dynamic warming candidate.
    std::bitset<kVanillaModelInfoCountCap> staticIds;
    for(const auto& c:candidates)staticIds.set(unsigned(c.modelId));
    dynamicCandidates.erase(
        std::remove_if(dynamicCandidates.begin(),dynamicCandidates.end(),
            [&](const Candidate&c){return staticIds.test(unsigned(c.modelId));}),
        dynamicCandidates.end());

    stats_.dynamicEligible=unsigned(dynamicCandidates.size());
    stats_.candidates=unsigned(candidates.size()+dynamicCandidates.size());
    ObserveDemand(candidates,s.nowMs);
    ObserveDemand(dynamicCandidates,s.nowMs);
    Expire(s.nowMs,game);
    stats_.adaptiveQuota=quota;
    stats_.frameGuard=(s.recentFrameMs>=cfg_.targetFrameMs||
                       s.recentLoadAvgUs>=cfg_.slowLoadUs);
    unsigned staticQuota=cfg_.prefetch?quota:0;
    unsigned dynamicQuota=considerDynamic?
        std::min(cfg_.dynamicSlotsPerTick,quota):0;
    // Reserve at most one slot for local traffic when eligible, and
    // keep the remaining quota for visible static-world assets.
    if(!dynamicCandidates.empty()&&staticQuota>0)
        staticQuota-=dynamicQuota;

    auto admit=[&](const std::vector<Candidate>&items,std::size_t& cursor,
                   unsigned limit,bool dynamic) {
        unsigned issuedHere=0;
        while(cursor<items.size()) {
            if(issuedHere>=limit || stats_.issued>=quota ||
               stats_.outstanding>=cfg_.maxOutstanding ||
               s.gameRequests+stats_.issued>=cfg_.backlogLimit)break;
            const auto& c=items[cursor++];
            if(c.modelId<0||ledger_.contains(c.modelId))continue;
            if(cooldown_.contains(c.modelId))continue;
            const uint64_t cost=std::max<uint64_t>(kMiB,
                uint64_t(c.estimatedSectors)*2048);
            if(cost>admissionBytes){++stats_.denied;continue;}
            if(!game.CanRequest(c.modelId)||game.IsLoaded(c.modelId)||
               game.IsStillRequested(c.modelId)){
                ++stats_.denied;continue;
            }
            ledger_[c.modelId]={s.nowMs,s.nowMs,false,false};
            const bool priority=!dynamic&&c.urgent&&
                stats_.state==Pressure::Normal&&s.gamePriorityRequests==0&&
                stats_.priorityIssued<cfg_.maxPriorityPerTick;
            if(priority){game.RequestPriority(c.modelId);++stats_.priorityIssued;}
            else game.RequestDefault(c.modelId);
            admissionBytes-=cost;
            ++issuedHere;++stats_.issued;++stats_.outstanding;
            if(dynamic) {
                ++stats_.dynamicIssued;
                for(auto&h:dynamicHistory_)if(h.id==c.modelId) {
                    h.warmed=true;
                    h.lastWarmAt=s.nowMs;
                    break;
                }
            }
        }
    };
    std::size_t staticCursor=0,dynamicCursor=0;
    admit(candidates,staticCursor,staticQuota,false);
    if(dynamicQuota)admit(dynamicCandidates,dynamicCursor,dynamicQuota,true);
    // Redistribute an unused dynamic reservation to static prefetch.
    // The overall speculative request budget remains unchanged.
    if(cfg_.prefetch&&stats_.issued<quota)
        admit(candidates,staticCursor,quota-stats_.issued,false);
    stats_.dynamicDeferred=stats_.dynamicEligible>stats_.dynamicIssued?
        stats_.dynamicEligible-stats_.dynamicIssued:0;

}
} // namespace asc
