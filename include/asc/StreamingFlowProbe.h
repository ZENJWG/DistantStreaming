#pragma once
// Portable, observational diagnostics. Does not issue, cancel or complete GTA
// streaming requests. A state sweep is NOT an instantaneous engine snapshot.
#include <array>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <utility>

namespace asc::stream_flow {

enum class State : uint8_t {
    NotLoaded=0, Requested=1, Channeled=2, Finishing=3,
    Loaded=4, Other=5, Unseen=255
};

inline constexpr bool Pending(State state) noexcept {
    return state==State::Requested || state==State::Channeled ||
           state==State::Finishing;
}

struct Sweep {
    uint64_t number{};
    uint32_t inspected{};
    uint32_t notLoaded{},requested{},channeled{},finishing{},loaded{},other{};
    uint32_t observedCompleted{},observedEvicted{},observedCancelled{};
};

template<std::size_t Capacity>
class StateSweep {
    static_assert(Capacity>0);
    std::array<State,Capacity> previous_{};
    std::size_t cursor_{};
    Sweep accumulating_{},completed_{};
    bool hasCompleted_{false};
public:
    StateSweep() { previous_.fill(State::Unseen); }
    std::size_t Cursor() const {return cursor_;}
    bool HasCompleted() const {return hasCompleted_;}
    const Sweep& Completed() const {return completed_;}
    void Reset() {
        previous_.fill(State::Unseen);
        cursor_=0;
        accumulating_={};
        completed_={};
        hasCompleted_=false;
    }

    // Source is called exactly once per examined ID. Budget clamps to total,
    // so an untrusted setting cannot force an unbounded game-thread sweep.
    template<class Source>
    bool Sample(std::size_t total,std::size_t budget,Source&& source) {
        if(total==0||total>Capacity||budget==0)return false;
        if(cursor_>=total)cursor_=0;
        const std::size_t count=std::min(budget,total-cursor_);
        for(std::size_t n=0;n<count;++n) {
            const auto id=cursor_++;
            State current=source(id);
            const auto prev=previous_[id];
            previous_[id]=current;
            ++accumulating_.inspected;
            switch(current) {
            case State::NotLoaded: ++accumulating_.notLoaded; break;
            case State::Requested: ++accumulating_.requested; break;
            case State::Channeled: ++accumulating_.channeled; break;
            case State::Finishing: ++accumulating_.finishing; break;
            case State::Loaded: ++accumulating_.loaded; break;
            default: ++accumulating_.other; break;
            }
            // These are sampled transitions; short-lived states may be missed.
            if(Pending(prev)&&current==State::Loaded)
                ++accumulating_.observedCompleted;
            if(prev==State::Loaded&&current==State::NotLoaded)
                ++accumulating_.observedEvicted;
            if(Pending(prev)&&current==State::NotLoaded)
                ++accumulating_.observedCancelled;
        }
        if(cursor_<total)return false;
        cursor_=0;
        completed_=accumulating_;
        // Number is persistent across sweep resets.
        completed_.number=accumulating_.number+1;
        accumulating_={};
        accumulating_.number=completed_.number;
        hasCompleted_=true;
        return true;
    }
};

struct BacklogInterval {
    uint32_t last{},peak{},low{},priorityLast{};
    uint32_t samples{},increases{},decreases{},unchanged{};
    uint32_t lastIncrease{},lastDecrease{};
};
class BacklogMeter {
    BacklogInterval interval_{};
    bool seeded_{false};
    uint32_t previous_{};
public:
    void Add(uint32_t now,uint32_t backlog,uint32_t priority) {
        if(!interval_.samples) {
            interval_.low=interval_.peak=backlog;
        } else {
            interval_.low=std::min(interval_.low,backlog);
            interval_.peak=std::max(interval_.peak,backlog);
        }
        if(seeded_) {
            if(backlog>previous_) {++interval_.increases;interval_.lastIncrease=now;}
            else if(backlog<previous_) {++interval_.decreases;interval_.lastDecrease=now;}
            else ++interval_.unchanged;
        }
        seeded_=true;
        previous_=backlog;
        ++interval_.samples;
        interval_.last=backlog;
        interval_.priorityLast=priority;
    }
    BacklogInterval Take() {
        auto out=interval_;
        interval_={};
        return out;
    }
};

struct StageCounter {
    uint64_t totalUs{},peakUs{},calls{};
    void Add(uint64_t us) {
        totalUs+=us;
        peakUs=std::max(peakUs,us);
        ++calls;
    }
    uint64_t Average() const {return calls?totalUs/calls:0;}
};

} // namespace asc::stream_flow
