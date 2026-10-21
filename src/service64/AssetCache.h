#pragma once
#include "asc/NativeMemoryProtocol.h"
#include "asc/ArchiveIdentity.h"
#include "asc/ImgMemberExtent.h"
#include <windows.h>
#include <unordered_map>
#include <list>
#include <deque>
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <limits>

// The helper retains immutable 1 MiB source pages. File reads do not hold
// the global metadata mutex; a per-key flight deduplicates concurrent loads.
class AssetCache {
public:
    ~AssetCache();
    bool Open(uint64_t id,HANDLE duplicated,bool warm=false);
    void Close(uint64_t id);
    bool WarmOnePage(uint64_t id,uint64_t offset,uint64_t capacity);
    // Returns the current source length; refreshes metadata on file changes.
    bool SourceSize(uint64_t id,uint64_t& size);
    // Resolve ONLY an observed read on a live selected IMG handle, not a
    // filename-based archive candidate. The read must identify one member.
    bool VerifiedImgExtent(uint64_t id,uint64_t observedOffset,
                           uint32_t observedBytes,
                           const asc::native::CatalogCellReply& hints,
                           asc::img_member::Extent& out);
    bool Read(uint64_t id,uint64_t offset,DWORD bytes,void* destination,DWORD& done,uint64_t capacity);
    void Trim(uint64_t capacity);
    // Authoritative limit, unlike a one-off pressure trim. Finishing reads
    // must obey the latest value, not the limit captured before their I/O.
    void SetCapacity(uint64_t capacity);
    asc::native::AssetSummary Summary()const;
    asc::native::CacheCoverage Coverage()const;
private:
    using Identity=asc::archive_io::FileIdentity;
    static Identity IdentityOf(const BY_HANDLE_FILE_INFORMATION& info) noexcept;
    struct File {
        HANDLE handle=INVALID_HANDLE_VALUE;
        uint64_t size=0;
        Identity identity{};
        bool warm=false;
    };
    struct Snapshot {
        HANDLE handle=INVALID_HANDLE_VALUE;
        uint64_t size=0;
        Identity identity{};
        ~Snapshot(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
        Snapshot()=default;
        Snapshot(const Snapshot&)=delete;
        Snapshot& operator=(const Snapshot&)=delete;
    };
    using Key=asc::archive_io::PageKey;
    using Hash=asc::archive_io::PageHash;
    struct Page {
        Key key{};
        DWORD bytes=0;
        void* data=nullptr;
        std::shared_ptr<std::atomic<uint64_t>> allocated;
        ~Page(){if(data){VirtualFree(data,0,MEM_RELEASE);
            allocated->fetch_sub(asc::native::TransferBytes,std::memory_order_relaxed);}}
    };
    struct Entry {
        std::shared_ptr<Page> page;
        std::list<Key>::iterator order;
    };
    struct Flight {
        bool complete=false;
        std::condition_variable ready;
    };
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t,File> files_;
    std::list<Key> lru_;
    std::unordered_map<Key,Entry,Hash> index_;
    // Includes demand pages that may be promoted to warm coverage later.
    std::unordered_map<Identity,uint64_t,asc::archive_io::IdentityHash> residentPages_;
    std::unordered_map<Key,std::shared_ptr<Flight>,Hash> inFlight_;
    // Warm files are represented by stable archive identities, not ephemeral IDs.
    std::unordered_map<Identity,uint64_t,asc::archive_io::IdentityHash> warmFiles_;
    // Validated IMG directories are immutable per captured source identity.
    // Bounded to eight files so observed reads don't reparse 4.8 MiB each.
    std::unordered_map<Identity,
        std::shared_ptr<const std::vector<asc::img_member::Extent>>,
        asc::archive_io::IdentityHash> imgDirectories_;
    std::deque<Identity> imgDirectoryOrder_;
    asc::native::AssetSummary summary_{};
    uint64_t pendingBytes_=0;
    uint64_t capacity_=std::numeric_limits<uint64_t>::max();
    // Shared ownership keeps accounting alive if a reader outlives the index.
    std::shared_ptr<std::atomic<uint64_t>> allocated_=
        std::make_shared<std::atomic<uint64_t>>(0);
    uint64_t waits_=0,deferrals_=0,failures_=0;
    bool SnapshotFile(uint64_t id,Snapshot& out);
    std::shared_ptr<Page> Load(const Snapshot& file,uint64_t index,uint64_t capacity,
                               bool warming,uint64_t deadline);
    void EvictUnlocked();
    void TrimUnlocked(uint64_t capacity);
};
