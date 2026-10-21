#pragma once
#include "asc/Core.h"
#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>

namespace asc::camera {
struct Settings {
    bool enabled=true,blocking=false,instanceLoaded=true;
    unsigned intervalMs=100,maxModels=8,maxInstances=32,maxBatchSectors=16384;
    // Camera catch-up is only a supplemental readiness hint; native
    // visibility and extended normal/LOD world scan remain independent.
    unsigned maxScanCells=64,maxScanNodes=3072,maxScanUs=3000;
    float range=300,halfAngleDegrees=70;
};
inline bool Valid(const Settings& c){
    return c.intervalMs>=100&&c.intervalMs<=5000&&c.maxModels>=1&&c.maxModels<=8
        &&c.maxInstances<=64&&c.maxBatchSectors>=1&&c.maxBatchSectors<=131072
        &&c.maxScanCells>=2&&c.maxScanCells<=64
        &&c.maxScanNodes>=64&&c.maxScanNodes<=4096
        &&c.maxScanUs>=250&&c.maxScanUs<=10000
        &&c.range>=50&&c.range<=600&&c.halfAngleDegrees>=10&&c.halfAngleDegrees<=89;
}
inline bool Allowed(const Snapshot& s,const Config& core,const Settings& cfg){
    constexpr uint64_t MiB=1048576;
    return cfg.enabled&&s.gameplaySafe&&s.playerValid&&s.cameraValid&&s.area==0
        &&!s.addressSpaceLow&&!s.gpuBudgetCritical&&s.streamingBudget
        &&s.streamingUsed<s.streamingBudget*core.memoryThrottlePercent/100
        &&(!s.availablePhysicalBytes||s.availablePhysicalBytes>=uint64_t(core.minAvailablePhysicalMiB)*MiB)
        &&(!core.privateLimitMiB||s.privateBytes<=uint64_t(core.privateLimitMiB)*MiB)
        &&(!core.workingSetLimitMiB||s.workingSetBytes<=uint64_t(core.workingSetLimitMiB)*MiB)
        &&(cfg.blocking||(s.gameRequests<core.backlogLimit
            &&s.recentFrameMs<core.targetFrameMs&&s.recentLoadAvgUs<core.slowLoadUs));
}
inline bool InView(Vec3 p,Vec3 camera,Vec3 forward,float range,float cosine){
    const float x=p.x-camera.x,y=p.y-camera.y,z=p.z-camera.z;
    const float distance2=x*x+y*y+z*z;
    const float norm2=forward.x*forward.x+forward.y*forward.y+forward.z*forward.z;
    if(!std::isfinite(distance2)||!std::isfinite(norm2)||norm2<0.01f||distance2>range*range)return false;
    if(distance2<1)return true;
    return (x*forward.x+y*forward.y+z*forward.z)>=cosine*std::sqrt(distance2*norm2);
}
// Recheck the live queue for every supplemental DFF/TXD request: a batch's
// dependencies can fill it after Allowed() admitted the initial scan.
// Promoting already queued work adds no new queue entry.
inline bool MayRequest(bool blocking,bool alreadyQueued,unsigned backlog,
                       unsigned limit) noexcept {
    return blocking||alreadyQueued||backlog<limit;
}
struct Target {
    int id;
    float distance2;
    unsigned sectors;
    bool loaded=false,priority=false;
};
struct Batch {
    std::array<int,8> requestIds{};
    std::array<int,8> selectedIds{};
    unsigned requests=0,selected=0;
    bool needsFlush=false;
};
// Caller supplies only eligible visible static world models, including those
// already queued by vanilla. Pending priority models still require a flush.
inline Batch Plan(std::vector<Target>& targets,const Settings& cfg){
    Batch out;
    if(!cfg.enabled||!Valid(cfg))return out;
    targets.erase(std::remove_if(targets.begin(),targets.end(),[](const Target& t){
        return !std::isfinite(t.distance2)||t.distance2<0;
    }),targets.end());
    std::sort(targets.begin(),targets.end(),[](const Target& a,const Target& b){
        return a.distance2==b.distance2?a.id<b.id:a.distance2<b.distance2;
    });
    std::bitset<kVanillaModelInfoCountCap> seen;
    uint64_t sectors=0;
    for(const auto& t:targets){
        if(t.id<0||t.id>=int(kVanillaModelInfoCountCap)||t.loaded||!t.sectors
           ||!std::isfinite(t.distance2)||seen.test(unsigned(t.id)))continue;
        seen.set(unsigned(t.id));
        if(out.selected>=cfg.maxModels)break;
        // One oversized near model may proceed alone, avoiding starvation.
        if(out.selected&&sectors+t.sectors>cfg.maxBatchSectors)continue;
        sectors+=t.sectors;out.selectedIds[out.selected++]=t.id;
        out.needsFlush=true;
        if(!t.priority)out.requestIds[out.requests++]=t.id;
        if(sectors>=cfg.maxBatchSectors)break;
    }
    return out;
}
template<class Parent,class Request>
void TextureDependencies(int slot,Parent&& parent,Request&& request){
    // Vanilla TXD domain, bounded depth, and cycle rejection. Parent callback
    // returns -2 for an invalid pool entry, -1 for a valid root.
    std::array<int,8> visited{};
    unsigned count=0;
    while(slot>=0&&slot<5000&&count<visited.size()){
        if(std::find(visited.begin(),visited.begin()+count,slot)!=visited.begin()+count)break;
        const int next=parent(slot);
        if(next<-1)break;
        visited[count++]=slot;request(slot);slot=next;
    }
}
template<class Request,class Flush>
void Execute(const Batch& batch,bool blocking,Request&& request,Flush&& flush){
    for(unsigned i=0;i<batch.requests;++i)request(batch.requestIds[i]);
    if(blocking&&batch.needsFlush)flush();
}
class Cadence {
public:
    bool Due(uint32_t now,unsigned interval)const{
        return !have_||uint32_t(now-last_)>=interval;
    }
    void Complete(uint32_t now){last_=now;have_=true;}
private:
    uint32_t last_=0;bool have_=false;
};
}
