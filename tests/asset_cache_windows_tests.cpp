#include "AssetCache.h"
#include "asc/WorldCachePolicy.h"
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdlib>
#define CHECK(condition) do { if(!(condition)) std::abort(); } while(0)
#include <thread>
#include <vector>

// Only built on Windows/MSVC x64. This exercises real overlapped files and
// concurrent warm/demand calls rather than mocking the helper's cache.
int wmain(){
    wchar_t dir[MAX_PATH]{},path[MAX_PATH]{};
    CHECK(GetTempPathW(MAX_PATH,dir));
    CHECK(GetTempFileNameW(dir,L"ASC",0,path));
    HANDLE writer=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,
        CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    CHECK(writer!=INVALID_HANDLE_VALUE);
    const auto page=asc::world_cache::PageBytes;
    std::vector<unsigned char> block(static_cast<size_t>(page),0x5a);
    for(int i=0;i<3;++i){
        DWORD written=0;
        CHECK(WriteFile(writer,block.data(),DWORD(block.size()),&written,nullptr));
        CHECK(written==DWORD(block.size()));
    }
    FlushFileBuffers(writer);
    AssetCache cache;
    auto registerHandle=[&](uint64_t id,bool warm){
        HANDLE copy=INVALID_HANDLE_VALUE;
        CHECK(DuplicateHandle(GetCurrentProcess(),writer,GetCurrentProcess(),
               &copy,0,FALSE,DUPLICATE_SAME_ACCESS));
        CHECK(cache.Open(id,copy,warm));
    };
    registerHandle(1,false);
    registerHandle(2,true);
    uint64_t sourceSize=0;
    CHECK(cache.SourceSize(2,sourceSize)&&sourceSize==3*page);
    auto coverage=cache.Coverage();
    CHECK(coverage.archives==1&&coverage.expectedPages==3);
    constexpr uint64_t budget=4*asc::world_cache::PageBytes;
    std::atomic<int> errors{0};
    std::thread warming([&]{
        for(uint64_t i=0;i<3;++i)
            if(!cache.WarmOnePage(2,i*page,budget))++errors;
    });
    std::thread demand([&]{
        for(int i=0;i<200;++i){
            std::array<unsigned char,256> bytes{};
            DWORD done=0;
            bool ok=false;
            for(int attempt=0;attempt<5&&!ok;++attempt){
                done=0;
                ok=cache.Read(1,page-100,DWORD(bytes.size()),
                              bytes.data(),done,budget)&&done==bytes.size();
                if(!ok)Sleep(20);
            }
            if(!ok) {++errors;continue;}
            for(const auto b:bytes)if(b!=0x5a){++errors;break;}
        }
    });
    warming.join();demand.join();
    CHECK(errors==0);
    coverage=cache.Coverage();
    CHECK(coverage.residentPages==3);
    // Change the file and its timestamp: the old page key must not be served
    // for a new source generation, even while other cached pages are resident.
    LARGE_INTEGER origin{};
    CHECK(SetFilePointerEx(writer,origin,nullptr,FILE_BEGIN));
    const unsigned char altered=0x67;
    DWORD rewritten=0;
    CHECK(WriteFile(writer,&altered,1,&rewritten,nullptr)&&rewritten==1);
    CHECK(FlushFileBuffers(writer));
    FILETIME stamp{};
    CHECK(GetFileTime(writer,nullptr,nullptr,&stamp));
    ULARGE_INTEGER later{};
    later.LowPart=stamp.dwLowDateTime;
    later.HighPart=stamp.dwHighDateTime;
    later.QuadPart+=10'000'000ull;
    FILETIME changed{later.LowPart,later.HighPart};
    CHECK(SetFileTime(writer,nullptr,nullptr,&changed));
    unsigned char check=0;
    DWORD checked=0;
    CHECK(cache.Read(1,0,1,&check,checked,budget));
    CHECK(checked==1&&check==altered);
    CHECK(cache.WarmOnePage(2,0,budget));
    // A retained 1 MiB page can be shared across source IDs.
    cache.Close(1);
    auto stats=cache.Summary();
    CHECK(stats.residentBytes>=3*page && stats.residentBytes<=budget);
    CHECK(stats.gameBytes>=256);
    cache.Trim(page);
    CHECK(cache.Summary().residentBytes<=page);
    // Simulate a later repair sweep after demand/LRU eviction.
    for(uint64_t i=0;i<3;++i)
        CHECK(cache.WarmOnePage(2,i*page,budget));
    CHECK(cache.Coverage().residentPages==3);
    cache.Close(2);
    CHECK(cache.Coverage().archives==0);
    CloseHandle(writer);
    DeleteFileW(path);
    return 0;
}
