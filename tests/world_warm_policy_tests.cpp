#include "asc/WorldWarmPolicy.h"
#include <cassert>
#include <cstdint>
#include <limits>
using namespace asc::world_cache;

int main() {
    static_assert(MaxImmediateRetries==5);
    static_assert(RetryDelayMs(0)==100);
    static_assert(RetryDelayMs(1)==200);
    static_assert(RetryDelayMs(5)==3200);
    static_assert(RetryDelayMs(99)==3200);
    uint64_t next=0;
    assert(AdvancePage(0,PageBytes*2,next) && next==PageBytes);
    assert(!AdvancePage(PageBytes,PageBytes*2,next));
    assert(!AdvancePage(std::numeric_limits<uint64_t>::max()-3,
                        std::numeric_limits<uint64_t>::max(),next));
    assert(CanRepair(3,2,4*PageBytes));
    assert(!CanRepair(5,2,4*PageBytes));
    assert(!CanRepair(3,3,4*PageBytes));
    assert(!CanRepair(0,0,4*PageBytes));
    assert(RegisteredSourcesComplete(2,2,2,5,5));
    assert(!RegisteredSourcesComplete(2,1,1,5,5));
    assert(!RegisteredSourcesComplete(2,2,2,5,4));
    assert(!RegisteredSourcesComplete(0,0,0,0,0));
    assert(CoveragePercent(0,0)==0);
    assert(CoveragePercent(1,3)==33);
    assert(CoveragePercent(2,3)==66);
    assert(CoveragePercent(4,3)==100);
    assert(CoveragePercent(UINT64_MAX/2,UINT64_MAX)==49 ||
           CoveragePercent(UINT64_MAX/2,UINT64_MAX)==50);
    return 0;
}
