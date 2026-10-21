#include "AssetCache.h"
#include "asc/WorldCachePolicy.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <new>

namespace {
uint64_t Size(const BY_HANDLE_FILE_INFORMATION& i) {
    return (uint64_t(i.nFileSizeHigh)<<32)|i.nFileSizeLow;
}
bool ReadAt(HANDLE file,uint64_t offset,void* data,DWORD bytes,DWORD& done,
            uint64_t deadline) {
    done=0;
    if(GetTickCount64()>=deadline)return false;
    OVERLAPPED ov{};
    ov.Offset=DWORD(offset);
    ov.OffsetHigh=DWORD(offset>>32);
    ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!ov.hEvent)return false;
    BOOL ok=ReadFile(file,data,bytes,&done,&ov);
    if(!ok&&GetLastError()==ERROR_IO_PENDING){
        const auto now=GetTickCount64();
        const DWORD remaining=now<deadline?DWORD(deadline-now):0;
        if(WaitForSingleObject(ov.hEvent,remaining)==WAIT_OBJECT_0)
            ok=GetOverlappedResult(file,&ov,&done,FALSE);
        else {
            CancelIoEx(file,&ov);
            GetOverlappedResult(file,&ov,&done,TRUE);
            ok=FALSE;
        }
    }
    CloseHandle(ov.hEvent);
    return ok!=FALSE;
}
}
AssetCache::~AssetCache(){
    std::lock_guard guard(mutex_);
    for(auto& [_,file]:files_)CloseHandle(file.handle);
}
AssetCache::Identity AssetCache::IdentityOf(const BY_HANDLE_FILE_INFORMATION& i) noexcept {
    return {i.dwVolumeSerialNumber,i.nFileIndexHigh,i.nFileIndexLow,
            i.nFileSizeHigh,i.nFileSizeLow,
            i.ftLastWriteTime.dwHighDateTime,i.ftLastWriteTime.dwLowDateTime};
}
bool AssetCache::Open(uint64_t id,HANDLE duplicated,bool warm){
    // Reopen independently; a duplicated synchronous GTA file handle can
    // otherwise share a file pointer. All reads below use explicit offsets.
    HANDLE independent=ReOpenFile(duplicated,GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        FILE_FLAG_OVERLAPPED|FILE_FLAG_RANDOM_ACCESS);
    CloseHandle(duplicated);
    if(independent==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};
    if(!id||!GetFileInformationByHandle(independent,&info)||
       (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)){
        CloseHandle(independent);return false;
    }
    std::lock_guard guard(mutex_);
    if(files_.contains(id)||files_.size()>=4096){
        CloseHandle(independent);return false;
    }
    const auto identity=IdentityOf(info);
    try {
        files_.emplace(id,File{independent,Size(info),identity,warm});
        if(warm)warmFiles_[identity]=Size(info);
    }catch(const std::bad_alloc&){
        files_.erase(id);
        CloseHandle(independent);
        return false;
    }
    return true;
}
void AssetCache::Close(uint64_t id){
    std::lock_guard guard(mutex_);
    auto it=files_.find(id);
    if(it==files_.end())return;
    // Warm-source handles intentionally remain open until session shutdown.
    // Closing one removes coverage only when no other warm source owns it.
    if(it->second.warm){
        bool other=false;
        for(const auto& [candidate,f]:files_)
            if(candidate!=id&&f.warm&&f.identity==it->second.identity){other=true;break;}
        if(!other)warmFiles_.erase(it->second.identity);
    }
    CloseHandle(it->second.handle);
    files_.erase(it);
}
bool AssetCache::SnapshotFile(uint64_t id,Snapshot& out){
    std::lock_guard guard(mutex_);
    const auto it=files_.find(id);
    if(it==files_.end())return false;
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(it->second.handle,&info))return false;
    const auto identity=IdentityOf(info);
    if(!(identity==it->second.identity)){
        if(it->second.warm){
            try {warmFiles_[identity]=Size(info);}
            catch(const std::bad_alloc&){return false;}
            bool otherOwner=false;
            for(const auto& [otherId,other]:files_)
                if(otherId!=id && other.warm &&
                   other.identity==it->second.identity){otherOwner=true;break;}
            if(!otherOwner)warmFiles_.erase(it->second.identity);
        }
        it->second.size=Size(info);
        it->second.identity=identity;
    }
    HANDLE copy=INVALID_HANDLE_VALUE;
    if(!DuplicateHandle(GetCurrentProcess(),it->second.handle,GetCurrentProcess(),
                        &copy,0,FALSE,DUPLICATE_SAME_ACCESS))return false;
    out.handle=copy;
    out.identity=identity;
    out.size=Size(info);
    return true;
}
void AssetCache::EvictUnlocked(){
    if(lru_.empty())return;
    auto key=lru_.back();
    index_.erase(key);
    const auto count=residentPages_.find(key.file);
    if(count!=residentPages_.end() && --count->second==0)residentPages_.erase(count);
    lru_.pop_back();
    summary_.residentBytes-=asc::native::TransferBytes;
}
void AssetCache::TrimUnlocked(uint64_t capacity){
    while(summary_.residentBytes>capacity&&!lru_.empty())EvictUnlocked();
}
void AssetCache::Trim(uint64_t capacity){
    std::lock_guard guard(mutex_);
    TrimUnlocked(capacity);
}
void AssetCache::SetCapacity(uint64_t capacity){
    std::lock_guard guard(mutex_);
    capacity_=capacity;
    TrimUnlocked(capacity_);
}
std::shared_ptr<AssetCache::Page> AssetCache::Load(
        const Snapshot& file,uint64_t pageIndex,uint64_t capacity,bool warming,
        uint64_t deadline){
    constexpr uint64_t pageSize=asc::native::TransferBytes;
    if(pageIndex>UINT64_MAX/pageSize)return {};
    const uint64_t position=pageIndex*pageSize;
    if(position>=file.size)return {};
    const Key key{file.identity,pageIndex};
    std::shared_ptr<Flight> flight;
    {
        std::unique_lock guard(mutex_);
        capacity=std::min(capacity,capacity_);
        auto found=index_.find(key);
        if(found!=index_.end()){
            if(!warming)++summary_.hits;
            lru_.splice(lru_.begin(),lru_,found->second.order);
            return found->second.page;
        }
        if(!warming)++summary_.misses;
        if(auto existing=inFlight_.find(key);existing!=inFlight_.end()){
            if(warming)return {}; // Another reader will publish this page.
            ++waits_;
            auto waiting=existing->second;
            // Never let a background read block GTA's RPC indefinitely.
            const auto now=GetTickCount64();
            waiting->ready.wait_for(guard,std::chrono::milliseconds(
                now<deadline?std::min<uint64_t>(200,deadline-now):0),[&]{
                return waiting->complete;
            });
            found=index_.find(key);
            if(found!=index_.end()){
                lru_.splice(lru_.begin(),lru_,found->second.order);
                return found->second.page;
            }
            ++deferrals_;
            return {};
        }
        // Cached pages can be served even if the remaining helper budget is
        // below one page; only new allocations require capacity.
        // At most 8 outstanding MiB, regardless of archive count.
        if(capacity<pageSize ||
           !asc::world_cache::MayStartFlight(pendingBytes_)||
           (warming && !asc::world_cache::MayWarm(
               summary_.residentBytes,pendingBytes_,capacity))){
            ++deferrals_;
            return {};
        }
        try {
            flight=std::make_shared<Flight>();
            inFlight_.emplace(key,flight);
        }catch(const std::bad_alloc&){++failures_;return {};}
        pendingBytes_+=pageSize;
    }
    // No global metadata mutex is held across allocation or file I/O.
    std::shared_ptr<Page> page;
    DWORD completed=0;
    const DWORD wanted=DWORD(std::min<uint64_t>(pageSize,file.size-position));
    MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);
    if(GlobalMemoryStatusEx(&memory) &&
       memory.ullAvailPhys>=512ull*asc::native::MiB &&
       memory.ullAvailPageFile>=pageSize){
        try {
            page=std::make_shared<Page>();
            page->key=key;
            page->allocated=allocated_;
            page->data=VirtualAlloc(nullptr,pageSize,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
            if(page->data)allocated_->fetch_add(pageSize,std::memory_order_relaxed);
            BY_HANDLE_FILE_INFORMATION after{};
            if(!page->data ||
               !ReadAt(file.handle,position,page->data,wanted,completed,deadline) ||
               completed!=wanted ||
               !GetFileInformationByHandle(file.handle,&after) ||
               !(IdentityOf(after)==file.identity))
                page.reset();
            else page->bytes=completed;
        }catch(const std::bad_alloc&){page.reset();}
    }
    {
        std::lock_guard guard(mutex_);
        pendingBytes_-=pageSize;
        capacity=std::min(capacity,capacity_);
        // Count returned source bytes even if validation/publication fails.
        summary_.diskBytes+=completed;
        if(page){
            // Reads can finish after other pages have filled the cache.
            // Demand reads may evict LRU entries only *after* successful I/O.
            if(capacity<pageSize ||
               (warming && summary_.residentBytes>capacity-pageSize))
                page.reset();
            else{
                try {
                    residentPages_.try_emplace(key.file,0);
                    lru_.push_front(key);
                    try {
                        auto [it,inserted]=index_.emplace(key,Entry{page,lru_.begin()});
                        if(!inserted){
                            lru_.pop_front();
                            page=it->second.page;
                        }else {
                            ++residentPages_.find(key.file)->second;
                            summary_.residentBytes+=pageSize;
                            TrimUnlocked(capacity);
                        }
                    }catch(...){
                        lru_.pop_front();
                        throw;
                    }
                }catch(const std::bad_alloc&){
                    const auto count=residentPages_.find(key.file);
                    if(count!=residentPages_.end() && !count->second)
                        residentPages_.erase(count);
                    page.reset();
                }
            }
        }
        if(!page)++failures_;
        // Notify existing waiters even when the I/O or publication failed.
        flight->complete=true;
        inFlight_.erase(key);
    }
    flight->ready.notify_all();
    return page;
}
bool AssetCache::Read(uint64_t id,uint64_t offset,DWORD bytes,void* destination,
                      DWORD& done,uint64_t capacity){
    done=0;
    if(!destination||bytes>asc::native::TransferBytes)return false;
    Snapshot snapshot;
    if(!SnapshotFile(id,snapshot))return false;
    if(offset>=snapshot.size)return true;
    bytes=DWORD(std::min<uint64_t>(bytes,snapshot.size-offset));
    // A read may straddle two pages; both share one disk-I/O budget.
    const uint64_t deadline=GetTickCount64()+asc::native::AssetIoTimeoutMs;
    while(done<bytes){
        const uint64_t position=offset+done;
        auto page=Load(snapshot,position/asc::native::TransferBytes,capacity,false,deadline);
        if(!page)return false;
        const DWORD within=DWORD(position%asc::native::TransferBytes);
        if(within>=page->bytes)return false;
        const DWORD count=std::min(bytes-done,page->bytes-within);
        std::memcpy(static_cast<unsigned char*>(destination)+done,
                    static_cast<const unsigned char*>(page->data)+within,count);
        done+=count;
    }
    BY_HANDLE_FILE_INFORMATION after{};
    if(!GetFileInformationByHandle(snapshot.handle,&after) ||
       !(IdentityOf(after)==snapshot.identity))return false;
    std::lock_guard guard(mutex_);
    summary_.gameBytes+=done;
    return true;
}
bool AssetCache::VerifiedImgExtent(
    uint64_t id,uint64_t observedOffset,uint32_t observedBytes,
    const asc::native::CatalogCellReply& hints,asc::img_member::Extent& out){
    out={};
    if(!observedBytes || hints.modelCount==0 || hints.modelCount>64)return false;
    Snapshot snapshot;
    if(!SnapshotFile(id,snapshot))return false;
    std::shared_ptr<const std::vector<asc::img_member::Extent>> members;
    {
        std::lock_guard guard(mutex_);
        const auto found=imgDirectories_.find(snapshot.identity);
        if(found!=imgDirectories_.end())members=found->second;
    }
    if(!members){
        // Header and directory chunks share one budget, not one per read.
        const uint64_t deadline=GetTickCount64()+asc::native::AssetIoTimeoutMs;
        unsigned char header[8]{};
        DWORD done=0;
        if(!ReadAt(snapshot.handle,0,header,8,done,deadline)||done!=8)return false;
        size_t length=0;
        if(!asc::img_member::DirectorySize(header,snapshot.size,length))return false;
        // Upper bound is 4.8 MiB; this method runs on the worker's RPC,
        // never a game callback. Partial directories are always rejected.
        try {
            std::vector<uint8_t> directory(length);
            for(size_t offset=0;offset<directory.size();){
                const DWORD count=DWORD(std::min<size_t>(
                    asc::native::TransferBytes,directory.size()-offset));
                DWORD received=0;
                if(!ReadAt(snapshot.handle,offset,directory.data()+offset,
                           count,received,deadline) || received!=count)return false;
                offset+=received;
            }
            auto parsed=std::make_shared<std::vector<asc::img_member::Extent>>();
            if(!asc::img_member::Parse(directory,snapshot.size,*parsed))return false;
            BY_HANDLE_FILE_INFORMATION verified{};
            if(!GetFileInformationByHandle(snapshot.handle,&verified) ||
               !(IdentityOf(verified)==snapshot.identity))return false;
            members=std::move(parsed);
            {
                std::lock_guard guard(mutex_);
                if(!imgDirectories_.contains(snapshot.identity)){
                    if(imgDirectoryOrder_.size()>=8){
                        imgDirectories_.erase(imgDirectoryOrder_.front());
                        imgDirectoryOrder_.pop_front();
                    }
                    imgDirectories_.emplace(snapshot.identity,members);
                    imgDirectoryOrder_.push_back(snapshot.identity);
                }
            }
        }catch(const std::bad_alloc&){return false;}
    }
    const auto decision=asc::img_member::FindObserved(
        *members,observedOffset,observedBytes,hints,out);
    BY_HANDLE_FILE_INFORMATION after{};
    return decision==asc::img_member::Decision::Match &&
        GetFileInformationByHandle(snapshot.handle,&after) &&
        IdentityOf(after)==snapshot.identity;
}
bool AssetCache::SourceSize(uint64_t id,uint64_t& size){
    Snapshot snapshot;
    if(!SnapshotFile(id,snapshot))return false;
    size=snapshot.size;
    return true;
}
bool AssetCache::WarmOnePage(uint64_t id,uint64_t offset,uint64_t capacity){
    Snapshot snapshot;
    if(!SnapshotFile(id,snapshot)||offset>=snapshot.size)return false;
    const Key key{snapshot.identity,offset/asc::native::TransferBytes};
    {
        std::lock_guard guard(mutex_);
        // In-flight pages are already scheduled; continue scanning without
        // waiting or inflating demand hit counters.
        if(index_.contains(key)||inFlight_.contains(key))return true;
    }
    return Load(snapshot,key.index,capacity,true,
                GetTickCount64()+asc::native::AssetIoTimeoutMs)!=nullptr;
}
asc::native::AssetSummary AssetCache::Summary()const {
    std::lock_guard guard(mutex_);
    return summary_;
}
asc::native::CacheCoverage AssetCache::Coverage()const {
    std::lock_guard guard(mutex_);
    asc::native::CacheCoverage report{};
    report.archives=warmFiles_.size();
    for(const auto& [identity,size]:warmFiles_){
        report.sourceBytes+=size;
        report.expectedPages+=asc::world_cache::PageCount(size);
        const auto count=residentPages_.find(identity);
        if(count!=residentPages_.end())report.residentPages+=count->second;
    }
    report.inFlightPages=inFlight_.size();
    report.waits=waits_;
    report.deferred=deferrals_;
    report.failures=failures_;
    report.pendingBytes=pendingBytes_;
    report.allocatedBytes=allocated_->load(std::memory_order_relaxed);
    report.capacityBytes=capacity_;
    return report;
}
