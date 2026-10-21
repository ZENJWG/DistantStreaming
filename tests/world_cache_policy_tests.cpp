#include "asc/WorldCachePolicy.h"
#include "asc/ArchiveIdentity.h"
#include "asc/NativeMemoryProtocol.h"
#include <cassert>
#include <limits>
#include <unordered_map>

int main(){
    using namespace asc::world_cache;
    static_assert(PageCount(0)==0);
    static_assert(PageCount(1)==1);
    static_assert(PageCount(PageBytes)==1);
    static_assert(PageCount(PageBytes+1)==2);
    static_assert(PageCount(std::numeric_limits<uint64_t>::max())==
                  std::numeric_limits<uint64_t>::max()/PageBytes+1);
    static_assert(!MayWarm(0,0,PageBytes-1));
    static_assert(MayWarm(0,0,PageBytes));
    static_assert(!MayWarm(PageBytes,0,PageBytes));
    static_assert(!MayWarm(PageBytes,PageBytes,2*PageBytes));
    static_assert(MayWarm(PageBytes,0,2*PageBytes));
    static_assert(!MayStartFlight(8*PageBytes));
    static_assert(MayStartFlight(7*PageBytes));
    asc::archive_io::FileIdentity f{1,2,3,0,2048,4,5};
    asc::archive_io::PageKey a{f,0},b{f,1};
    std::unordered_map<asc::archive_io::PageKey,int,asc::archive_io::PageHash> pages;
    pages[a]=1;pages[b]=2;
    assert(pages.size()==2);
    std::unordered_map<asc::archive_io::FileIdentity,int,asc::archive_io::IdentityHash> manifests;
    manifests[f]=3;
    assert(manifests[f]==3);
    static_assert(sizeof(asc::native::CacheCoverage)==88);
}
