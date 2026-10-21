#pragma once
#include <cstdint>

namespace asc::archive {
// These functions are intentionally pure, with no GTA or Windows dependency.
// The profiler always calls vanilla exactly once, returning its exact value.
constexpr bool ValidSampleRate(unsigned n) {
    return n>=1 && n<=1024 && (n&(n-1))==0;
}
constexpr bool ShouldSample(uint32_t sequence,unsigned everyN) {
    return ValidSampleRate(everyN) &&
           (sequence & (everyN-1))==0;
}
constexpr unsigned LatencyBin(uint64_t us) {
    return us<25?0:us<50?1:us<100?2:us<250?3:
           us<500?4:us<1000?5:us<2500?6:7;
}
constexpr unsigned kBinCount=8;
} // namespace asc::archive
