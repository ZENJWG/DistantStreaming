#pragma once
// Conservative read guard for GTA SA world-sector linked lists.
// A guard limits crashes from obviously invalid pointers; it cannot prove
// the lifetime or dynamic type of a concurrently-mutated game object.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
#include "asc/ReadableRegionCache.h"
#include <cstddef>
#include <cstdint>

namespace asc::world {
inline std::atomic<uint32_t> rejectedNodes{0};
inline std::atomic<uint32_t> rejectedEntities{0};
// Scoped to one read-only world traversal. Never retain regions across ticks
// or across native loading, instancing or deletion calls that may remap pools.
// Thread-local: other game/ASI threads do not share these cached checks.
struct RegionValidationCache {
    asc::read_cache::RegionCache<32> entries{};
    void Reset() noexcept { entries.Reset(); }
    uint64_t Hits() const noexcept { return entries.Hits(); }
    uint64_t Queries() const noexcept { return entries.Misses(); }
};
inline thread_local RegionValidationCache* activeRegionCache = nullptr;
class ScopedRegionCache {
    RegionValidationCache* previous_{};
public:
    explicit ScopedRegionCache(RegionValidationCache& cache) noexcept
        : previous_(activeRegionCache) { activeRegionCache=&cache; }
    ~ScopedRegionCache() { activeRegionCache=previous_; }
    ScopedRegionCache(const ScopedRegionCache&)=delete;
    ScopedRegionCache& operator=(const ScopedRegionCache&)=delete;
};
inline bool Readable(const void* ptr, size_t count) noexcept {
    const auto addr=reinterpret_cast<uintptr_t>(ptr);
    if(addr<0x10000 || (addr & (alignof(void*)-1)) || count==0 ||
       count>static_cast<size_t>(UINTPTR_MAX-addr))
        return false;
    // One-frame positive-only region cache. Still reject malformed pointers,
    // and never retain object/node pointers or skip model/type/scan-code checks.
    if(activeRegionCache && activeRegionCache->entries.Covers(addr,count))
        return true;
    MEMORY_BASIC_INFORMATION mbi{};
    if(VirtualQuery(ptr,&mbi,sizeof(mbi))!=sizeof(mbi) ||
       mbi.State!=MEM_COMMIT || (mbi.Protect&(PAGE_GUARD|PAGE_NOACCESS)))
        return false;
    const DWORD p=mbi.Protect&0xff;
    if(p!=PAGE_READONLY && p!=PAGE_READWRITE && p!=PAGE_WRITECOPY &&
       p!=PAGE_EXECUTE_READ && p!=PAGE_EXECUTE_READWRITE &&
       p!=PAGE_EXECUTE_WRITECOPY)
        return false;
    const auto begin=reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    if(mbi.RegionSize>UINTPTR_MAX-begin)return false;
    const auto end=begin+mbi.RegionSize;
    if(addr<begin || end<addr || count>end-addr)return false;
    if(activeRegionCache)
        activeRegionCache->entries.Remember(begin,mbi.RegionSize);
    return true;
}
}
