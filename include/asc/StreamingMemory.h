#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

namespace asc::memory {
constexpr uint64_t MiB=1048576ULL;
constexpr uint64_t MinimumBudget=64*MiB;
// GTA stores this budget in a uint32_t byte counter. This is a representation
// limit, NOT a fixed 1 GiB or other policy cap. Keep the result 64 KiB-aligned.
// The largest representable aligned value is 0xFFFF0000 (4095.9375 MiB).
constexpr uint64_t MaxRepresentableBudget =
    uint64_t(std::numeric_limits<uint32_t>::max()) & ~uint64_t(65535);
struct Settings {
    bool enabled=true;
    unsigned limitMiB=512; // native stream budget; GPU VRAM is separate
    unsigned reserveMiB=512;
    bool overrideOla=true; // ASC owns budget by default; OLA still owns table expansion
};
constexpr bool Valid(Settings s) {
    return (s.limitMiB==0||(s.limitMiB>=64&&s.limitMiB<=4096))&&
           s.reserveMiB>=64&&s.reserveMiB<=1024;
}
struct Result {
    uint32_t budgetBytes=0;
    uint64_t processLimitBytes=0;
    bool lowAddressSpace=false;
};
// No allocations, system calls, or 32-bit intermediate multiplication.
inline Result Calculate(Settings cfg,bool laa,uint64_t totalVirtual,
                        uint64_t availableVirtual,uint64_t streamingUsed) {
    Result out;
    if(!Valid(cfg)||totalVirtual==0)return out;
    out.processLimitBytes=std::min<uint64_t>(totalVirtual,(laa?4096ULL:2048ULL)*MiB);
    const uint64_t reserve=uint64_t(cfg.reserveMiB)*MiB;
    if(out.processLimitBytes<reserve+MinimumBudget){
        out.lowAddressSpace=true;
        return out; // invalid/impossibly small OS sample; do not write a budget
    }
    const uint64_t available=std::min(availableVirtual,out.processLimitBytes);
    out.lowAddressSpace=available<=reserve;
    // Count current stream allocations as reusable, but retain headroom for
    // code, heaps, stacks, drivers, other mods, and transient loading work.
    const uint64_t used=std::min(streamingUsed,out.processLimitBytes);
    const uint64_t capacity=used+available;
    const uint64_t growthLimit=capacity>reserve?capacity-reserve:0;
    const uint64_t addressLimit=out.processLimitBytes>reserve?
        out.processLimitBytes-reserve:0;
    const uint64_t userLimit=cfg.limitMiB?uint64_t(cfg.limitMiB)*MiB:addressLimit;
    // 4 GiB cannot fit in the game's unsigned byte counter. The only
    // unconditional ceiling is the field's representable 32-bit value.
    // Align down to 64 KiB. Retain a normal minimum budget during address exhaustion;
    // a near-zero budget would break vanilla's budget-minus-file arithmetic.
    const uint64_t budget=std::min({userLimit,addressLimit,growthLimit,
                                  MaxRepresentableBudget});
    out.budgetBytes=uint32_t(std::max(MinimumBudget,budget&~uint64_t(65535)));
    return out;
}
}
