#pragma once
#include <cstdint>

namespace asc::native_residency {
// Raw source pages remain in the x64 helper; decoded RenderWare objects cannot.
// This policy only authorizes GTA's own removal of stale, unreferenced objects.
struct Limits {
    unsigned beginHeadroomMiB=256;
    unsigned stopHeadroomMiB=512;
    unsigned minimumHelperCachedMiB=64;
    unsigned maxBacklog=24;
};
constexpr uint64_t MiB=1048576ULL;
inline bool Update(bool previous,const Limits& limits,bool helperConnected,
                   uint64_t helperCachedBytes,uint64_t used,uint64_t budget,
                   unsigned backlog) noexcept {
    if(!helperConnected || helperCachedBytes <
            uint64_t(limits.minimumHelperCachedMiB)*MiB ||
       !budget || backlog>limits.maxBacklog)
        return false;
    const uint64_t headroom=budget>used?budget-used:0;
    return previous ?
        headroom<uint64_t(limits.stopHeadroomMiB)*MiB :
        headroom<uint64_t(limits.beginHeadroomMiB)*MiB;
}
inline bool UnloadCandidate(unsigned refs,bool loaded,unsigned flags,
                            bool requestedRecently,unsigned sectors,
                            unsigned maxSectors) noexcept {
    return refs==0 && loaded && flags==0 && !requestedRecently &&
        sectors>0 && sectors<=maxSectors;
}
inline bool Stable(uint32_t firstSeen,uint32_t now,unsigned stableMs) noexcept {
    return firstSeen!=0 && uint32_t(now-firstSeen)>=stableMs;
}
}
