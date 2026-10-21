#pragma once
#include "asc/Core.h"

namespace asc {
// Event counters sum over a CSV interval; gauges describe the latest tick.
class StatsWindow {
public:
    void Add(const Stats& tick) {
        Stats next=tick;
        next.issued+=value_.issued;
        next.promotions+=value_.promotions;
        next.expired+=value_.expired;
        next.denied+=value_.denied;
        next.dynamicIssued+=value_.dynamicIssued;
        next.dynamicDeferred+=value_.dynamicDeferred;
        next.priorityIssued+=value_.priorityIssued;
        value_=next;
    }
    Stats Take() {
        const Stats out=value_;
        value_={};
        return out;
    }
private:
    Stats value_{};
};
}
