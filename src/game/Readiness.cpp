#include "Readiness.h"
#include <CStreaming.h>
#include <CStreamingInfo.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <windows.h>

namespace readiness {
namespace {
// GTA calls RequestModel for both model and non-model resource ids.
// Recording an id must not require dereferencing Plugin-SDK model tables.
constexpr int kMaxObservedId=20000;
bool validId(int id) noexcept {
    return id>=0 && id<kMaxObservedId;
}

// Diagnostic-only: read the game streaming state through a checked OS copy
// rather than following the plugin SDK's game pointer in ordinary C++.
// This can still fail if another mod changes the streaming array's layout;
// failures drop telemetry for that id, without taking down the game.
bool TryGetLoadState(int id, uint8_t& state) noexcept {
    if(!validId(id))return false;
    // Read from the live array-base global (0x9654B4 in SA 1.0 US).
    // Plugin-SDK's CStreaming::ms_aInfoForModel is initialized from an
    // instruction immediate in gta_sa.exe and can become stale/invalid if
    // another mod rewrites that instruction before the ASI initializes.
    const auto* base=CStreamingInfo::ms_pArrayBase;
    const uintptr_t pointer=reinterpret_cast<uintptr_t>(base);
    if(pointer<0x10000u)return false;
    constexpr size_t kOffset=offsetof(CStreamingInfo,m_nLoadState);
    const uint64_t address=uint64_t(pointer)+
        uint64_t(id)*sizeof(CStreamingInfo)+kOffset;
    using StateType=decltype(((CStreamingInfo*)nullptr)->m_nLoadState);
    // LAA arrays may legitimately live above 2 GiB. Check arithmetic against
    // the actual pointer width; ReadProcessMemory validates accessibility.
    if(address>uint64_t(UINTPTR_MAX)-sizeof(StateType)+1)return false;
    StateType result{};
    SIZE_T copied=0;
    if(!ReadProcessMemory(GetCurrentProcess(),
        reinterpret_cast<LPCVOID>(uintptr_t(address)),
        &result,sizeof(result),&copied) || copied!=sizeof(result))
        return false;
    state=static_cast<uint8_t>(result);
    return true;
}
uint32_t percentile(std::vector<uint32_t>&values,unsigned num){
    if(values.empty())return 0;
    const size_t index=(values.size()-1)*num/100;
    std::nth_element(values.begin(),values.begin()+index,values.end());
    return values[index];
}
}
void Tracker::OnRequest(int id,uint32_t now,ModelClass type){
    if(!validId(id))return;
    // Request observation never dereferences GTA state. This is important:
    // previous builds crashed at readiness::Tracker::OnRequest+0x42 while
    // inspecting the model-info/streaming tables.
    if(active_.size()>=512)return;
    active_.try_emplace(id,Entry{now,0,type});
}
void Tracker::Sample(uint32_t now){
    for(auto it=active_.begin();it!=active_.end();) {
        uint8_t state=0;
        if(!TryGetLoadState(it->first,state)){
            it=active_.erase(it);continue;
        }
        auto& e=it->second;
        const uint32_t elapsed=uint32_t(now-e.requestedAt);
        if(!e.channeledAt &&
           (state==LOADSTATE_Channeled||state==LOADSTATE_Finishing||
            state==LOADSTATE_LOADED))
            e.channeledAt=now;
        if(state==LOADSTATE_LOADED){
            times_.push_back(elapsed);
            if(e.channeledAt)
                queueTimes_.push_back(uint32_t(e.channeledAt-e.requestedAt));
            ++aggregate_.loaded;
            if(e.type==ModelClass::Ped)++aggregate_.pedReady;
            if(e.type==ModelClass::Vehicle)++aggregate_.vehicleReady;
            it=active_.erase(it);continue;
        }
        if(elapsed>15000||state==LOADSTATE_NOT_LOADED) {
            if(elapsed>250)++aggregate_.timedOut;
            it=active_.erase(it);continue;
        }
        aggregate_.maxPendingMs=std::max(aggregate_.maxPendingMs,elapsed);
        ++it;
    }
    aggregate_.pending=unsigned(active_.size());
}
Summary Tracker::TakeSummary(){
    Summary v=aggregate_;
    v.p50Ms=percentile(times_,50);
    v.p95Ms=percentile(times_,95);
    v.queueP95Ms=percentile(queueTimes_,95);
    aggregate_=Summary{};
    aggregate_.pending=unsigned(active_.size());
    times_.clear();queueTimes_.clear();
    return v;
}
void Tracker::Reset(){
    active_.clear();times_.clear();queueTimes_.clear();
    aggregate_=Summary{};
}
}
