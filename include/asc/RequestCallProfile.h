#pragma once
// Bounded, observational RequestModel profiling. Nothing here issues, changes,
// completes, or suppresses an engine request. Model states are sampled later.
#include "asc/StreamingFlowProbe.h"
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace asc::request_profile {
// GTA SA 1.0 resource-streaming IDs (DFF..SCM). Keep the four internal
// linked-list sentinels separate from resources. This is diagnostic-only;
// other/limit-adjuster IDs must never be considered invalid engine calls.
enum class ResourceFamily : unsigned {
    DFF, TXD, COL, IPL, DAT, IFP, RRR, SCM, Internal, Extension, Negative, Count
};
inline constexpr unsigned kResourceFamilyCount =
    static_cast<unsigned>(ResourceFamily::Count);
inline constexpr ResourceFamily Classify(int id) noexcept {
    if(id<0)return ResourceFamily::Negative;
    if(id<20000)return ResourceFamily::DFF;
    if(id<25000)return ResourceFamily::TXD;
    if(id<25255)return ResourceFamily::COL;
    if(id<25511)return ResourceFamily::IPL;
    if(id<25575)return ResourceFamily::DAT;
    if(id<25755)return ResourceFamily::IFP;
    if(id<26230)return ResourceFamily::RRR;
    if(id<26312)return ResourceFamily::SCM;
    if(id<26316)return ResourceFamily::Internal;
    return ResourceFamily::Extension;
}
inline constexpr const char* FamilyName(ResourceFamily c) noexcept {
    switch(c) {
    case ResourceFamily::DFF: return "DFF";
    case ResourceFamily::TXD: return "TXD";
    case ResourceFamily::COL: return "COL";
    case ResourceFamily::IPL: return "IPL";
    case ResourceFamily::DAT: return "DAT";
    case ResourceFamily::IFP: return "IFP";
    case ResourceFamily::RRR: return "RRR";
    case ResourceFamily::SCM: return "SCM";
    case ResourceFamily::Internal: return "internal";
    case ResourceFamily::Extension: return "extension/unclassified";
    case ResourceFamily::Negative: return "negative";
    default: return "unknown";
    }
}
inline constexpr bool StateIndexCandidate(int id) noexcept {
    return id>=0 && id<26312;
}
// Convert aggregated high-resolution counter ticks to nanoseconds without
// overflowing a 64-bit product. Used only at the once-per-second report.
inline uint64_t AverageNanoseconds(uint64_t ticks, uint64_t frequency,
                                   uint64_t count=1) noexcept {
    if(!frequency || !count)return 0;
    return static_cast<uint64_t>(
        (static_cast<long double>(ticks)*1000000000.0L)/
        (static_cast<long double>(frequency)*count));
}
using stream_flow::State;
inline unsigned StateSlot(State s) noexcept {
    switch(s) {
    case State::NotLoaded: return 0;
    case State::Requested: return 1;
    case State::Channeled: return 2;
    case State::Finishing: return 3;
    case State::Loaded: return 4;
    default: return 5;
    }
}
struct Sample {
    uintptr_t caller{};
    int id{};
    uint32_t atMs{};
};
struct Caller {
    uintptr_t address{};
    uint32_t count{}, error{};
};
struct Model {
    int id{-1};
    uint32_t count{},error{};
    std::array<uint32_t,6> states{};
};
struct Snapshot {
    std::array<Caller,24> callers{};
    std::array<Model,32> models{};
    std::array<uint32_t,6> states{};
    std::array<uint32_t,kResourceFamilyCount> families{};
    uint32_t examined{}, invalidIds{}, stateUnavailable{}, trackCapacityMisses{};
    uint32_t completed{},cancelled{},timedOut{},active{};
    uint64_t elapsedTotalMs{};
    uint32_t elapsedPeakMs{};
};

template<std::size_t CallerSlots=24,std::size_t TrackedSlots=128>
class Profiler {
    struct Tracked {int id{-1}; uint32_t from{},lastSeen{};};
    std::array<Caller,CallerSlots> callers_{};
    std::array<Model,32> models_{};
    std::array<Tracked,TrackedSlots> tracked_{};
    Snapshot interval_{};
    static_assert(CallerSlots>0 && TrackedSlots>0);
    void AddCaller(uintptr_t caller) {
        if(!caller)return;
        for(auto& x:callers_)
            if(x.address==caller){++x.count;return;}
        for(auto& x:callers_)
            if(!x.address){x={caller,1,0};return;}
        // Bounded space-saving heavy hitters; counts have an approximation
        // error when replacing a low-frequency address.
        auto* least=&callers_[0];
        for(auto& x:callers_)if(x.count<least->count)least=&x;
        *least={caller,least->count+1,least->count};
    }
    void AddModel(int id,State state) {
        for(auto& x:models_) {
            if(x.id==id) {
                ++x.count; ++x.states[StateSlot(state)];return;
            }
        }
        for(auto& x:models_) {
            if(x.id<0) {
                x={};x.id=id;x.count=1;
                x.states[StateSlot(state)]=1;return;
            }
        }
        auto* least=&models_[0];
        for(auto& x:models_)if(x.count<least->count)least=&x;
        const uint32_t old=least->count;
        *least={};
        least->id=id;least->count=old+1;least->error=old;
        least->states[StateSlot(state)]=1;
    }
    void AddPending(int id,uint32_t now) {
        for(const auto& t:tracked_)if(t.id==id)return; // first sample wins
        for(auto& t:tracked_)if(t.id<0){t={id,now,now};return;}
        ++interval_.trackCapacityMisses;
    }
public:
    void Reset() {
        callers_={};tracked_={};interval_={};
        for(auto& x:models_)x.id=-1;
        for(auto& t:tracked_)t.id=-1;
    }
    Profiler(){Reset();}
    void Record(Sample s,State current,bool valid) {
        ++interval_.examined;
        AddCaller(s.caller);
        const auto family=Classify(s.id);
        ++interval_.families[static_cast<unsigned>(family)];
        if(family==ResourceFamily::Negative) {
            ++interval_.invalidIds;return;
        }
        if(!valid) {++interval_.stateUnavailable;return;}
        ++interval_.states[StateSlot(current)];
        AddModel(s.id,current);
        if(stream_flow::Pending(current))AddPending(s.id,s.atMs);
    }
    template<class Source>
    void Poll(uint32_t now,Source&& stateOf) {
        for(auto& t:tracked_) {
            if(t.id<0)continue;
            const auto age=uint32_t(now-t.from);
            if(age>=30000u) {
                ++interval_.timedOut;t.id=-1;continue;
            }
            const State state=stateOf(t.id);
            if(state==State::Loaded) {
                ++interval_.completed;
                interval_.elapsedTotalMs+=age;
                interval_.elapsedPeakMs=std::max(interval_.elapsedPeakMs,age);
                t.id=-1;
            }else if(state==State::NotLoaded) {
                // Observed transition from an initial pending state; cannot
                // distinguish GTA cancellation from other eviction paths.
                ++interval_.cancelled;t.id=-1;
            }else if(stream_flow::Pending(state)){
                t.lastSeen=now;
            }
        }
    }
    Snapshot Take() {
        auto out=interval_;
        for(std::size_t i=0;i<CallerSlots && i<out.callers.size();++i)
            out.callers[i]=callers_[i];
        std::sort(out.callers.begin(),out.callers.end(),
            [](const Caller&a,const Caller&b){return a.count>b.count;});
        for(std::size_t i=0;i<models_.size();++i)out.models[i]=models_[i];
        std::sort(out.models.begin(),out.models.end(),
            [](const Model&a,const Model&b){return a.count>b.count;});
        for(const auto& t:tracked_)if(t.id>=0)++out.active;
        callers_={};interval_={};
        for(auto& x:models_)x={};
        for(auto& x:models_)x.id=-1;
        return out;
    }
};
}
