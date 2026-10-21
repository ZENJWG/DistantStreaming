#pragma once
#include "asc/Core.h"
#include <atomic>
#include <array>
#include <bitset>
#include <cmath>

namespace asc::offscreen {
struct Settings {
    bool enabled=true,unloadModels=true,unloadTextures=true,disableBlockingLoads=true;
    bool helperFirstTrim=false;
    unsigned trimBeginHeadroomMiB=256,trimStopHeadroomMiB=512;
    unsigned trimMinHelperCachedMiB=64,trimMaxBacklog=24;
    unsigned trimScanPerPass=256,trimStableMs=5000,trimIntervalMs=200;
    unsigned maxScanPerFrame=512,maxDeletesPerFrame=4,maxPressureDeletes=8;
    unsigned maxUnloadsPerFrame=1,timeBudgetUs=1000,graceMs=250,requestQuietMs=1000;
    unsigned maxUnloadSectors=4096;
    float keepNearUnits=80;
};
inline bool Valid(const Settings& c){
    return c.maxScanPerFrame>=32&&c.maxScanPerFrame<=4096
        &&c.maxDeletesPerFrame>=1&&c.maxDeletesPerFrame<=32
        &&c.maxPressureDeletes>=c.maxDeletesPerFrame&&c.maxPressureDeletes<=64
        &&c.maxUnloadsPerFrame<=4&&c.timeBudgetUs>=100&&c.timeBudgetUs<=4000
        &&c.graceMs<=5000&&c.requestQuietMs>=250&&c.requestQuietMs<=10000
        &&c.maxUnloadSectors>=1&&c.maxUnloadSectors<=65536
        &&c.keepNearUnits>=25&&c.keepNearUnits<=250
        &&c.trimBeginHeadroomMiB>=32&&c.trimBeginHeadroomMiB<=1024
        &&c.trimStopHeadroomMiB>c.trimBeginHeadroomMiB&&c.trimStopHeadroomMiB<=1536
        &&c.trimMinHelperCachedMiB<=8192
        &&c.trimMaxBacklog<=256
        &&c.trimScanPerPass>=16&&c.trimScanPerPass<=1024
        &&c.trimStableMs>=1000&&c.trimStableMs<=60000
        &&c.trimIntervalMs>=50&&c.trimIntervalMs<=5000;
}
struct Stamp {
    uintptr_t object=0;
    uint32_t since=0;
    int model=-1;
    unsigned generation=0;
    bool offscreen=false;
    bool Observe(uintptr_t token,int id,unsigned gen,bool outside,uint32_t now,unsigned grace){
        if(!outside){offscreen=false;return false;}
        if(!offscreen||object!=token||model!=id||generation!=gen){
            object=token;model=id;generation=gen;since=now;offscreen=true;
        }
        return uint32_t(now-since)>=grace;
    }
};
struct Protection {
    bool onScreen=false,nearPlayer=false,rendering=false,pinned=false;
    bool staticWorld=true,hasObject=true,validModel=true;
};
inline bool CanDelete(const Protection& p){
    return p.staticWorld&&p.hasObject&&p.validModel&&!p.onScreen&&!p.nearPlayer&&!p.rendering&&!p.pinned;
}
inline bool CanUnload(unsigned references,bool loaded,bool protectedFlags,
                      bool recentRequest,unsigned sectors,const Settings& cfg){
    return cfg.unloadModels&&references==0&&loaded&&!protectedFlags&&!recentRequest
        &&sectors>0&&sectors<=cfg.maxUnloadSectors;
}
// Request hooks update atomics only; the cleanup consumer never guesses that
// a recently requested but not-yet-instanced model is safe to evict.
template<unsigned Count>
class RecentRequests {
public:
    void Record(int id,uint32_t now){
        if(id>=0&&unsigned(id)<Count)
            times_[id].store((uint64_t(now)<<1)|1,std::memory_order_relaxed);
    }
    bool Recent(int id,uint32_t now,unsigned quietMs)const{
        if(id<0||unsigned(id)>=Count)return false;
        const auto stamp=times_[id].load(std::memory_order_relaxed);
        return stamp&&uint32_t(now-uint32_t(stamp>>1))<quietMs;
    }
private:
    std::atomic<uint64_t> times_[Count]{};
};
class PendingModels {
public:
    void Add(int id){
        if(id<0||id>=int(kVanillaModelInfoCountCap)||pending_.test(unsigned(id)))return;
        ids_[(head_+count_)%ids_.size()]=id;++count_;pending_.set(unsigned(id));
    }
    int Pop(){
        if(!count_)return -1;
        const int id=ids_[head_];head_=(head_+1)%unsigned(ids_.size());--count_;
        pending_.reset(unsigned(id));return id;
    }
    unsigned Size()const{return count_;}
    void Clear(){pending_.reset();head_=count_=0;}
private:
    std::array<int,kVanillaModelInfoCountCap> ids_{};
    std::bitset<kVanillaModelInfoCountCap> pending_;
    unsigned head_=0,count_=0;
};
}
