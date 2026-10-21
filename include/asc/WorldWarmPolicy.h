#pragma once
#include "asc/WorldCachePolicy.h"
#include <cstdint>
#include <limits>

namespace asc::world_cache {

// A failed page read is retried a finite number of times before the next
// scheduled repair pass. This avoids a busy loop on corrupt/offline archives.
constexpr unsigned MaxImmediateRetries=5;
constexpr uint32_t RetryDelayMs(unsigned failure) noexcept {
    const unsigned bounded=failure<MaxImmediateRetries?failure:MaxImmediateRetries;
    return uint32_t(100u) << bounded;
}
constexpr bool AdvancePage(uint64_t offset,uint64_t size,uint64_t& next) noexcept {
    if(offset>=size || offset>std::numeric_limits<uint64_t>::max()-PageBytes)
        return false;
    next=offset+PageBytes;
    return next<size;
}
constexpr bool CanRepair(uint64_t expectedPages,uint64_t residentPages,
                         uint64_t availableBytes) noexcept {
    // Repairing a manifest larger than the budget causes eternal churn:
    // it constantly evicts pages that were present on earlier passes.
    return expectedPages!=0 && residentPages<expectedPages &&
           expectedPages<=availableBytes/PageBytes;
}
constexpr bool RegisteredSourcesComplete(uint64_t discovered,uint64_t registered,
                                         uint64_t indexedArchives,
                                         uint64_t expectedPages,
                                         uint64_t residentPages) noexcept {
    return discovered!=0 && discovered==registered &&
           indexedArchives==registered && expectedPages!=0 &&
           residentPages==expectedPages;
}
constexpr uint32_t CoveragePercent(uint64_t residentPages,
                                   uint64_t expectedPages) noexcept {
    if(!expectedPages)return 0;
    if(residentPages>=expectedPages)return 100;
    // No overflow even for synthetic / corrupt large page counts.
    return static_cast<uint32_t>(
        (static_cast<long double>(residentPages)*100.0L) /
         static_cast<long double>(expectedPages));
}
}
