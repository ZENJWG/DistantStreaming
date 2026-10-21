#include "WorldListSafety.h"
#include <cassert>
#include <iostream>

int main(){
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const size_t page=system.dwPageSize;
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*2,
        MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(memory);
    assert(!asc::world::activeRegionCache);
    {
        asc::world::RegionValidationCache cache;
        asc::world::ScopedRegionCache scope(cache);
        for(unsigned i=0;i<4096;++i)
            assert(asc::world::Readable(memory+(i%32)*sizeof(void*),sizeof(void*)));
        assert(cache.Queries()==1&&cache.Hits()==4095);
        assert(!asc::world::Readable(memory+1,sizeof(void*))); // misaligned
        assert(!asc::world::Readable(nullptr,sizeof(void*)));
        {
            asc::world::RegionValidationCache nested;
            asc::world::ScopedRegionCache inner(nested);
            assert(asc::world::Readable(memory,8));
            assert(nested.Queries()==1);
        }
        assert(asc::world::activeRegionCache==&cache);
    }
    assert(!asc::world::activeRegionCache);
    // Pool protection can change between scans. The next scope must query
    // the OS again and cannot reuse the previous positive region.
    DWORD previous=0;
    assert(VirtualProtect(memory+page,page,PAGE_NOACCESS,&previous));
    {
        asc::world::RegionValidationCache cache;
        asc::world::ScopedRegionCache scope(cache);
        assert(asc::world::Readable(memory,8));
        assert(!asc::world::Readable(memory+page,8));
        assert(!asc::world::Readable(memory+page-sizeof(void*),2*sizeof(void*)));
    }
    assert(VirtualFree(memory,0,MEM_RELEASE));
    std::cout<<"PASS: 4096 readable checks use one OS query; nested scopes, "
                 "pointer bounds and protection changes remain validated\n";
}
