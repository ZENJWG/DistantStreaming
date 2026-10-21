#pragma once
// A bounded, opt-in bridge from read-only x64 WorldCatalog hints to GTA's
// regular main-thread RequestModel pipeline. No IPC, engine access or hooks here.
#include "asc/Core.h"
#include "asc/NativeMemoryProtocol.h"
#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

namespace asc::catalog_prefetch {

struct Settings {
    bool enabled=false;
    unsigned maxRequestsPerTick=2;    // Additional requests; also respect controller's shared quota
    unsigned maxOutstanding=8;
    unsigned nativeBacklogLimit=16;
    unsigned maxChecksPerTick=16;
    unsigned maxMemoryPercent=82;
    uint32_t cooldownMs=8000;
    uint32_t lookAheadMs=1800;
    float maxPredictionUnits=320.0f;
};

inline bool Valid(const Settings& c) noexcept {
    return c.maxRequestsPerTick>=1 && c.maxRequestsPerTick<=2 &&
        c.maxOutstanding>=1 && c.maxOutstanding<=16 &&
        c.nativeBacklogLimit>=1 && c.nativeBacklogLimit<=128 &&
        c.maxChecksPerTick>=1 && c.maxChecksPerTick<=64 &&
        c.maxMemoryPercent>=40 && c.maxMemoryPercent<=95 &&
        c.cooldownMs>=1000 && c.cooldownMs<=120000 &&
        c.lookAheadMs<=6000 && std::isfinite(c.maxPredictionUnits) &&
        c.maxPredictionUnits>=0 && c.maxPredictionUnits<=600;
}

inline bool CopySafeName(const char raw[24], char (&out)[24]) noexcept {
    size_t length=0;
    for(;length<24 && raw[length];++length) {
        const unsigned char ch=static_cast<unsigned char>(raw[length]);
        const bool okay=(ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||
            (ch>='0'&&ch<='9')||ch=='_'||ch=='-'||ch=='.';
        if(!okay)return false;
    }
    // Reject full-width/truncated names and invalid UTF-8; the host hash API
    // must receive a short, terminated, predictable ASCII model name.
    if(length==0||length==24)return false;
    std::memcpy(out,raw,length);
    out[length]='\0';
    return true;
}

class Trajectory {
    Vec3 previous_{};
    uint32_t previousAt_=0;
    bool havePrevious_=false;
public:
    void Reset() noexcept {havePrevious_=false;}
    Vec3 Predict(Vec3 p,uint32_t now,const Settings& cfg,
                 float teleportUnits) noexcept {
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)) {
            Reset();return {};
        }
        const Vec3 old=previous_;
        const uint32_t dt=uint32_t(now-previousAt_);
        const bool usable=havePrevious_ && dt>=10 && dt<=1000 &&
            std::isfinite(teleportUnits) && teleportUnits>0;
        previous_=p;previousAt_=now;havePrevious_=true;
        if(!usable||cfg.lookAheadMs==0)return p;
        const float dx=p.x-old.x,dy=p.y-old.y;
        const float dist=std::hypot(dx,dy);
        if(!std::isfinite(dist) || dist>teleportUnits)return p;
        const float desired=dist*(float(cfg.lookAheadMs)/float(dt));
        const float ahead=std::min(desired,cfg.maxPredictionUnits);
        if(dist>0.001f && ahead>0) {
            p.x+=dx/dist*ahead;
            p.y+=dy/dist*ahead;
        }
        return p;
    }
};

struct Counters {
    uint64_t requests=0, checked=0, badName=0, wrongIdentity=0;
    uint64_t duplicate=0, notEligible=0, staleCell=0, pressureSkips=0;
    uint64_t completed=0;
    unsigned outstanding=0;
};

