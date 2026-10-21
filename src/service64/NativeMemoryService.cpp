#include "NativeMemoryService.h"
#include "asc/NativeMemoryProtocol.h"
#include "AssetCache.h"
#include "asc/WorldWarmPolicy.h"
#include "asc/WorldCatalog.h"
#include <filesystem>
#include <windows.h>
#include <unordered_map>
#include <memory>
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <thread>
#include <deque>
#include <mutex>
#include <chrono>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>

namespace {
struct Mapping {
    HANDLE handle=nullptr;
    void* view=nullptr;
    uint64_t bytes=0;
    ~Mapping(){if(view)UnmapViewOfFile(view);if(handle)CloseHandle(handle);}
};
// Every wait is interruptible by parent exit, including an idle/malformed client.
bool Transfer(HANDLE pipe,HANDLE parent,void* data,DWORD size,bool write,
              DWORD timeout=asc::native::RpcTimeoutMs){
    auto* next=static_cast<unsigned char*>(data);
    while(size){
        OVERLAPPED ov{};ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!ov.hEvent)return false;
        DWORD done=0;
        BOOL ok=write?WriteFile(pipe,next,size,&done,&ov):ReadFile(pipe,next,size,&done,&ov);
        if(!ok&&GetLastError()==ERROR_IO_PENDING){
            HANDLE waits[]={parent,ov.hEvent};
            if(WaitForMultipleObjects(2,waits,FALSE,timeout)==WAIT_OBJECT_0+1)
                ok=GetOverlappedResult(pipe,&ov,&done,FALSE);
            else {
                CancelIoEx(pipe,&ov);
                GetOverlappedResult(pipe,&ov,&done,TRUE);
                ok=FALSE;
            }
        }
        CloseHandle(ov.hEvent);
        if(!ok||!done)return false;
        next+=done;size-=done;
    }
    return true;
}
}
int NativeMemoryService(int argc,wchar_t** argv){
    if(argc!=5)return 2; // --asset-cache <parent-pid> <session> <budget-MiB>
    uint64_t parentId=0,budgetMiB=0;
    try {
        size_t end=0;
        parentId=std::stoull(argv[2],&end);
        if(end!=wcslen(argv[2])||!parentId||parentId>MAXDWORD)return 2;
        budgetMiB=std::stoull(argv[4],&end);
        if(end!=wcslen(argv[4])||budgetMiB<128||budgetMiB>32768)return 2;
    }catch(const std::exception&){return 2;}
    const std::wstring session=argv[3];
    if(session.empty()||session.size()>64||session.find_first_not_of(L"0123456789abcdef-")!=std::wstring::npos)
        return 2;
    HANDLE parent=OpenProcess(SYNCHRONIZE|PROCESS_DUP_HANDLE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(parentId));
    if(!parent)return 3;
    std::unordered_map<uint64_t,std::unique_ptr<Mapping>> resources;
    AssetCache assets;
    std::atomic<uint64_t> used{0};
    const uint64_t budget=budgetMiB*asc::native::MiB;
    assets.SetCapacity(budget);
    HANDLE pipe=CreateNamedPipeW(asc::native::PipeName(session).c_str(),
        PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,
        1,4096,4096,0,nullptr);
    if(pipe==INVALID_HANDLE_VALUE){CloseHandle(parent);return 4;}

    // Warm-up is best effort. All source bytes stay in the 64-bit process,
    // while demand reads retain priority on the single IPC connection.
    struct WarmJob {
        uint64_t id=0;
        uint64_t offset=0;
        unsigned retries=0;
        std::chrono::steady_clock::time_point notBefore{};
        // Zero means legacy whole-source warming. A bounded IMG member job
        // only visits pages intersecting [offset,endExclusive).
        uint64_t endExclusive=0;
        bool transient=false; // close source after final page/failure
    };
    std::mutex warmMutex;
    std::deque<WarmJob> warmQueue;
    std::unordered_set<uint64_t> warmSources;
    auto lastWarmRegistration=std::chrono::steady_clock::now();
    std::atomic<bool> warmStop{false};
    std::thread warmThread([&]{
        SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_BEGIN);
        using Clock=std::chrono::steady_clock;
        constexpr auto sweepInterval=std::chrono::seconds(60);
        auto lastSweep=Clock::now();
        auto lastPressureCheck=Clock::now()-std::chrono::milliseconds(250);
        bool memoryAllowsWarming=false;
        while(!warmStop.load()&&WaitForSingleObject(parent,0)==WAIT_TIMEOUT){
            const auto now=Clock::now();
            // Demand-only sessions have no warm jobs. Reclaim expendable
            // source pages even when the queue is empty or a job is delayed.
            // Rate-limit the OS query and trim to one check per 250 ms.
            if(now-lastPressureCheck>=std::chrono::milliseconds(250)){
                lastPressureCheck=now;
                MEMORYSTATUSEX mem{};mem.dwLength=sizeof(mem);
                const bool measured=GlobalMemoryStatusEx(&mem)!=FALSE;
                memoryAllowsWarming=measured &&
                    mem.ullAvailPhys>=2ull*1024ull*asc::native::MiB;
                if(measured && !memoryAllowsWarming){
                    const auto resident=assets.Summary().residentBytes;
                    const uint64_t release=32ull*asc::native::MiB;
                    assets.Trim(resident>release?resident-release:0);
                }
            }
            WarmJob job{};
            bool hasJob=false;
            {
                std::lock_guard guard(warmMutex);
                if(!warmQueue.empty()){
                    job=warmQueue.front();
                    warmQueue.pop_front();
                    hasJob=true;
                }
            }
            if(!hasJob){
                // Repair missing pages after a complete warm-up (or after GTA
                // demand reads have evicted some). Never run repair when the
                // registered archives cannot all fit the remaining budget.
                if(now-lastSweep>=sweepInterval){
                    lastSweep=now;
                    const auto coverage=assets.Coverage();
                    const uint64_t available=budget>used.load()?budget-used.load():0;
                    if(asc::world_cache::CanRepair(
                           coverage.expectedPages,coverage.residentPages,available)){
                        std::lock_guard guard(warmMutex);
                        if(now-lastWarmRegistration>=std::chrono::seconds(10)){
                            for(auto id:warmSources)
                                warmQueue.push_back(WarmJob{id,0,0,now});
                        }
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            if(job.notBefore>now){
                {
                    std::lock_guard guard(warmMutex);
                    warmQueue.push_back(job);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            if(!memoryAllowsWarming){
                job.notBefore=now+std::chrono::milliseconds(250);
                {
                    std::lock_guard guard(warmMutex);
                    warmQueue.push_back(job);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            uint64_t sourceBytes=0;
            if(!assets.SourceSize(job.id,sourceBytes)){
                // Do not retain a dead transient source ID indefinitely.
                if(job.transient)assets.Close(job.id);
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
                continue;
            }
            if(job.offset>=sourceBytes ||
               (job.endExclusive && job.offset>=job.endExclusive)){
                if(job.transient)assets.Close(job.id);
                continue;
            }
            const uint64_t live=used.load();
            const uint64_t remaining=budget>live?budget-live:0;
            const bool warmed=assets.WarmOnePage(job.id,job.offset,remaining);
            if(warmed){
                // Overflow-safe advance; SourceSize tells us when to stop.
                uint64_t next=0;
                if(asc::world_cache::AdvancePage(job.offset,sourceBytes,next) &&
                   (!job.endExclusive || next<job.endExclusive)){
                    job.offset=next;
                    job.retries=0;
                    std::lock_guard guard(warmMutex);
                    warmQueue.push_back(job);
                }else if(job.transient)assets.Close(job.id);
            }else if(++job.retries<=5){
                // Temporary I/O, capacity and in-flight failures should not
                // permanently terminate a warm archive on its first miss.
                job.notBefore=Clock::now()+
                    std::chrono::milliseconds(
                        asc::world_cache::RetryDelayMs(job.retries));
                std::lock_guard guard(warmMutex);
                warmQueue.push_back(job);
            }else if(job.transient)assets.Close(job.id);
            // Failures beyond five attempts remain measurable in Coverage().
            // Periodic repair can retry, without an unbounded hot retry loop.
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_END);
    });
    // Built only on explicit opt-in from the ASI. No catalog disk scanning
    // occurs on the GTA process/thread or on the synchronous pipe loop.
    std::shared_ptr<const asc::world_catalog::Catalog> catalog;
    std::mutex catalogMutex;
    std::thread catalogThread;
    std::atomic<bool> catalogScanning{false};
    bool catalogStarted=false;
    uint64_t queryCell=0,queryGeneration=0;
    size_t queryCursor=0;
    bool stop=false;
    while(!stop&&WaitForSingleObject(parent,0)==WAIT_TIMEOUT){
        OVERLAPPED ov{};ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!ov.hEvent)break;
        BOOL connected=ConnectNamedPipe(pipe,&ov);
        if(!connected){
            const DWORD error=GetLastError();
            if(error==ERROR_PIPE_CONNECTED)connected=TRUE;
            else if(error==ERROR_IO_PENDING){
                HANDLE waits[]={parent,ov.hEvent};
                if(WaitForMultipleObjects(2,waits,FALSE,INFINITE)==WAIT_OBJECT_0+1){
                    DWORD done=0;connected=GetOverlappedResult(pipe,&ov,&done,FALSE);
                }else {
                    CancelIoEx(pipe,&ov);DWORD done=0;
                    GetOverlappedResult(pipe,&ov,&done,TRUE);
                }
            }
        }
        CloseHandle(ov.hEvent);
        if(!connected)break;
        // Keep one verified connection open across all RPCs. Resource
        // allocation no longer competes for the single named-pipe instance.
        ULONG clientId=0;
        if(!GetNamedPipeClientProcessId(pipe,&clientId)||clientId!=parentId){
            DisconnectNamedPipe(pipe);
            continue;
        }
        while(!stop&&WaitForSingleObject(parent,0)==WAIT_TIMEOUT){
            asc::native::Request req{};
            asc::native::Reply reply{};
            // Idle waiting is interruptible by parent exit, not a 5-second
            // forced disconnect. Payload read/write remains time-bounded.
            if(!Transfer(pipe,parent,&req,sizeof(req),false,INFINITE))break;
            if(req.magic!=asc::native::Magic||req.version!=asc::native::Version)break;
            using asc::native::Command;
            asc::native::ObservedImgMemberWarm observedImg{};
            if(req.command==Command::OpenWarmImgMember &&
               !Transfer(pipe,parent,&observedImg,sizeof(observedImg),false))
                break;
            if(req.command==Command::Allocate&&req.id&&req.bytes&&
               req.bytes<=asc::native::MaxResource&&req.bytes<=budget-used&&
               !resources.contains(req.id)){
                // Do not evict valid warm pages until a new GPU backing
                // allocation has actually succeeded.
                auto mapping=std::make_unique<Mapping>();
                mapping->bytes=req.bytes;
                mapping->handle=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,
                    DWORD(req.bytes>>32),DWORD(req.bytes),asc::native::MappingName(session,req.id).c_str());
                const bool fresh=GetLastError()!=ERROR_ALREADY_EXISTS;
                if(mapping->handle&&fresh)
                    mapping->view=MapViewOfFile(mapping->handle,FILE_MAP_ALL_ACCESS,0,0,0);
                if(mapping->view){
                    try {
                        resources.emplace(req.id,std::move(mapping));
                        used+=req.bytes;
                        assets.SetCapacity(budget-used.load());
                        reply.status=0;
                    }catch(const std::bad_alloc&){}
                }
            }else if(req.command==Command::Release){
                const auto it=resources.find(req.id);
                if(it!=resources.end()){
                    used-=it->second->bytes;resources.erase(it);
                    assets.SetCapacity(budget-used.load());reply.status=0;
                }
            }else if(req.command==Command::Stats)reply.status=0;
            else if(req.command==Command::Stop){reply.status=0;stop=true;}
            else if(req.command==Command::CatalogConfigure) {
                reply.status=0;
                if(req.bytes==1 && !catalogStarted) {
                    wchar_t parentExe[32768]{};
                    DWORD n=DWORD(std::size(parentExe));
                    if(QueryFullProcessImageNameW(parent,0,parentExe,&n) && n>0) {
                        catalogStarted=true;
                        catalogScanning=true;
                        const auto gameRoot=std::filesystem::path(
                            std::wstring(parentExe,n)).parent_path();
                        try {
                            catalogThread=std::thread([&,gameRoot] {
                                try {
                                    // Catalog cache is strictly an optimization:
                                    // a stale/corrupt snapshot triggers source rebuilding.
                                    // Store under LOCALAPPDATA, not a potentially read-only
                                    // GTA installation directory.
                                    std::filesystem::path cacheFile;
                                    wchar_t local[32768]{};
                                    const DWORD size=GetEnvironmentVariableW(
                                        L"LOCALAPPDATA",local,DWORD(std::size(local)));
                                    if(size>0&&size<std::size(local)){
                                        uint64_t h=14695981039346656037ull;
                                        for(wchar_t ch:gameRoot.lexically_normal().wstring()){
                                            h^=uint64_t(std::towlower(ch));
                                            h*=1099511628211ull;
                                        }
                                        cacheFile=std::filesystem::path(local)/
                                            L"DistantStreaming"/L"WorldCatalog"/
                                            (L"catalog-"+std::to_wstring(h)+L".bin");
                                    }
                                    auto next=std::make_shared<const asc::world_catalog::Catalog>(
                                        cacheFile.empty()
                                            ?asc::world_catalog::Catalog::Build(gameRoot)
                                            :asc::world_catalog::Catalog::LoadOrBuild(
                                                gameRoot,cacheFile));
                                    std::lock_guard guard(catalogMutex);
                                    catalog=std::move(next);
                                }catch(...) {} // native GTA streaming is unaffected
                                catalogScanning=false;
                            });
                        }catch(...) {catalogScanning=false;catalogStarted=false;reply.status=1;}
                    } else reply.status=1;
                } else if(req.bytes>1)reply.status=1;
            }
            else if(req.command==Command::CatalogStatus ||
                    req.command==Command::CatalogQueryCell)reply.status=0;
            else if(req.command==Command::OpenSource||
                    req.command==Command::OpenWarmSource){
                HANDLE duplicated=nullptr;
                if(DuplicateHandle(parent,reinterpret_cast<HANDLE>(uintptr_t(req.bytes)),
                    GetCurrentProcess(),&duplicated,0,FALSE,DUPLICATE_SAME_ACCESS)&&
                   assets.Open(req.id,duplicated,req.command==Command::OpenWarmSource)){
                    reply.status=0;
                    if(req.command==Command::OpenWarmSource){
                        try {
                            std::lock_guard guard(warmMutex);
                            warmSources.insert(req.id);
                            warmQueue.push_back(WarmJob{req.id,0,0,
                                std::chrono::steady_clock::now()});
                            lastWarmRegistration=std::chrono::steady_clock::now();
                        }catch(const std::bad_alloc&){
                            std::lock_guard guard(warmMutex);
                            warmSources.erase(req.id);
                            assets.Close(req.id);
                            reply.status=1;
                        }
                    }
                }
            }else if(req.command==Command::OpenWarmImgMember){
                // Both provenance and dependency association are verified:
                // live Mod Loader-selected HANDLE; IMG VER2 member containing
                // the actual observed read; filename matches current-cell hint.
                // No path scan and no arbitrary whole-IMG background warming.
                HANDLE duplicated=INVALID_HANDLE_VALUE;
                if(req.id && observedImg.observedBytes &&
                   observedImg.hints.modelCount<=64 &&
                   observedImg.maxMemberBytes &&
                   observedImg.maxMemberBytes<=512u*1024u*1024u &&
                   observedImg.remainingQuotaBytes &&
                   DuplicateHandle(parent,
                       reinterpret_cast<HANDLE>(uintptr_t(req.bytes)),
                       GetCurrentProcess(),&duplicated,0,FALSE,DUPLICATE_SAME_ACCESS)){
                    if(assets.Open(req.id,duplicated,false)){
                        asc::img_member::Extent extent{};
                        uint64_t sourceBytes=0;
                        if(assets.VerifiedImgExtent(
                               req.id,observedImg.observedOffset,
                               observedImg.observedBytes,observedImg.hints,extent) &&
                           assets.SourceSize(req.id,sourceBytes) &&
                           extent.bytes<=observedImg.maxMemberBytes &&
                           extent.bytes<=observedImg.remainingQuotaBytes &&
                           extent.offset<=sourceBytes &&
                           extent.bytes<=sourceBytes-extent.offset){
                            constexpr uint64_t page=asc::native::TransferBytes;
                            const uint64_t begin=(extent.offset/page)*page;
                            const uint64_t end=extent.offset+extent.bytes;
                            // The helper caches full 1 MiB pages: cap at EOF
                            // and never traverse outside pages intersecting
                            // the verified member's source extent.
                            const uint64_t rounded=
                                ((end+page-1)/page)*page;
                            const uint64_t stopAt=std::min(rounded,sourceBytes);
                            try {
                                std::lock_guard guard(warmMutex);
                                if(warmQueue.size()<4096){
                                    warmQueue.push_back(WarmJob{req.id,begin,0,
                                        std::chrono::steady_clock::now(),stopAt,true});
                                    reply.status=0;
                                    reply.used=extent.bytes;
                                    reply.resources=extent.offset;
                                }
                            }catch(const std::bad_alloc&){}
                        }
                        if(reply.status!=0)assets.Close(req.id);
                    }
                    // assets.Open always consumes / closes duplicated.
                }
            }else if(req.command==Command::CloseSource){
                {
                    std::lock_guard guard(warmMutex);
                    warmSources.erase(req.id);
                    for(auto it=warmQueue.begin();it!=warmQueue.end();){
                        if(it->id==req.id)it=warmQueue.erase(it);
                        else ++it;
                    }
                }
                assets.Close(req.id);reply.status=0;
            }
            else if(req.command==Command::ReadSource){
                asc::native::AssetRead read;
                if(!Transfer(pipe,parent,&read,sizeof(read),false))break;
                {
                    auto destination=resources.find(read.destination);DWORD done=0;
                    if(destination!=resources.end()&&req.bytes<=asc::native::TransferBytes&&
                       req.bytes<=destination->second->bytes&&
                       assets.Read(req.id,read.offset,DWORD(req.bytes),destination->second->view,done,budget-used)){
                        reply.status=0;reply.used=done;
                    }
                }
            }else if(req.command==Command::AssetStats||
                     req.command==Command::CacheCoverage)reply.status=0;
        if(req.command!=asc::native::Command::ReadSource &&
           req.command!=asc::native::Command::OpenWarmImgMember)
            reply.used=used;
        if(req.command!=asc::native::Command::OpenWarmImgMember)
            reply.resources=resources.size();
        bool sent=Transfer(pipe,parent,&reply,sizeof(reply),true);
        if(sent&&reply.status==0){
            if(req.command==asc::native::Command::AssetStats){
                auto summary=assets.Summary();
                sent=Transfer(pipe,parent,&summary,sizeof(summary),true);
            }else if(req.command==asc::native::Command::CacheCoverage){
                auto coverage=assets.Coverage();
                sent=Transfer(pipe,parent,&coverage,sizeof(coverage),true);
            }else if(req.command==asc::native::Command::CatalogStatus){
                asc::native::CatalogSummary summary{};
                summary.scanning=catalogScanning?1u:0u;
                {
                    std::lock_guard guard(catalogMutex);
                    if(catalog){
                        const auto& s=catalog->GetStatus();
                        summary.generation=s.generation;summary.manifestHash=s.manifestHash;
                        summary.sources=s.sources;summary.ideDefinitions=s.ideDefinitions;
                        summary.placements=s.placements;summary.resolved=s.resolved;
                        summary.unresolved=s.unresolved;summary.unsupported=s.unsupported;
                        summary.rejected=s.rejected;summary.cells=s.cells;
                         summary.binarySources=s.binarySources;
                         summary.binaryPlacements=s.binaryPlacements;
                         summary.imgArchives=s.imgArchives;
                         summary.ambiguousModels=s.ambiguousModels;
                         summary.archiveAssetCandidates=s.archiveAssetCandidates;
                         summary.ambiguousAssetNames=s.ambiguousAssetNames;
                         summary.modelDependencyEdges=s.modelDependencyEdges;
                         summary.modelsWithArchiveCandidates=s.modelsWithArchiveCandidates;
                        summary.ready=s.ready?1u:0u;summary.complete=s.complete?1u:0u;
                        summary.loadedFromCache=s.loadedFromCache?1u:0u;
                    }
                }
                sent=Transfer(pipe,parent,&summary,sizeof(summary),true);
            }else if(req.command==asc::native::Command::CatalogQueryCell){
                asc::native::CatalogCellReply response{};
                const int32_t x=int32_t(uint32_t(req.id>>32));
                const int32_t y=int32_t(uint32_t(req.id));
                response.cellX=x;response.cellY=y;
                {
                    std::lock_guard guard(catalogMutex);
                    if(catalog && catalog->GetStatus().ready){
                        const auto generation=catalog->GetStatus().generation;
                        if(queryCell!=req.id || queryGeneration!=generation){
                            queryCell=req.id;queryGeneration=generation;queryCursor=0;
                        }
                        const auto hints=catalog->Query(x,y,64,&queryCursor);
                        response.placementCount=hints.placements;
                        response.modelCount=uint32_t(hints.modelIds.size());
                        std::copy(hints.modelIds.begin(),hints.modelIds.end(),response.modelIds);
                         for(size_t i=0;i<hints.modelNames.size();++i){
                             std::memcpy(response.modelNames[i],hints.modelNames[i].data(),24);
                             std::memcpy(response.txdNames[i],hints.txdNames[i].data(),24);
                         }
                    }
                }
                sent=Transfer(pipe,parent,&response,sizeof(response),true);
            }
        }
        if(!sent)break;
        } // request loop; a clean Stop command or broken pipe ends the session
        DisconnectNamedPipe(pipe);
    }
    warmStop=true;
    if(warmThread.joinable())warmThread.join();
    if(catalogThread.joinable())catalogThread.join();
    CloseHandle(pipe);CloseHandle(parent);
    return 0;
}
