#pragma once
#include <cstdint>
#include <limits>

namespace asc::world_cache {
constexpr uint64_t PageBytes=1024ull*1024ull;
// Overflow-safe, including empty archive files and >4 GiB archives.
constexpr uint64_t PageCount(uint64_t bytes) noexcept {
    return bytes/PageBytes + ((bytes%PageBytes)!=0);
}
constexpr bool MayWarm(uint64_t resident,uint64_t pending,uint64_t budget) noexcept {
    if(budget<PageBytes||resident>budget-PageBytes)return false;
    return pending<=budget-PageBytes-resident;
}
constexpr bool MayStartFlight(uint64_t pending) noexcept {
    return pending<8*PageBytes;
}
}
