#include "asc/ArchiveReadPolicy.h"
#include "asc/ArchiveIdentity.h"
#include <unordered_map>
#include <cassert>
#include <cstdint>
#include <iostream>
int main(){
    using namespace asc::archive_io;
    constexpr uint32_t max=4u*1024u*1024u;
    static_assert(Classify(4096,true,true,false,max)==ReadMode::SynchronousCandidate);
    static_assert(Classify(4096,true,false,true,max)==ReadMode::WarmOnlyOverlapped);
    static_assert(Classify(4096,true,true,true,max)==ReadMode::WarmOnlyOverlapped);
    static_assert(Classify(4096,true,false,false,max)==ReadMode::PassThrough);
    static_assert(Classify(0,true,true,false,max)==ReadMode::PassThrough);
    static_assert(Classify(max+1,true,true,false,max)==ReadMode::PassThrough);
    static_assert(Classify(1,false,true,true,max)==ReadMode::PassThrough);
    static_assert(OverlappedOffset(0x76543210u,0xFEDCBA98u)==0xFEDCBA9876543210ull);
    // Two independent opened source IDs with the same on-disk file identity
    // must hit the same external cache page, while a file rewrite misses.
    using asc::archive_io::FileIdentity;
    using asc::archive_io::PageKey;
    using asc::archive_io::PageHash;
    std::unordered_map<PageKey,int,PageHash> pages;
    FileIdentity identity{42,1,2,0,128,56,78};
    pages.emplace(PageKey{identity,7},1234);
    assert(pages.at(PageKey{identity,7})==1234);
    identity.writeLow++;
    assert(!pages.contains(PageKey{identity,7}));
    identity.writeLow--;
    assert(!pages.contains(PageKey{identity,8}));
    std::cout<<"Archive read policy: synchronous, overlapped, stable cross-source page keys\\n";
}