// The caller owns the live ID/name validation and supplies GTA's adapter.
// It MUST call this exclusively on the game thread, after validating OLA tables.
class Scheduler {
    struct Pending {int id=-1; uint32_t at=0;};
    Settings cfg_{};
    std::array<Pending,16> pending_{};
    unsigned pendingSize_=0;
    std::array<uint32_t,kVanillaModelInfoCountCap> lastRequestedAt_{};
    std::bitset<kVanillaModelInfoCountCap> previouslyRequested_{};
    native::CatalogCellReply page_{};
    uint64_t pageSerial_=0;
    bool havePage_=false;
    unsigned cursor_=0;
    uint32_t previousFrame_=0;
    unsigned issuedThisFrame_=0;
    bool haveFrame_=false;
    Counters counts_{};
public:
    void Configure(const Settings& cfg) noexcept {cfg_=cfg;}
    const Counters& GetCounters() const noexcept {return counts_;}
    void Reset() noexcept {
        pendingSize_=0;
        havePage_=false;pageSerial_=0;cursor_=0;
        haveFrame_=false;issuedThisFrame_=0;
        counts_.outstanding=0;
    }
    template<class VerifyIdentity>
    void Tick(const Snapshot& s,const native::CatalogCellReply* hints,
              uint64_t serial,int expectedCellX,int expectedCellY,
              uint32_t frameId,unsigned controllerIssued,unsigned sharedQuota,
              IGame& game,VerifyIdentity&& verifyIdentity) {
        if(!cfg_.enabled || !Valid(cfg_))return;
        if(!haveFrame_ || frameId!=previousFrame_){
            previousFrame_=frameId;haveFrame_=true;issuedThisFrame_=0;
        }
        // Keep the actual outstanding set bounded rather than assuming that a
        // request completes after an arbitrary amount of time. No cancellation.
        for(unsigned i=0;i<pendingSize_;) {
            const int id=pending_[i].id;
            if(game.IsStillRequested(id)) {++i;continue;}
            if(game.IsLoaded(id))++counts_.completed;
            pending_[i]=pending_[--pendingSize_];
        }
        counts_.outstanding=pendingSize_;
        if(hints && serial && (!havePage_ || serial!=pageSerial_)){
            page_=*hints;pageSerial_=serial;cursor_=0;havePage_=true;
        }
        if(!hints || !havePage_ || page_.modelCount>64 ||
           page_.cellX!=expectedCellX||page_.cellY!=expectedCellY){
            if(havePage_)++counts_.staleCell;
            return;
        }
        if(!game.RequestAllowance() || !s.gameplaySafe || !s.playerValid || s.area!=0 ||
           s.addressSpaceLow ||
           s.gpuBudgetCritical || s.streamingBudget==0 ||
           s.streamingUsed>=s.streamingBudget ||
           uint64_t(s.streamingUsed)*100>=
                 uint64_t(s.streamingBudget)*cfg_.maxMemoryPercent ||
           s.availablePhysicalBytes<256ull*1024*1024 ||
           (s.recentFrameMs>0 && s.recentFrameMs>=20.0f) ||
           (s.recentLoadAvgUs>12000) ||
           s.gamePriorityRequests>1 ||
           s.gameRequests>=cfg_.nativeBacklogLimit ||
           pendingSize_>=cfg_.maxOutstanding ||
           controllerIssued>=sharedQuota ||
           issuedThisFrame_>=cfg_.maxRequestsPerTick) {
            ++counts_.pressureSkips;
            return;
        }
        const unsigned quota=std::min({
            cfg_.maxRequestsPerTick-issuedThisFrame_,
            cfg_.maxOutstanding-pendingSize_,
            sharedQuota-controllerIssued,
            cfg_.nativeBacklogLimit-s.gameRequests
        });
        unsigned issued=0,checked=0;
        while(cursor_<page_.modelCount && issued<quota && game.RequestAllowance() &&
              checked<cfg_.maxChecksPerTick) {
            const unsigned idx=cursor_++;
            ++counts_.checked;++checked;
            const int id=page_.modelIds[idx];
            if(id<0 || id>=int(kVanillaModelInfoCountCap)){
                ++counts_.wrongIdentity;continue;
            }
            char name[24]{};
            if(!CopySafeName(page_.modelNames[idx],name)){
                ++counts_.badName;continue;
            }
            if(previouslyRequested_[id] &&
               uint32_t(s.nowMs-lastRequestedAt_[id])<cfg_.cooldownMs){
                ++counts_.duplicate;continue;
            }
            bool inFlight=false;
            for(unsigned j=0;j<pendingSize_;++j)
                if(pending_[j].id==id){inFlight=true;break;}
            if(inFlight){++counts_.duplicate;continue;}
            // Never trust IDE IDs by themselves: resolve name using the
            // live game model hash, then require the EXACT indicated ID.
            if(!verifyIdentity(id,name)) {
                ++counts_.wrongIdentity;continue;
            }
            if(!game.CanRequest(id)) {
                ++counts_.notEligible;continue;
            }
            // The request is a normal, non-priority, non-pinning GTA request.
            game.RequestDefault(id);
            lastRequestedAt_[id]=s.nowMs;
            previouslyRequested_.set(id);
            pending_[pendingSize_++]={id,s.nowMs};
            ++counts_.requests;++issued;++issuedThisFrame_;
        }
        counts_.outstanding=pendingSize_;
    }
};

} // namespace asc::catalog_prefetch
