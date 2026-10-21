#include "asc/ReadableRegionCache.h"
#include <cassert>
#include <cstdint>

int main() {
    using asc::read_cache::RegionCache;
    RegionCache<2> cache;
    // No region in a new renderer frame is trusted until queried.
    assert(!cache.Covers(0x1008, 8));
    cache.Remember(0x1000, 0x100);
    assert(cache.Covers(0x1008, 8));
    assert(cache.Covers(0x10F0, 0x10));
    assert(!cache.Covers(0x10F0, 0x11));
    assert(!cache.Covers(0, 8));
    assert(!cache.Covers(UINTPTR_MAX-1, 10));
    assert(cache.Hits() == 2);

    cache.Remember(0x2000, 0x100);
    assert(cache.Covers(0x2050, 1));
    // Bounded ring eviction, not a permanent address-space assertion.
    cache.Remember(0x3000, 0x100);
    assert(!cache.Covers(0x1008, 8));
    assert(cache.Covers(0x2008, 8));
    cache.Reset();
    assert(cache.Hits() == 0 && cache.Misses() == 0);
    assert(!cache.Covers(0x2008, 8));
    // Overflow cannot populate a cache entry.
    cache.Remember(UINTPTR_MAX-1, 32);
    assert(!cache.Covers(UINTPTR_MAX-1, 1));
}
