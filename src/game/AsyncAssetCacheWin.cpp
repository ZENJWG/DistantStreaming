// ASI-owned optional read-through cache. NEVER does disk I/O or IPC inside ReadFile.
// A miss must take the unchanged GTA/other-mod ReadFile path.
#include "AsyncAssetCacheWin.h"
#include "NativeBacking.h"
#include "asc/NativeMemoryProtocol.h"
#include "asc/WorldWarmPolicy.h"
#include "asc/ObservedSourceWarmPolicy.h"
#include "asc/ArchiveReadPolicy.h"
#include "asc/CdStreamHandleGate.h"
#include "asc/CdStreamDispatchPolicy.h"
#include "asc/CdStreamHookSignature.h"
#include "asc/ModLoaderReadGate.h"
#include "asc/ObservedDependencyWarmPolicy.h"
#include <windows.h>
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <system_error>
#include <list>
#include <iterator>
#include <memory>
#include <sstream>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace asc::external_cache {
namespace {
using ReadFn=BOOL (WINAPI*)(HANDLE,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
using CdReadFn=bool (__cdecl*)(int,void*,uint32_t,int);
// GTA SA 1.0 US CdStreamInfo.h layout (gta-reversed). x86 only.
struct GtaCdChannel {
    uint32_t sectorOffset;
    uint32_t sectorsToRead;
    void* buffer;
    uint8_t fieldC,locked,inUse,fieldF;
    int32_t status;
    HANDLE sync;
    HANDLE file;
    OVERLAPPED overlapped;
};
static_assert(sizeof(void*)==4 && sizeof(GtaCdChannel)==0x30);
static_assert(offsetof(GtaCdChannel,status)==0x10);
static_assert(offsetof(GtaCdChannel,file)==0x18);
constexpr uintptr_t kCdReadAddress=0x406A20;
constexpr uintptr_t kCdChannelsAddress=0x8E3FFC;
constexpr uintptr_t kCdInitializedAddress=0x8E3FE4;
constexpr uintptr_t kCdChannelCountAddress=0x8E4090;
constexpr uintptr_t kCdLastPositionAddress=0x8E4898;
constexpr uintptr_t kCdHandlesAddress=0x8E4010;
constexpr uint32_t kMaxRead=4u*asc::native::TransferBytes;
constexpr size_t kMaxTracked=256;
struct Key {
    DWORD volume=0, high=0, low=0, sizeHigh=0, sizeLow=0, writeHigh=0,writeLow=0;
    uint64_t offset=0;
    DWORD length=0;
    bool operator==(const Key&)const = default;
};
struct Hash {
    size_t operator()(const Key& k) const noexcept {
        uint64_t h=1469598103934665603ull;
        const auto mix=[&](uint64_t v){h^=v;h*=1099511628211ull;};
        mix(k.volume);mix(k.high);mix(k.low);mix(k.sizeHigh);mix(k.sizeLow);
        mix(k.writeHigh);mix(k.writeLow);mix(k.offset);mix(k.length);
        return size_t(h);
    }
};
struct Work {
    Key key;
    HANDLE duplicated=INVALID_HANDLE_VALUE;
    // Non-Unsupported only for a handle actually consumed by Mod Loader's
    // module-local ReadFile; file paths on disk cannot establish precedence.
    asc::modloader_cache::SourceKind observedKind=
        asc::modloader_cache::SourceKind::Unsupported;
};
struct Entry {std::shared_ptr<const std::vector<uint8_t>> bytes;std::list<Key>::iterator order;};
struct SourceReuse {
    Key identity{};
    uint64_t fetched=0,served=0;
    uint32_t nextProbe=0;
    bool occupied=false,probing=false;
};
struct CostMetric {
    std::atomic<uint64_t> calls{0},samples{0},ticks{0},peakTicks{0};
};
// One in 64 executions, including early exits. No GPU queries or IPC.
struct CostSample {
    CostMetric& metric;
    LARGE_INTEGER start{};
    bool sampled;
    explicit CostSample(CostMetric& value):metric(value),
        sampled((value.calls.fetch_add(1,std::memory_order_relaxed)&63u)==0){
        if(sampled){
            const DWORD error=GetLastError();
            QueryPerformanceCounter(&start);
            SetLastError(error);
        }
    }
    ~CostSample(){
        if(!sampled)return;
        const DWORD error=GetLastError();
        LARGE_INTEGER end{};QueryPerformanceCounter(&end);
        const uint64_t ticks=end.QuadPart>=start.QuadPart?uint64_t(end.QuadPart-start.QuadPart):0;
        metric.ticks.fetch_add(ticks,std::memory_order_relaxed);
        metric.samples.fetch_add(1,std::memory_order_relaxed);
        auto peak=metric.peakTicks.load(std::memory_order_relaxed);
        while(peak<ticks && !metric.peakTicks.compare_exchange_weak(
              peak,ticks,std::memory_order_relaxed)){}
        SetLastError(error);
    }
};
struct State {
    Settings cfg{};
    ReadFn original=nullptr;
    CdReadFn cdOriginal=nullptr;
    void* cdHookTarget=nullptr;
    std::atomic<bool> cdActive{false};
    std::atomic<bool> cdLayoutRejected{false};
    std::atomic<uint64_t> cdCalls{0},cdEligible{0},cdHits{0},
                          cdMisses{0},cdQueued{0},cdSkipped{0};
    std::atomic<uint64_t> cdSkipDisconnected{0},cdSkipAddresses{0},
                          cdSkipChannels{0},cdSkipDecode{0},
                          cdSkipHandle{0},cdSkipSource{0},
                          cdSkipHandleNull{0},cdSkipHandleInvalid{0};
    std::atomic<uint32_t> cdLastMissingFileId{0xffffffffu};
    // Diagnostic only: channel.file may be stale or mod-owned. Never use it
    // for cache substitution or metadata reads.
    std::atomic<uint64_t> cdNullHandleChannelFilePresent{0};
    std::atomic<uint64_t> cdNullHandleNativeAccepted{0};
    void** importSlot=nullptr;
    ReadFn modLoaderOriginal=nullptr;
    void** modLoaderImportSlot=nullptr;
    HMODULE modLoaderModule=nullptr;
    std::atomic<bool> modLoaderIatActive{false};
    std::atomic<uint32_t> modLoaderHookState{0}; // 0=not tried,1=DLL missing,2=active,3=unsupported IAT,4=foreign reader
    std::atomic<uint64_t> modLoaderImgReads{0},modLoaderLooseReads{0},
        modLoaderImgHits{0},modLoaderLooseHits{0};
    std::atomic<uint64_t> modLoaderCalls{0},modLoaderEligible{0},
        modLoaderHits{0},modLoaderMisses{0},modLoaderQueued{0},
        modLoaderNativePending{0},modLoaderPassThrough{0};
    void* nativeHookTarget=nullptr;
    bool processReaderHook=false;
    std::atomic<uint64_t> gtaStreamHandleCalls{0}, otherHandleCalls{0};
    bool safeSourceReader=false; // Only bypass ReadFile for verified Windows reader.
    std::atomic<uint64_t> interceptedCalls{0}, eligibleCalls{0};
    std::atomic<uint64_t> observedReads{0}, observedBytes{0};
    std::atomic<uint64_t> synchronousCalls{0}, overlappedCalls{0};
    std::atomic<uint64_t> inspectedImg{0}, rejectedSource{0}, oversizedCalls{0};
    std::atomic<uint64_t> overlappedQueued{0}, nativePending{0};
    std::thread worker;
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<Work> pending;
    std::unordered_set<Key,Hash> inFlight; // Includes currently active worker job.
    std::unordered_map<Key,Entry,Hash> local;
    std::list<Key> lru;
    size_t localBytes=0;
    // Fixed-size, source-identity keyed history: no allocations in read hooks.
    // A collision simply forgets old statistics; it never reuses source bytes.
    std::array<SourceReuse,256> sourceReuse{};
    std::atomic<uint64_t> lowReuseSkipped{0},lowReuseWarmSkipped{0};
    std::atomic<bool> stop{false},active{false},connected{false};
    std::atomic<uint64_t> hits{0}, misses{0}, queued{0}, dropped{0}, fetched{0};
    std::atomic<uint64_t> suppliedBytes{0}, errors{0};
    CostMetric metadataCost,lookupCost,copyCost,queueCost;
    CostMetric cdHookCost,modLoaderHookCost,readHookCost,nativeReadCost;
    std::atomic<uint64_t> lookupHits{0},lookupContention{0},copiedBytes{0};
    std::atomic<uint64_t> queuedBytes{0},fetchedBytes{0};
    std::atomic<uint64_t> pinnedPublicationSkips{0};
    std::atomic<uint64_t> remoteResident{0},remoteReadHits{0},remoteDiskBytes{0};
    std::atomic<int32_t> worldCellX{INT32_MAX},worldCellY{INT32_MAX};
    std::atomic<uint64_t> catalogPlacements{0},catalogResolved{0},catalogUnresolved{0},
        catalogCells{0},catalogGeneration{0};
    std::atomic<uint64_t> catalogAssetCandidates{0},catalogAmbiguousAssets{0},
        catalogDependencyEdges{0},catalogMatchedModels{0};
    std::atomic<uint64_t> catalogBinarySources{0},catalogBinaryPlacements{0},
        catalogImgArchives{0},catalogAmbiguous{0};
    std::atomic<uint64_t> imgExtentMatched{0},imgExtentRejected{0},
        imgExtentBytes{0};
    std::atomic<uint32_t> catalogReady{0},catalogScanning{0},catalogLoadedFromCache{0},catalogCellPlacements{0},
        catalogCellModels{0};
    // Separate small mailbox: game callback only try_lock's, never waits on IPC.
    std::mutex catalogHintMutex;
    asc::native::CatalogCellReply catalogHints{};
    uint64_t catalogHintSerial=0;
    std::atomic<uint64_t> remoteAllocated{0},remotePending{0},remoteCapacity{0};
    uint64_t reportServedBytes=0,reportDiskBytes=0,reportQueuedBytes=0;
    DWORD reportHelperPid=0;
    std::atomic<uint64_t> warmArchives{0},warmTotalPages{0},warmResidentPages{0};
    std::atomic<uint64_t> warmDeferred{0},warmFailed{0},cacheWaits{0};
    std::atomic<uint64_t> discoverCandidates{0},discoverOpened{0},discoverRejected{0};
    std::atomic<uint64_t> observedWarmSources{0},observedWarmBytes{0},
        observedWarmDenied{0},observedWarmErrors{0};
    std::atomic<uint64_t> dependencyWarmMatched{0},dependencyWarmUnmatched{0},
        dependencyWarmNoHints{0},dependencyWarmUnsafe{0};
    std::atomic<DWORD> helperPid{0};
    uint32_t lastReport=0;
    std::wstring helperPath;
};
State& S(){static State s;return s;}
BOOL NativeRead(ReadFn read,HANDLE file,LPVOID buffer,DWORD count,
                LPDWORD done,LPOVERLAPPED ov){
    CostSample sample(S().nativeReadCost);
    return read(file,buffer,count,done,ov);
}

bool Committed(const void* ptr,size_t len){
    if(!ptr)return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if(!VirtualQuery(ptr,&mbi,sizeof(mbi))||mbi.State!=MEM_COMMIT||
       (mbi.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
    const uintptr_t a=reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t end=reinterpret_cast<uintptr_t>(mbi.BaseAddress)+mbi.RegionSize;
    return a<=end && len<=end-a;
}
bool Writable(const void* ptr,size_t len){
    if(!ptr)return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if(!VirtualQuery(ptr,&mbi,sizeof(mbi)) || mbi.State!=MEM_COMMIT ||
       (mbi.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
    const auto address=reinterpret_cast<uintptr_t>(ptr);
    const auto begin=reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    if(address<begin || address-begin>mbi.RegionSize ||
       len>mbi.RegionSize-(address-begin))return false;
    const DWORD access=mbi.Protect&0xFF;
    return access==PAGE_READWRITE || access==PAGE_WRITECOPY ||
           access==PAGE_EXECUTE_READWRITE || access==PAGE_EXECUTE_WRITECOPY;
}
bool SupportedArchive(HANDLE handle,const Key& key){
    struct Recent {
        DWORD volume=0,high=0,low=0,writeHigh=0,writeLow=0;
        bool known=false,supported=false;
    };
    thread_local Recent recent;
    if(recent.known && recent.volume==key.volume &&
       recent.high==key.high && recent.low==key.low &&
       recent.writeHigh==key.writeHigh && recent.writeLow==key.writeLow)
        return recent.supported;
    wchar_t path[1024]{};
    const DWORD len=GetFinalPathNameByHandleW(handle,path,1024,FILE_NAME_NORMALIZED);
    if(!len||len>=1024)return false;
    // The helper always reopens this exact HANDLE, not a path built from the name.
    // Only source IMG reads are intercepted; mod-loader ReadFile hooks are respected.
    const wchar_t* last=path;
    for(DWORD i=0;i<len;++i)if(path[i]==L'\\'||path[i]==L'/')last=path+i+1;
    const wchar_t* ext=wcsrchr(last,L'.');
    const bool supported=ext && _wcsicmp(ext,L".img")==0;
    recent={key.volume,key.high,key.low,key.writeHigh,key.writeLow,true,supported};
    return supported;
}
// Only an actual loose replacement under a modloader folder may enter this
// provider-specific path. The packed sector of the GTA CdStream dispatcher
// does NOT identify the selected Mod Loader source.
asc::modloader_cache::SourceKind ModLoaderSourceKind(HANDLE handle,const Key& key){
    using Kind=asc::modloader_cache::SourceKind;
    struct Recent {Key key{};bool known=false;Kind kind=Kind::Unsupported;};
    thread_local Recent recent;
    if(recent.known && recent.key.volume==key.volume &&
       recent.key.high==key.high && recent.key.low==key.low &&
       recent.key.sizeHigh==key.sizeHigh && recent.key.sizeLow==key.sizeLow &&
       recent.key.writeHigh==key.writeHigh && recent.key.writeLow==key.writeLow)
        return recent.kind;
    wchar_t path[32768]{};
    const DWORD length=GetFinalPathNameByHandleW(handle,path,DWORD(std::size(path)),
                                                 FILE_NAME_NORMALIZED);
    const auto kind=(length>0 && length<std::size(path)) ?
        asc::modloader_cache::ClassifySource(std::wstring_view(path,length)) :
        Kind::Unsupported;
    recent={key,true,kind};
    return kind;
}
bool SourceKey(HANDLE handle,uint64_t offset,DWORD n,Key& key){
    CostSample sample(S().metadataCost);
    BY_HANDLE_FILE_INFORMATION i{};
    if(!GetFileInformationByHandle(handle,&i) ||
       (i.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))return false;
    const uint64_t fileSize=(uint64_t(i.nFileSizeHigh)<<32)|i.nFileSizeLow;
    if(offset>fileSize||n>fileSize-offset)return false;
    key={i.dwVolumeSerialNumber,i.nFileIndexHigh,i.nFileIndexLow,
         i.nFileSizeHigh,i.nFileSizeLow,
         i.ftLastWriteTime.dwHighDateTime,i.ftLastWriteTime.dwLowDateTime,offset,n};
    return true;
}
// Caller owns the metadata mutex. Exclude read extent from source identity;
// replacement/size/write-time changes start fresh statistics.
SourceReuse& ReuseFor(State& state,const Key& key){
    Key identity=key;identity.offset=0;identity.length=0;
    auto& entry=state.sourceReuse[Hash{}(identity)%state.sourceReuse.size()];
    if(!entry.occupied || !(entry.identity==identity)){
        entry={};entry.identity=identity;entry.occupied=true;
    }
    return entry;
}
bool LowReuse(const SourceReuse& entry){
    return entry.fetched>=8ull*1048576 && entry.served<entry.fetched/8;
}
bool Lookup(const Key& key,void* destination){
    auto& s=S();
    CostSample sample(s.lookupCost);
    std::unique_lock<std::mutex> guard(s.mutex,std::try_to_lock);
    if(!guard.owns_lock()){++s.lookupContention;return false;}
    auto it=s.local.find(key);
    if(it==s.local.end())return false;
    if(it->second.bytes->size()!=key.length)return false;
    // Pin immutable data, then release metadata before the potentially large
    // copy. Worker eviction cannot invalidate the bytes while we use them.
    auto bytes=it->second.bytes;
    ReuseFor(s,key).served+=key.length;
    s.lru.splice(s.lru.begin(),s.lru,it->second.order);
    guard.unlock();
    {
        CostSample copy(s.copyCost);
        std::memcpy(destination,bytes->data(),key.length);
    }
    ++s.lookupHits;
    s.copiedBytes.fetch_add(key.length,std::memory_order_relaxed);
    return true;
}
bool Queue(HANDLE original,const Key& key,
           asc::modloader_cache::SourceKind observedKind=
               asc::modloader_cache::SourceKind::Unsupported){
    auto& s=S();
    CostSample sample(s.queueCost);
    if(!s.connected.load(std::memory_order_acquire))return false;
    // The game thread never does IPC or disk access. Dedup in-flight jobs
    // as well as pending jobs; otherwise repeated archive reads can enqueue
    // thousands of duplicated handles while the single worker is busy.
    {
        std::unique_lock<std::mutex> guard(s.mutex,std::try_to_lock);
        if(!guard.owns_lock() || s.stop || s.pending.size()>=s.cfg.maxPending ||
           s.local.contains(key) || s.inFlight.contains(key)){
            ++s.dropped;return false;
        }
        if(s.cfg.adaptiveSourceWarming){
            auto& reuse=ReuseFor(s,key);
            const uint32_t now=GetTickCount();
            if(LowReuse(reuse)){
                // Allow a recovery read every five seconds. Lookup remains
                // active, so ready hits can restore normal admission sooner.
                if(reuse.probing && uint32_t(now-reuse.nextProbe)>=0x80000000u){
                    ++s.lowReuseSkipped;return false;
                }
                reuse.nextProbe=now+5000;reuse.probing=true;
            }
        }
    }
    HANDLE duplicate=INVALID_HANDLE_VALUE;
    if(!DuplicateHandle(GetCurrentProcess(),original,GetCurrentProcess(),
                        &duplicate,0,FALSE,DUPLICATE_SAME_ACCESS)){
        ++s.errors;return false;
    }
    try {
        std::unique_lock<std::mutex> guard(s.mutex,std::try_to_lock);
        if(!guard.owns_lock()||s.stop||s.pending.size()>=s.cfg.maxPending||
           s.inFlight.contains(key)||s.local.contains(key)){
            CloseHandle(duplicate);++s.dropped;return false;
        }
        s.inFlight.insert(key);
        try {s.pending.push_back({key,duplicate,observedKind});}
        catch(...){s.inFlight.erase(key);throw;}
        ++s.queued;
        s.queuedBytes.fetch_add(key.length,std::memory_order_relaxed);
    }catch(...){CloseHandle(duplicate);++s.errors;return false;}
    s.cv.notify_one();
    return true;
}
// Native GTA SA 1.0 US CdStream keeps its IMG handles in this array.
// Only these handles can enter the process-wide ReadFile interception.
// We NEVER replace asynchronous completions or another mod's file contents.
bool IsGtaStreamHandle(HANDLE candidate){
    if(!candidate || candidate==INVALID_HANDLE_VALUE ||
       reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))!=0x400000)
        return false;
    constexpr uintptr_t kStreamHandles=0x8E4010; // gta-reversed CdStreamInfo
    const auto* slots=reinterpret_cast<const HANDLE*>(kStreamHandles);
    if(!Committed(slots,32*sizeof(HANDLE)))return false;
    return asc::archive_io::IsStreamHandle(candidate,slots,32);
}


// The game uses CdStreamRead to queue archive sectors. The OS ReadFile hook
// never observed any of those handles on the reported installation. This
// detour runs at the actual request boundary; on a miss GTA's original
// queue + worker + synchronization semantics are left entirely unchanged.
// On an idle-channel cache hit we copy READY bytes and leave the channel in
// the same successful idle state GTA's CdStreamSync/GetStatus expects. No
// synthetic overlapped I/O, semaphore signal, or worker queue entry is used.
bool __cdecl CdStreamDetour(int streamId,void* destination,uint32_t packedPos,
                            int sectors) {
    auto& s=S();
    CostSample sample(s.cdHookCost);
    const auto fallback=[&](){
        return s.cdOriginal ? s.cdOriginal(streamId,destination,packedPos,sectors) : false;
    };
    if(!s.cdActive.load(std::memory_order_acquire)||!s.cdOriginal)
        return fallback();
    ++s.cdCalls;
    if(!s.cfg.serveCdStreamHits){++s.cdSkipped;return fallback();}
    // Connection/retry is handled independently by the worker. No cache hit
    // or fetch is possible yet; skip metadata, probes and duplicate handles.
    if(!s.connected.load(std::memory_order_acquire)){
        ++s.cdSkipDisconnected;++s.cdSkipped;return fallback();
    }
    // These globals occupy one contiguous native data span. Validate it once
    // instead of issuing separate VirtualQuery calls for the same region.
    // Image base was already verified before this detour could be enabled.
    if(s.cdLayoutRejected.load(std::memory_order_relaxed) ||
       !Committed(reinterpret_cast<const void*>(kCdInitializedAddress),
           kCdLastPositionAddress+sizeof(uint32_t)-kCdInitializedAddress)){
        ++s.cdSkipAddresses;++s.cdSkipped;return fallback();
    }
    const auto* body=reinterpret_cast<const uint8_t*>(asc::archive_io::HoodlumCdReadBody);
    if(!Committed(body,asc::archive_io::HoodlumCdReadBodyPrefix.size()) ||
       !asc::archive_io::HasKnownHoodlumCdReadBody(body)){
        // A late mod may relocate native tables without changing the entry
        // prologue. Never interpret its reads through stale vanilla addresses.
        s.cdLayoutRejected.store(true,std::memory_order_relaxed);
        ++s.cdSkipAddresses;++s.cdSkipped;return fallback();
    }
    const int channelCount=*reinterpret_cast<const int*>(kCdChannelCountAddress);
    auto* channels=*reinterpret_cast<GtaCdChannel* const*>(kCdChannelsAddress);
    if(channelCount<1 || channelCount>16 || !channels ||
       streamId<0 || streamId>=channelCount ||
       !Committed(channels,size_t(channelCount)*sizeof(GtaCdChannel))) {
        ++s.cdSkipChannels;++s.cdSkipped;return fallback();
    }
    asc::archive_io::CdRead read{};
    if(!asc::archive_io::DecodeCdRead(packedPos,sectors,kMaxRead,read)){
        ++s.cdSkipDecode;++s.cdSkipped;return fallback();
    }
    const auto* handles=reinterpret_cast<const HANDLE*>(kCdHandlesAddress);
    HANDLE file=handles[read.fileId];
    if(!file || file==INVALID_HANDLE_VALUE){
        ++s.cdSkipHandle;
        if(!file)++s.cdSkipHandleNull;
        else ++s.cdSkipHandleInvalid;
        s.cdLastMissingFileId.store(read.fileId,std::memory_order_relaxed);
        const HANDLE channelFile=channels[streamId].file;
        if(channelFile && channelFile!=INVALID_HANDLE_VALUE)
            ++s.cdNullHandleChannelFilePresent;
        ++s.cdSkipped;
        // Count successful original reads when GTA's supposed IMG slot is
        // empty. This distinguishes alternate read providers from true errors.
        const bool nativeAccepted=fallback();
        if(nativeAccepted && !file)++s.cdNullHandleNativeAccepted;
        return nativeAccepted; // Preserve native GTA stream behavior.
    }
    // The file metadata and archive suffix identify the exact source bytes.
    // Refuse modified/virtual readers; never substitute raw IMG content for
    // bytes synthesized by an unrelated mod.
    Key key{};
    if(!SourceKey(file,read.byteOffset,read.byteCount,key)||
       !SupportedArchive(file,key)){
        ++s.cdSkipSource;++s.cdSkipped;return fallback();
    }
    ++s.cdEligible;
    auto& ch=channels[streamId];
    const bool initialized=*reinterpret_cast<const int*>(kCdInitializedAddress)!=0;
    const bool channelIdle=ch.sectorsToRead==0 && ch.inUse==0;
    if(s.cfg.serveCdStreamHits && s.connected.load(std::memory_order_acquire) &&
       initialized && channelIdle && read.byteCount<=s.cfg.maxCdStreamCopyKiB*1024u &&
       Writable(&ch,sizeof(ch)) &&
       Writable(reinterpret_cast<void*>(kCdLastPositionAddress),sizeof(uint32_t)) &&
       asc::archive_io::CanServeCdStream(
           initialized,channelIdle,Writable(destination,read.byteCount),
           true,read.byteCount,s.cfg.maxCdStreamCopyKiB*1024u) &&
       Lookup(key,destination)){
        // Preserve the observed game status and handle contract without
        // queuing a new read or disturbing another stream's completion.
        ch.file=file;
        ch.buffer=destination;
        ch.sectorOffset=read.sectorOffset;
        ch.locked=0;
        ch.status=0; // eCdStreamStatus::READING_SUCCESS
        *reinterpret_cast<uint32_t*>(kCdLastPositionAddress)=packedPos+uint32_t(sectors);
        ++s.cdHits;
        s.suppliedBytes.fetch_add(read.byteCount,std::memory_order_relaxed);
        SetLastError(ERROR_SUCCESS);
        return true;
    }
    ++s.cdMisses;
    const bool accepted=fallback();
    const DWORD nativeError=GetLastError();
    if(accepted && Queue(file,key))++s.cdQueued;
    SetLastError(nativeError);
    return accepted;
}
// On this legacy GTA SA HOODLUM executable, the canonical .text entry and
// relocated function body can both be mapped PAGE_READWRITE. MinHook rejects
// the .text entry with MH_ERROR_NOT_EXECUTABLE (7), even if GTA is able to
// execute the original code under its process DEP policy. Never alter unknown
// pages: verify BOTH stub and relocated body, and recover only the entry page.
bool HostExecutableSection(uintptr_t address) {
    const uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if(base!=0x400000 || address<base)return false;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(!Committed(dos,sizeof(*dos)) || dos->e_magic!=IMAGE_DOS_SIGNATURE ||
       dos->e_lfanew<64 || dos->e_lfanew>4096)return false;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(!Committed(nt,sizeof(*nt)) || nt->Signature!=IMAGE_NT_SIGNATURE ||
       nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
       nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
       nt->FileHeader.NumberOfSections>96)return false;
    if(address-base>=nt->OptionalHeader.SizeOfImage)return false;
    const auto* sections=IMAGE_FIRST_SECTION(nt);
    if(!Committed(sections,nt->FileHeader.NumberOfSections*sizeof(*sections)))
       return false;
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){
        const auto& sec=sections[i];
        const uintptr_t begin=sec.VirtualAddress;
        const uintptr_t span=std::max(sec.Misc.VirtualSize,sec.SizeOfRawData);
        if(address-base>=begin && address-base-begin<span)
            return (sec.Characteristics&IMAGE_SCN_MEM_EXECUTE)!=0;
    }
    return false;
}
bool IsExecutablePage(DWORD protection) {
    switch(protection&0xff){
        case PAGE_EXECUTE:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY: return true;
        default: return false;
    }
}
bool VerifyCdReadHookPage(void* target,std::ostream& log){
    MEMORY_BASIC_INFORMATION mbi{};
    if(VirtualQuery(target,&mbi,sizeof(mbi))!=sizeof(mbi)){
        log<<"AssetCache64: CdStreamRead target VirtualQuery failed\n";
        return false;
    }
    log<<"AssetCache64: CdStreamRead target addr=0x"<<std::hex
       <<reinterpret_cast<uintptr_t>(target)<<", protect=0x"<<mbi.Protect
       <<", state=0x"<<mbi.State<<", type=0x"<<mbi.Type<<std::dec<<"\n";
    if(mbi.State!=MEM_COMMIT || (mbi.Protect&(PAGE_NOACCESS|PAGE_GUARD)) ||
       mbi.Type!=MEM_IMAGE ||
       !HostExecutableSection(reinterpret_cast<uintptr_t>(target))){
        log<<"AssetCache64: CdStreamRead target is not verified host executable code; no hook\n";
        return false;
    }
    if(IsExecutablePage(mbi.Protect))return true;
    // 0x406A20 is an E9 redirect to 0x0156C2C0 on the documented
    // HOODLUM build. Inspect BOTH the stub and its genuine function prologue.
    // Some legacy GTA executables execute the .HOODLUM body from PAGE_READWRITE
    // mappings (process DEP/NX policy); its page protection alone is not proof
    // that the code is invalid. Crucially, do NOT change the body's protection:
    // it shares pages with other HOODLUM content that may require write access.
    constexpr uintptr_t kBody=asc::archive_io::HoodlumCdReadBody;
    const auto* entry=static_cast<const uint8_t*>(target);
    const auto* bodyBytes=reinterpret_cast<const uint8_t*>(kBody);
    const uintptr_t regionEnd=reinterpret_cast<uintptr_t>(mbi.BaseAddress)+mbi.RegionSize;
    MEMORY_BASIC_INFORMATION body{};
    if(mbi.Protect!=PAGE_READWRITE ||
       uintptr_t(entry)>regionEnd ||
       asc::archive_io::HoodlumCdReadEntry.size()>regionEnd-uintptr_t(entry) ||
       !HostExecutableSection(kBody) ||
       VirtualQuery(bodyBytes,&body,sizeof(body))!=sizeof(body) ||
       body.State!=MEM_COMMIT || body.Type!=MEM_IMAGE ||
       (body.Protect&(PAGE_NOACCESS|PAGE_GUARD)) ||
       !Committed(bodyBytes,asc::archive_io::HoodlumCdReadBodyPrefix.size()) ||
       !asc::archive_io::IsKnownHoodlumCdRead(entry,bodyBytes) ||
       reinterpret_cast<uintptr_t>(entry)+5+int32_t(0x0116589B)!=kBody){
        log<<"AssetCache64: CdStreamRead HOODLUM stub/body fingerprint mismatch; "
              "native loader untouched\n";
        return false;
    }
    if(!IsExecutablePage(body.Protect))
        log<<"AssetCache64: verified HOODLUM CdStreamRead body mapped "
              "non-executable; preserving original body protection\n";
    // Only the 5-byte host entry is made executable for MinHook.
    DWORD oldProtection=0;
    if(!VirtualProtect(target,asc::archive_io::HoodlumCdReadEntry.size(),
                       PAGE_EXECUTE_READ,&oldProtection)){
        log<<"AssetCache64: CdStreamRead verified entry RX recovery failed\n";
        return false;
    }
    MEMORY_BASIC_INFORMATION updated{};
    const bool recovered=VirtualQuery(target,&updated,sizeof(updated))==sizeof(updated) &&
                          IsExecutablePage(updated.Protect);
    log<<(recovered ? "AssetCache64: CdStreamRead verified HOODLUM entry recovered RW -> RX\n"
                    : "AssetCache64: CdStreamRead RX recovery verification failed\n");
    // Never restore PAGE_READWRITE: GTA must be able to execute this entry.
    return recovered;
}

bool PatchCdStreamRead(std::ostream& log) {
    auto& s=S();
    const auto target=reinterpret_cast<void*>(kCdReadAddress);
    if(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))!=0x400000 ||
       !Committed(target,8)) {
        log<<"AssetCache64: CdStreamRead unsupported executable; native loader untouched\n";
        return false;
    }
    // v14.7 implemented this guarded recovery but never invoked it. Without
    // this preflight MinHook immediately rejects a PAGE_READWRITE HOODLUM
    // entry with MH_ERROR_NOT_EXECUTABLE, leaving all CdStream counters zero.
    // This routine is fail-closed: unknown code and page protections are left
    // untouched. Run it BEFORE reading the entry bytes or creating the hook.
    if(!VerifyCdReadHookPage(target,log)){
        log<<"AssetCache64: CdStreamRead executable-page preflight declined; "
              "native loader untouched\n";
        return false;
    }
    // This deployment targets the documented HOODLUM build. An executable
    // entry page is NOT sufficient proof that the relocated body/layout is
    // compatible; verify both fingerprints regardless of page protection.
    const auto* entry=static_cast<const uint8_t*>(target);
    const auto* body=reinterpret_cast<const uint8_t*>(
        asc::archive_io::HoodlumCdReadBody);
    if(!Committed(body,asc::archive_io::HoodlumCdReadBodyPrefix.size()) ||
       !HostExecutableSection(asc::archive_io::HoodlumCdReadBody) ||
       !asc::archive_io::IsKnownHoodlumCdRead(entry,body)){
        log<<"AssetCache64: unsupported/modified HOODLUM CdStreamRead "
               "fingerprint; native loader untouched\n";
        return false;
    }
    // MinHook checks BOTH the target and the detour for execute permission.
    // Report an unexpected non-executable ASI detour separately instead of
    // misdiagnosing that failure as a game-entry protection problem.
    MEMORY_BASIC_INFORMATION detourPage{};
    if(VirtualQuery(reinterpret_cast<const void*>(&CdStreamDetour),
                    &detourPage,sizeof(detourPage))!=sizeof(detourPage) ||
       detourPage.State!=MEM_COMMIT ||
       !IsExecutablePage(detourPage.Protect)){
        log<<"AssetCache64: CdStreamRead detour code is not executable; "
              "native loader untouched\n";
        return false;
    }
    // Some GTA installations use a same-image JMP to the relocated .HOODLUM
    // body. Allow that, but never override another mod's external trampoline.
    const auto* code=static_cast<const uint8_t*>(target);
    if(code[0]==0xE9){
        int32_t rel=0;
        std::memcpy(&rel,code+1,sizeof(rel));
        const auto* dest=reinterpret_cast<void*>(kCdReadAddress+5+rel);
        HMODULE owner=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(dest),&owner) ||
           owner!=GetModuleHandleW(nullptr)){
            log<<"AssetCache64: foreign CdStreamRead detour; engine hook declined\n";
            return false;
        }
    }
    const auto initialized=MH_Initialize();
    if(initialized!=MH_OK && initialized!=MH_ERROR_ALREADY_INITIALIZED)
        return false;
    void* trampoline=nullptr;
    const auto status=MH_CreateHook(target,reinterpret_cast<void*>(&CdStreamDetour),
                                    &trampoline);
    if(status!=MH_OK || !trampoline){
        log<<"AssetCache64: CdStreamRead hook creation failed: "
           <<MH_StatusToString(status)<<" ("<<int(status)<<")\n";
        return false;
    }
    s.cdOriginal=reinterpret_cast<CdReadFn>(trampoline);
    if(MH_EnableHook(target)!=MH_OK){
        s.cdOriginal=nullptr;
        MH_RemoveHook(target);
        log<<"AssetCache64: CdStreamRead hook enable failed\n";
        return false;
    }
    s.cdHookTarget=target;
    s.cdLayoutRejected.store(false,std::memory_order_relaxed);
    s.cdActive.store(true,std::memory_order_release);
    log<<"AssetCache64: native GTA CdStreamRead hook active; ready idle-channel "
          "hits direct, misses native GTA queue, no synthetic overlapped I/O\n";
    return true;
}
// Module-local IAT detour, NOT an OS-wide ReadFile hook. Mod Loader's
// std.stream/backend.cpp CdStreamThread reads a selected loose file through
// its own HANDLE and treats immediate ReadFile(TRUE) as success. It only calls
// GetOverlappedResult on the native ERROR_IO_PENDING path. We retain that
// behavior, including OVERLAPPED success fields, on verified READY L1 hits.
// All misses run the original ReadFile and its existing completion logic.
BOOL WINAPI ModLoaderReadDetour(HANDLE file,LPVOID buffer,DWORD count,
                                LPDWORD done,LPOVERLAPPED ov){
    auto& s=S();
    CostSample sample(s.modLoaderHookCost);
    auto native=s.modLoaderOriginal;
    if(!native){SetLastError(ERROR_PROC_NOT_FOUND);return FALSE;}
    if(!s.modLoaderIatActive.load(std::memory_order_acquire)||
       !s.connected.load(std::memory_order_acquire))return NativeRead(native,file,buffer,count,done,ov);
    ++s.modLoaderCalls;
    const uint32_t maxBytes=s.cfg.maxModLoaderReadMiB*1048576u;
    if(!ov||!buffer||!done||!count||count>maxBytes||
       !Writable(ov,sizeof(OVERLAPPED))||
       !Writable(buffer,count)||!Writable(done,sizeof(DWORD))){
        ++s.modLoaderPassThrough;
        return NativeRead(native,file,buffer,count,done,ov);
    }
    const uint64_t offset=asc::archive_io::OverlappedOffset(ov->Offset,ov->OffsetHigh);
    Key key{};
    if(!SourceKey(file,offset,count,key)){
        ++s.modLoaderPassThrough;return NativeRead(native,file,buffer,count,done,ov);
    }
    const uint64_t fileBytes=(uint64_t(key.sizeHigh)<<32)|key.sizeLow;
    const auto sourceKind=ModLoaderSourceKind(file,key);
    if(!asc::modloader_cache::EligibleRead(sourceKind,fileBytes,offset,count,
                                           maxBytes,true,true)){
        ++s.modLoaderPassThrough;return NativeRead(native,file,buffer,count,done,ov);
    }
    if(sourceKind==asc::modloader_cache::SourceKind::Img)++s.modLoaderImgReads;
    else ++s.modLoaderLooseReads;
    ++s.modLoaderEligible;
    // CdStreamSync uses Mod Loader's own worker wakeup when streaming is
    // initialized. Avoid supplying synthetic fast completions during the
    // legacy/uninitialized GetOverlappedResult path.
    const bool workerInitialized=Committed(
        reinterpret_cast<const void*>(kCdInitializedAddress),sizeof(int)) &&
        *reinterpret_cast<const int*>(kCdInitializedAddress)!=0;
    if(s.cfg.serveModLoaderReadyHits && workerInitialized && Lookup(key,buffer)){
        // Windows immediate overlapped completion: no pending operation exists
        // and the Mod Loader worker takes the direct ReadFile(TRUE) branch.
        *done=count;
        ov->Internal=0; // STATUS_SUCCESS
        ov->InternalHigh=count;
        if(ov->hEvent && !(reinterpret_cast<uintptr_t>(ov->hEvent)&1u))
            SetEvent(ov->hEvent);
        ++s.modLoaderHits;
        if(sourceKind==asc::modloader_cache::SourceKind::Img)++s.modLoaderImgHits;
        else ++s.modLoaderLooseHits;
        s.suppliedBytes.fetch_add(count,std::memory_order_relaxed);
        SetLastError(ERROR_SUCCESS);
        return TRUE;
    }
    ++s.modLoaderMisses;
    const BOOL result=NativeRead(native,file,buffer,count,done,ov);
    const DWORD nativeError=GetLastError();
    if(result || nativeError==ERROR_IO_PENDING){
        if(!result)++s.modLoaderNativePending;
        if(Queue(file,key,sourceKind))++s.modLoaderQueued;
    }
    SetLastError(nativeError);
    return result;
}

BOOL WINAPI ReadDetour(HANDLE file,LPVOID buffer,DWORD count,
                       LPDWORD done,LPOVERLAPPED ov){
    auto& s=S();
    CostSample sample(s.readHookCost);
    if(!s.original){SetLastError(ERROR_PROC_NOT_FOUND);return FALSE;}
    if(!s.active.load(std::memory_order_acquire))
        return NativeRead(s.original,file,buffer,count,done,ov);
    ++s.interceptedCalls;
    if(s.processReaderHook){
        // Hooking the Win32 implementation observes GTA's streaming worker
        // even when that worker bypasses the executable's ReadFile IAT.
        // All OTHER handles, including ASI, save and named-pipe traffic,
        // take the native trampoline without any further inspection.
        if(!IsGtaStreamHandle(file)){
            ++s.otherHandleCalls;
            return NativeRead(s.original,file,buffer,count,done,ov);
        }
        ++s.gtaStreamHandleCalls;
    }
    // GTA's CdStream uses overlapped ReadFile on many SA installations.
    // Never fake an OVERLAPPED completion: IOCP/event/GetOverlappedResult,
    // cancellation and the kernel's file-position semantics must remain intact.
    // Unlike v14.3, do not reject legitimate reads via hard-coded return
    // address or thread-id checks. Validate the real source handle instead.
    const bool async=(ov!=nullptr);
    if(async)++s.overlappedCalls;else ++s.synchronousCalls;
    const auto mode=asc::archive_io::Classify(
        count,buffer!=nullptr,done!=nullptr,async,kMaxRead);
    if(mode==asc::archive_io::ReadMode::PassThrough){
        if(count>kMaxRead)++s.oversizedCalls;
        return NativeRead(s.original,file,buffer,count,done,ov);
    }
    if(mode==asc::archive_io::ReadMode::WarmOnlyOverlapped) {
        if(!Committed(ov,sizeof(OVERLAPPED)))
            return NativeRead(s.original,file,buffer,count,done,ov);
        // Use explicit OVERLAPPED offsets only as a hint for background
        // warming; the GTA/Windows loader always performs the real read.
        // Inspecting OVERLAPPED is safe here because the caller passed it to
        // ReadFile and must keep it alive until completion.
        const uint64_t offset=asc::archive_io::OverlappedOffset(ov->Offset,ov->OffsetHigh);
        const BOOL result=NativeRead(s.original,file,buffer,count,done,ov);
        const DWORD originalError=GetLastError();
        if(result || originalError==ERROR_IO_PENDING){
            if(!result)++s.nativePending;
            Key key{};
            if(SourceKey(file,offset,count,key) && SupportedArchive(file,key)){
                ++s.inspectedImg;
                if(Queue(file,key))++s.overlappedQueued;
            }else ++s.rejectedSource;
        }
        SetLastError(originalError);
        return result;
    }
    // Only synchronous original IMG reads can be satisfied from ready
    // local bytes while preserving ReadFile's count and file-position result.
    // Invalid/read-only caller buffers must go to the real Win32 API (which
    // returns its normal error) rather than crashing in our memcpy.
    if(!Writable(buffer,count)||!Writable(done,sizeof(DWORD)))
        return NativeRead(s.original,file,buffer,count,done,ov);
    LARGE_INTEGER at{};
    if(!SetFilePointerEx(file,{},&at,FILE_CURRENT)||at.QuadPart<0){
        ++s.rejectedSource;
        return NativeRead(s.original,file,buffer,count,done,ov);
    }
    Key key{};
    if(!SourceKey(file,uint64_t(at.QuadPart),count,key) ||
       !SupportedArchive(file,key)){
        ++s.rejectedSource;
        return NativeRead(s.original,file,buffer,count,done,ov);
    }
    ++s.inspectedImg;
    ++s.eligibleCalls;
    if(!s.safeSourceReader){
        // In case a foreign hook gets installed after startup, preserve
        // its output and only observe/queue source data.
        const BOOL ok=NativeRead(s.original,file,buffer,count,done,ov);
        const DWORD originalError=GetLastError();
        if(ok&&*done==count){
            ++s.observedReads;
            s.observedBytes.fetch_add(count,std::memory_order_relaxed);
            Queue(file,key);
        }
        SetLastError(originalError);
        return ok;
    }
    if(Lookup(key,buffer)){
        LARGE_INTEGER next{};next.QuadPart=at.QuadPart+count;
        if(SetFilePointerEx(file,next,nullptr,FILE_BEGIN)){
            *done=count;
            ++s.hits;
            s.suppliedBytes.fetch_add(count,std::memory_order_relaxed);
            SetLastError(ERROR_SUCCESS);return TRUE;
        }
        // Use the original ReadFile if the pointer cannot be advanced.
    }else ++s.misses;
    const BOOL result=NativeRead(s.original,file,buffer,count,done,ov);
    const DWORD originalError=GetLastError();
    if(result&&*done==count)Queue(file,key);
    SetLastError(originalError);
    return result;
}
bool Replace(void** slot,void* expected,void* replacement){
    DWORD old=0;
    if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old))return false;
    const bool ok=InterlockedCompareExchangePointer(slot,replacement,expected)==expected;
    DWORD ignored=0;VirtualProtect(slot,sizeof(void*),old,&ignored);
    return ok;
}
// On modern Windows a genuine kernel32/kernelbase API can begin with a
// forwarding JMP. The old "any JMP = foreign hook" test rejected these.
// Permit a forward only when its destination is another trusted Windows
// module; a JMP into a third-party ASI/DLL must never be bypassed.
bool SystemReadFileAddress(const void* ptr) {
    HMODULE module=nullptr;
    if(!ptr || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(ptr),&module))return false;
    return module==GetModuleHandleW(L"kernel32.dll") ||
           module==GetModuleHandleW(L"kernelbase.dll");
}
bool SafeWindowsReader(void* pointer){
    // Bounded forwarder walk, not merely a first-byte check. A system thunk
    // pointing to a third-party trampoline is an observe-only provider.
    void* current=pointer;
    std::array<void*,4> visited{};
    for(unsigned hop=0;hop<visited.size();++hop){
        if(!SystemReadFileAddress(current)||!Committed(current,6))return false;
        for(unsigned n=0;n<hop;++n)if(visited[n]==current)return false;
        visited[hop]=current;
        const auto* code=static_cast<const uint8_t*>(current);
        if(code[0]==0xE9){
            int32_t rel=0;std::memcpy(&rel,code+1,sizeof(rel));
            current=reinterpret_cast<void*>(uintptr_t(current)+5+rel);
        }else if(code[0]==0xEB){
            const intptr_t rel=static_cast<int8_t>(code[1]);
            current=reinterpret_cast<void*>(uintptr_t(current)+2+rel);
        }else if(code[0]==0xFF && code[1]==0x25){
            uint32_t address=0;std::memcpy(&address,code+2,sizeof(address));
            const auto* indirect=reinterpret_cast<void* const*>(uintptr_t(address));
            if(!Committed(indirect,sizeof(void*)))return false;
            current=*indirect;
        }else return true;
    }
    // Unusually long or cyclic forwarding chains are not safe to bypass.
    return false;
}
// Find *only* std.stream.dll's ReadFile import. Never patch kernelbase,
// GTA, or the executable. If another mod already owns this IAT, decline.
void** FindModLoaderReadImport(HMODULE dll){
    if(!dll)return nullptr;
    const auto base=reinterpret_cast<uintptr_t>(dll);
    auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(!Committed(dos,sizeof(*dos))||dos->e_magic!=IMAGE_DOS_SIGNATURE||
       dos->e_lfanew<64||dos->e_lfanew>4096)return nullptr;
    auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(!Committed(nt,sizeof(*nt))||nt->Signature!=IMAGE_NT_SIGNATURE||
       nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC)return nullptr;
    const auto& d=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    const auto size=nt->OptionalHeader.SizeOfImage;
    if(!d.VirtualAddress||d.VirtualAddress>=size||
       d.Size>size-d.VirtualAddress)return nullptr;
    auto* imports=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+d.VirtualAddress);
    for(unsigned j=0;j<d.Size/sizeof(*imports);++j){
        if(!Committed(&imports[j],sizeof(*imports)))break;
        const auto& imp=imports[j];
        if(!imp.Name)break;
        if(!imp.OriginalFirstThunk||!imp.FirstThunk)continue;
        for(unsigned i=0;i<65536;++i){
            const uintptr_t lookup=base+imp.OriginalFirstThunk+
                                    i*sizeof(IMAGE_THUNK_DATA32);
            const uintptr_t address=base+imp.FirstThunk+i*sizeof(IMAGE_THUNK_DATA32);
            if(lookup+sizeof(IMAGE_THUNK_DATA32)>base+size ||
               address+sizeof(IMAGE_THUNK_DATA32)>base+size)break;
            auto* name=reinterpret_cast<const IMAGE_THUNK_DATA32*>(lookup);
            auto* thunk=reinterpret_cast<IMAGE_THUNK_DATA32*>(address);
            if(!Committed(name,sizeof(*name))||!Committed(thunk,sizeof(*thunk))||
               !name->u1.AddressOfData)break;
            if(IMAGE_SNAP_BY_ORDINAL32(name->u1.Ordinal))continue;
            const uintptr_t labelAddress=base+name->u1.AddressOfData;
            if(labelAddress+sizeof(WORD)+9>base+size)continue;
            auto* label=reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(labelAddress);
            if(!Committed(label,sizeof(WORD)+9) ||
               std::memcmp(label->Name,"ReadFile",9)!=0)continue;
            return reinterpret_cast<void**>(&thunk->u1.Function);
        }
    }
    return nullptr;
}
void PatchModLoaderReadImport(){
    auto& s=S();
    if(s.stop.load(std::memory_order_acquire) || s.modLoaderIatActive.load() ||
       !s.cfg.interceptModLoaderReads)return;
    HMODULE dll=GetModuleHandleW(L"std.stream.dll");
    if(!dll){s.modLoaderHookState=1;return;}
    void** slot=FindModLoaderReadImport(dll);
    if(!slot){s.modLoaderHookState=3;return;}
    void* original=*slot;
    if(!SafeWindowsReader(original)){
        s.modLoaderHookState=4;return;
    }
    s.modLoaderOriginal=reinterpret_cast<ReadFn>(original);
    if(!Replace(slot,original,reinterpret_cast<void*>(&ModLoaderReadDetour))){
        s.modLoaderOriginal=nullptr;
        s.modLoaderHookState=4;return;
    }
    s.modLoaderImportSlot=slot;
    s.modLoaderModule=dll;
    s.modLoaderIatActive.store(true,std::memory_order_release);
    s.modLoaderHookState=2;
}

// IAT interception sees unrelated Win32 reads on some ASI installations.
// An opt-in, verified system-API hook reaches the actual GTA CdStream worker
// regardless of which import thunk it used. Its detour is gated by the
// original GTA stream handle table above; all other calls go straight through.
// On a modified Windows entrypoint we decline this hook rather than chaining
// through an unknown patch or manufacturing overlapped completions.
bool PatchProcessReadFile(std::ostream& log) {
    auto& s=S();
    HMODULE module=GetModuleHandleW(L"kernelbase.dll");
    if(!module)module=GetModuleHandleW(L"kernel32.dll");
    if(!module)return false;
    void* target=reinterpret_cast<void*>(GetProcAddress(module,"ReadFile"));
    if(!target||!SafeWindowsReader(target)){
        log<<"AssetCache64: system ReadFile already redirected or unverifiable; "
              "process hook skipped\n";
        return false;
    }
    const auto init=MH_Initialize();
    if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED){
        log<<"AssetCache64: MinHook initialization unavailable for CdStream reads\n";
        return false;
    }
    void* trampoline=nullptr;
    const auto created=MH_CreateHook(target,
            reinterpret_cast<void*>(&ReadDetour),&trampoline);
    if(created!=MH_OK||!trampoline){
        log<<"AssetCache64: cannot install process ReadFile hook, status="
           <<static_cast<int>(created)<<"\n";
        return false;
    }
    // The trampoline, not GetProcAddress, is the original function after
    // MH_EnableHook. Otherwise cache misses would recurse indefinitely.
    s.original=reinterpret_cast<ReadFn>(trampoline);
    s.safeSourceReader=true;
    s.nativeHookTarget=target;
    s.processReaderHook=true;
    const auto enabled=MH_EnableHook(target);
    if(enabled!=MH_OK){
        s.processReaderHook=false;
        s.nativeHookTarget=nullptr;
        s.original=nullptr;
        MH_RemoveHook(target);
        log<<"AssetCache64: enable process reader hook failed, status="
           <<static_cast<int>(enabled)<<"\n";
        return false;
    }
    s.active.store(true,std::memory_order_release);
    log<<"AssetCache64: GTA CdStream handle-gated process ReadFile hook active; "
          "all non-stream handles pass through, overlapped completions native\n";
    return true;
}

bool PatchGtaReadFile(std::ostream& log){
    auto& s=S();
    const uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if(base!=0x400000)return false;
    auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(!Committed(dos,sizeof(*dos))||dos->e_magic!=IMAGE_DOS_SIGNATURE||
       dos->e_lfanew<64||dos->e_lfanew>4096)return false;
    auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(!Committed(nt,sizeof(*nt))||nt->Signature!=IMAGE_NT_SIGNATURE||
       nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC)return false;
    const auto& d=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!d.VirtualAddress||d.Size<sizeof(IMAGE_IMPORT_DESCRIPTOR)||
       d.VirtualAddress>=nt->OptionalHeader.SizeOfImage||
       d.Size>nt->OptionalHeader.SizeOfImage-d.VirtualAddress)return false;
    const uintptr_t end=base+nt->OptionalHeader.SizeOfImage;
    auto* imports=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+d.VirtualAddress);
    for(unsigned j=0;j<d.Size/sizeof(*imports);++j){
        if(!Committed(&imports[j],sizeof(*imports)))break;
        const auto& imp=imports[j];
        if(!imp.Name)break;
        if(!imp.OriginalFirstThunk||!imp.FirstThunk)continue;
        for(unsigned i=0;i<65536;++i){
            const uintptr_t lookup=base+imp.OriginalFirstThunk+i*sizeof(IMAGE_THUNK_DATA32);
            const uintptr_t address=base+imp.FirstThunk+i*sizeof(IMAGE_THUNK_DATA32);
            if(lookup+sizeof(IMAGE_THUNK_DATA32)>end ||
               address+sizeof(IMAGE_THUNK_DATA32)>end)break;
            auto* name=reinterpret_cast<const IMAGE_THUNK_DATA32*>(lookup);
            auto* thunk=reinterpret_cast<IMAGE_THUNK_DATA32*>(address);
            if(!Committed(name,sizeof(*name))||!Committed(thunk,sizeof(*thunk))||
               !name->u1.AddressOfData)break;
            if(IMAGE_SNAP_BY_ORDINAL32(name->u1.Ordinal))continue;
            const uintptr_t labelAddr=base+name->u1.AddressOfData;
            if(labelAddr+sizeof(WORD)+9>end)continue;
            auto* label=reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(labelAddr);
            if(!Committed(label,sizeof(WORD)+9) ||
               std::memcmp(label->Name,"ReadFile",9)!=0)continue;
            void** slot=reinterpret_cast<void**>(&thunk->u1.Function);
            void* reader=*slot;
            s.safeSourceReader = SafeWindowsReader(reader);
            if(!s.safeSourceReader){
                log<<"AssetCache64: foreign ReadFile provider; hook skipped for mod compatibility\n";
                return false;
            }
            s.original=reinterpret_cast<ReadFn>(reader);
            if(!Replace(slot,reader,reinterpret_cast<void*>(&ReadDetour)))
                return false;
            s.importSlot=slot;s.active.store(true,std::memory_order_release);
            log<<"AssetCache64: original GTA IAT ReadFile hook installed; sync IMG cache hits only\n";
            return true;
        }
    }
    log<<"AssetCache64: GTA ReadFile import unavailable; engine I/O unchanged\n";
    return false;
}
// Enumerate IMG sources *only on the cache worker*. Both vanilla models\ and
// modloader\ archives are candidates. Mod Loader's effective override order
// cannot be inferred from a directory scan: native CdStream reads still own the
// authoritative content and loose overrides remain with the native loader.
std::vector<std::wstring> DiscoverArchives(const Settings& cfg){
    std::vector<std::wstring> paths;
    wchar_t exe[32768]{};
    const DWORD n=GetModuleFileNameW(nullptr,exe,DWORD(std::size(exe)));
    if(!n||n>=std::size(exe))return paths;
    namespace fs=std::filesystem;
    std::error_code ec;
    const fs::path gameRoot=fs::path(exe).parent_path();
    const auto scan=[&](const fs::path& dir){
        ec.clear();
        if(!fs::exists(dir,ec)||ec)return;
        fs::recursive_directory_iterator it(dir,
            fs::directory_options::skip_permission_denied,ec),end;
        unsigned visited=0;
        for(;it!=end && paths.size()<3072 && visited++<65536;it.increment(ec)){
            if(ec){ec.clear();continue;}
            auto status=it->symlink_status(ec);
            if(ec){ec.clear();continue;}
            if(fs::is_symlink(status)){it.disable_recursion_pending();continue;}
            if(!fs::is_regular_file(status))continue;
            const auto extension=it->path().extension().wstring();
            if(_wcsicmp(extension.c_str(),L".img")==0)
                paths.push_back(it->path().wstring());
        }
    };
    scan(gameRoot/L"models");
    if(cfg.discoverModLoaderArchives)scan(gameRoot/L"modloader");
    return paths;
}
// Only a few files are registered per worker cycle: demand fetches cannot
// wait behind a long sequence of OpenWarmSource pipe RPCs.
void WarmArchiveBatch(const std::shared_ptr<native_memory::Session>& session,
                      const std::vector<std::wstring>& paths,size_t& next,
                      std::vector<HANDLE>& retainedSources){
    constexpr size_t batch=4;
    size_t completed=0;
    while(next<paths.size() && completed++<batch && !S().stop.load()){
        const auto& path=paths[next++];
        HANDLE handle=CreateFileW(path.c_str(),GENERIC_READ,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE){++S().discoverRejected;continue;}
        if(!session->RegisterWarmSource(handle)){
            ++S().discoverRejected;CloseHandle(handle);continue;
        }
        try {
            retainedSources.push_back(handle);
            ++S().discoverOpened;
        }catch(...){
            // Session retains this source until shutdown, even if the client
            // cannot retain its local HANDLE. No stale handle is ever reused.
            CloseHandle(handle);
            throw;
        }
    }
}

// Called only by the worker. Recheck the selected HANDLE identity to avoid
// attaching a stale source to catalog hints after a file is replaced.
bool SelectedLooseDependency(const Work& job) {
    auto& s=S();
    if(job.observedKind!=asc::modloader_cache::SourceKind::Loose){
        ++s.dependencyWarmUnsafe;return false;
    }
    Key current{};
    if(!SourceKey(job.duplicated,job.key.offset,job.key.length,current) ||
       !(current==job.key)){
        ++s.dependencyWarmUnsafe;return false;
    }
    wchar_t path[32768]{};
    const DWORD n=GetFinalPathNameByHandleW(
        job.duplicated,path,DWORD(std::size(path)),FILE_NAME_NORMALIZED);
    if(!n || n>=std::size(path) ||
       asc::modloader_cache::ClassifySource(std::wstring_view(path,n))!=
           asc::modloader_cache::SourceKind::Loose){
        ++s.dependencyWarmUnsafe;return false;
    }
    // Only ASCII model source basenames are supported; Unicode conversion
    // guessing can associate an unrelated replacement with an IDE model.
    std::wstring_view full(path,n);
    const auto slash=full.find_last_of(L"\\/");
    const auto basename=full.substr(slash==std::wstring_view::npos?0:slash+1);
    if(basename.empty() || basename.size()>260){
        ++s.dependencyWarmUnsafe;return false;
    }
    std::string filename;
    filename.reserve(basename.size());
    for(const wchar_t ch:basename){
        if(ch<=0 || ch>127){
            ++s.dependencyWarmUnsafe;return false;
        }
        filename.push_back(char(ch));
    }
    asc::native::CatalogCellReply hints{};
    {
        std::lock_guard guard(s.catalogHintMutex);
        if(!s.catalogHintSerial){
            ++s.dependencyWarmNoHints;return false;
        }
        hints=s.catalogHints;
    }
    // A cell change can outrun the background IPC query. Do not warm against
    // stale hints belonging to the player's previous position.
    if(hints.cellX!=s.worldCellX.load(std::memory_order_relaxed) ||
       hints.cellY!=s.worldCellY.load(std::memory_order_relaxed)){
        ++s.dependencyWarmNoHints;return false;
    }
    const auto decision=asc::dependency_warm::MatchLooseFile(filename,hints);
    if(decision==asc::dependency_warm::Decision::Match){
        ++s.dependencyWarmMatched;return true;
    }
    if(decision==asc::dependency_warm::Decision::NoHints)
        ++s.dependencyWarmNoHints;
    else if(decision==asc::dependency_warm::Decision::Unsupported)
        ++s.dependencyWarmUnsafe;
    else
        ++s.dependencyWarmUnmatched;
    return false;
}
// IMG member warming uses the *selected live Mod Loader handle*, not a
// directory scan. A validated per-cell hint alone NEVER authorizes warming.
// All IMG parsing and disk I/O happens inside the 64-bit helper.
bool PromoteSelectedImgMember(
    const std::shared_ptr<native_memory::Session>& session,
    const Work& job,std::vector<Key>& members,
    asc::observed_warm::Usage& usage){
    auto& state=S();
    if(job.observedKind!=asc::modloader_cache::SourceKind::Img ||
       !state.cfg.warmVerifiedImgMembers)return false;
    Key current{};
    if(!SourceKey(job.duplicated,job.key.offset,job.key.length,current) ||
       !(current==job.key)){++state.imgExtentRejected;return false;}
    for(const auto& prior:members){
        if(prior.volume==job.key.volume && prior.high==job.key.high &&
           prior.low==job.key.low && prior.sizeHigh==job.key.sizeHigh &&
           prior.sizeLow==job.key.sizeLow &&
           prior.writeHigh==job.key.writeHigh &&
           prior.writeLow==job.key.writeLow &&
           job.key.offset>=prior.offset &&
           uint64_t(job.key.length)<=uint64_t(prior.length) &&
           job.key.offset-prior.offset<=uint64_t(prior.length)-job.key.length)
            return true; // already admitted this exact member
    }
    asc::native::CatalogCellReply hints{};
    {
        std::lock_guard guard(state.catalogHintMutex);
        if(!state.catalogHintSerial){++state.imgExtentRejected;return false;}
        hints=state.catalogHints;
    }
    if(hints.cellX!=state.worldCellX.load(std::memory_order_relaxed) ||
       hints.cellY!=state.worldCellY.load(std::memory_order_relaxed) ||
       !hints.modelCount || hints.modelCount>64){
        ++state.imgExtentRejected;return false;
    }
    using asc::observed_warm::Limits;
    const Limits limits{
        uint64_t(state.cfg.observedWarmMaxSourceMiB)*asc::native::MiB,
        uint64_t(state.cfg.observedWarmMaxTotalMiB)*asc::native::MiB,
        state.cfg.observedWarmMaxSources
    };
    if(usage.admittedSources>=limits.maxSources ||
       usage.admittedBytes>=limits.maxAggregateBytes){
        ++state.observedWarmDenied;return false;
    }
    asc::native::ObservedImgMemberWarm requested{};
    requested.observedOffset=job.key.offset;
    requested.observedBytes=job.key.length;
    requested.maxMemberBytes=uint32_t(std::min<uint64_t>(
        limits.maxSingleBytes,512ull*asc::native::MiB));
    requested.remainingQuotaBytes=limits.maxAggregateBytes-usage.admittedBytes;
    requested.hints=hints;
    uint64_t extentOffset=0,extentBytes=0;
    if(!session->RegisterVerifiedImgMember(job.duplicated,requested,
                                            extentOffset,extentBytes)){
        ++state.imgExtentRejected;return false;
    }
    // The helper has already validated and queued this bounded range.
    // Both sides enforce the quota; publication here records its cost.
    if(extentBytes>UINT32_MAX || !asc::observed_warm::Admit(
        asc::observed_warm::Kind::Img,extentBytes,limits,usage)){
        ++state.observedWarmErrors;return false;
    }
    Key selected=job.key;
    selected.offset=extentOffset;
    selected.length=DWORD(extentBytes);
    members.push_back(selected);
    usage.admittedSources++;
    usage.admittedBytes+=extentBytes;
    state.observedWarmSources=usage.admittedSources;
    state.observedWarmBytes=usage.admittedBytes;
    state.imgExtentBytes.fetch_add(extentBytes,std::memory_order_relaxed);
    ++state.imgExtentMatched;
    return true;
}
// Invoked only by the background worker, never by a ReadFile or game hook.
// The only trusted provenance is the original, selected live Mod Loader
// HANDLE. The x64 helper duplicates that handle and checks identity again.
void PromoteObservedSource(
    const std::shared_ptr<native_memory::Session>& session,
    const Work& job,std::unordered_set<Key,Hash>& admitted,
    std::vector<Key>& admittedImgMembers,
    asc::observed_warm::Usage& usage){
    auto& state=S();
    if(!state.cfg.warmObservedModLoaderSources ||
       job.observedKind==asc::modloader_cache::SourceKind::Unsupported)
        return;
    // V6: never register a full IMG in dependency-only mode. The x64 helper
    // verifies the observed IMG member and queues only intersecting pages.
    if(state.cfg.warmCatalogDependencies &&
       job.observedKind==asc::modloader_cache::SourceKind::Img){
        if(state.cfg.warmVerifiedImgMembers)
            PromoteSelectedImgMember(session,job,admittedImgMembers,usage);
        else ++state.dependencyWarmUnsafe;
        return;
    }
    if(state.cfg.warmCatalogDependencies && !SelectedLooseDependency(job))
        return;
    Key file=job.key;
    file.offset=0;file.length=0;
    if(admitted.contains(file))return;
    using asc::observed_warm::Kind;
    const Kind kind=job.observedKind==asc::modloader_cache::SourceKind::Img ?
        Kind::Img : Kind::Loose;
    const uint64_t size=(uint64_t(file.sizeHigh)<<32)|file.sizeLow;
    asc::observed_warm::Limits limits{
        uint64_t(state.cfg.observedWarmMaxSourceMiB)*asc::native::MiB,
        uint64_t(state.cfg.observedWarmMaxTotalMiB)*asc::native::MiB,
        state.cfg.observedWarmMaxSources};
    // Reserve the set entry first, to avoid admitting a source twice.
    // Failed registration rolls it back and does not consume the quota.
    if(!asc::observed_warm::Admit(kind,size,limits,usage)){
        ++state.observedWarmDenied;return;
    }
    auto [it,inserted]=admitted.insert(file);
    if(!inserted)return;
    if(!session->RegisterWarmSource(job.duplicated)){
        admitted.erase(it);
        ++state.observedWarmErrors;
        return;
    }
    usage.admittedSources++;
    usage.admittedBytes+=size;
    state.observedWarmSources=usage.admittedSources;
    state.observedWarmBytes=usage.admittedBytes;
}

void WorkThread(){
    auto& s=S();
    std::shared_ptr<native_memory::Session> session;
    uint64_t retryAfter=0,lastStats=0;
    bool archivesWarmed=false;
    unsigned failedStatsPolls=0;
    std::vector<HANDLE> retainedSources;
    std::vector<std::wstring> discovered;
    size_t nextArchive=0;
    uint64_t lastModLoaderProbe=0,lastCatalogQuery=0;
    std::unordered_set<Key,Hash> observedWarmFiles;
    std::vector<Key> admittedImgMembers;
    asc::observed_warm::Usage observedWarmUsage{};
    int32_t lastQueryX=INT32_MAX,lastQueryY=INT32_MAX;
    uint64_t lastCellQuery=0;
    uint64_t lastCatalogStatus=0;
    auto demandWaiting=[&]{
        std::lock_guard guard(s.mutex);
        return !s.pending.empty();
    };
    while(!s.stop){
        if(!session && GetTickCount64()>=retryAfter){
            // Startup, timeouts and named-pipe traffic NEVER occur on GTA's thread.
            std::ostringstream trace;
            try {
                session=native_memory::Session::Start(s.helperPath,
                              s.cfg.helperBudgetMiB,trace);
            }catch(...){
                ++s.errors;
                session.reset();
            }
            if(session){
                s.helperPid=session->Pid();s.connected=true;archivesWarmed=false;
                // A previous helper session may have owned these sources.
                for(auto h:retainedSources)CloseHandle(h);
                retainedSources.clear();
                discovered.clear();nextArchive=0;
                observedWarmFiles.clear();admittedImgMembers.clear();observedWarmUsage={};
                s.observedWarmSources=0;s.observedWarmBytes=0;
                s.observedWarmDenied=0;s.observedWarmErrors=0;
                s.dependencyWarmMatched=0;s.dependencyWarmUnmatched=0;
                s.dependencyWarmNoHints=0;s.dependencyWarmUnsafe=0;
                s.imgExtentMatched=0;s.imgExtentRejected=0;s.imgExtentBytes=0;
                s.discoverCandidates=0;s.discoverOpened=0;s.discoverRejected=0;
                s.warmArchives=0;s.warmTotalPages=0;s.warmResidentPages=0;
                s.remoteAllocated=0;s.remotePending=0;s.remoteCapacity=0;
                s.catalogPlacements=0;s.catalogResolved=0;s.catalogUnresolved=0;
                s.catalogCells=0;s.catalogGeneration=0;s.catalogReady=0;
                s.catalogAssetCandidates=0;s.catalogAmbiguousAssets=0;
                s.catalogDependencyEdges=0;s.catalogMatchedModels=0;
                s.catalogBinarySources=0;s.catalogBinaryPlacements=0;
                s.catalogImgArchives=0;s.catalogAmbiguous=0;
                s.catalogScanning=0;s.catalogLoadedFromCache=0;s.catalogCellPlacements=0;s.catalogCellModels=0;
                lastQueryX=INT32_MAX;lastQueryY=INT32_MAX;
                lastCellQuery=0;
                lastCatalogStatus=0;
                {
                    std::lock_guard guard(s.catalogHintMutex);
                    s.catalogHints={};++s.catalogHintSerial;
                }
                if(s.cfg.worldCatalogEnabled && !session->CatalogConfigure(true))
                    ++s.errors;
            }else{
                s.connected=false;retryAfter=GetTickCount64()+10000;
            }
        }
        if(session && !archivesWarmed){
            archivesWarmed=true;
            if(s.cfg.backgroundArchiveWarming){
                try {
                    discovered=DiscoverArchives(s.cfg);
                    s.discoverCandidates=discovered.size();
                }catch(...){++s.errors;}
            }
            // Without warming, the 64-bit cache fills only on observed
            // GTA IMG reads. The old unconditional 32-GiB preload consumed
            // physical RAM even with cdEligible=0 and zero usable hits.
        }
        // Module can load after this ASI; retry only on the cache worker.
        // No probing or IAT patching is performed on GTA's frame callbacks.
        const uint64_t modProbeNow=GetTickCount64();
        if(s.cfg.interceptModLoaderReads && !s.modLoaderIatActive.load() &&
           modProbeNow-lastModLoaderProbe>=2000){
            lastModLoaderProbe=modProbeNow;
            PatchModLoaderReadImport();
        }
        // Give demand jobs first opportunity to run through the single
        // named-pipe connection before registering more warm sources.
        if(session && nextArchive<discovered.size()){
            bool pendingDemand=false;
            {
                std::lock_guard guard(s.mutex);
                pendingDemand=!s.pending.empty();
            }
            if(!pendingDemand){
                try {WarmArchiveBatch(session,discovered,nextArchive,retainedSources);}
                catch(...){++s.errors;}
            }
        }
        const uint64_t now=GetTickCount64();
        if(session && !demandWaiting() && s.cfg.worldCatalogEnabled &&
           now-lastCatalogStatus>=(s.catalogReady.load()
               ?1000u:s.cfg.catalogQueryIntervalMs)){
            lastCatalogStatus=now;
            asc::native::CatalogSummary summary{};
            if(session->CatalogStatus(summary)){
                s.catalogPlacements=summary.placements;
                s.catalogResolved=summary.resolved;
                s.catalogUnresolved=summary.unresolved;
                s.catalogCells=summary.cells;
                s.catalogGeneration=summary.generation;
                s.catalogAssetCandidates=summary.archiveAssetCandidates;
                s.catalogAmbiguousAssets=summary.ambiguousAssetNames;
                s.catalogDependencyEdges=summary.modelDependencyEdges;
                s.catalogMatchedModels=summary.modelsWithArchiveCandidates;
                s.catalogBinarySources=summary.binarySources;
                s.catalogBinaryPlacements=summary.binaryPlacements;
                s.catalogImgArchives=summary.imgArchives;
                s.catalogAmbiguous=summary.ambiguousModels;
                s.catalogReady=summary.ready;
                s.catalogScanning=summary.scanning;
                s.catalogLoadedFromCache=summary.loadedFromCache;
            }
        }
        // Ready inventory is immutable. Poll its status at 1 Hz while allowing
        // fast cell changes independently, without an extra status round trip.
        if(session && !demandWaiting() && s.cfg.worldCatalogEnabled &&
           s.catalogReady.load() &&
           now-lastCatalogQuery>=s.cfg.catalogQueryIntervalMs){
            lastCatalogQuery=now;
            const auto x=s.worldCellX.load(std::memory_order_relaxed);
            const auto y=s.worldCellY.load(std::memory_order_relaxed);
            if(x!=INT32_MAX && y!=INT32_MAX &&
               (x!=lastQueryX || y!=lastQueryY ||
                now-lastCellQuery>=s.cfg.catalogRescanIntervalMs)){
                asc::native::CatalogCellReply hints{};
                if(session->CatalogQueryCell(x,y,hints)){
                    s.catalogCellPlacements=hints.placementCount;
                    s.catalogCellModels=hints.modelCount;
                    lastQueryX=x;lastQueryY=y;
                    lastCellQuery=GetTickCount64();
                    // Publish immutable copy; main thread must revalidate name
                    // and ID against actual live OLA/GTA before any request.
                    if(hints.modelCount<=64){
                        std::lock_guard guard(s.catalogHintMutex);
                        s.catalogHints=hints;
                        ++s.catalogHintSerial;
                    }
                }
            }
        }
        if(session && !demandWaiting() && now-lastStats>=5000){
            lastStats=now;
            asc::native::AssetSummary stats{};
            if(session->AssetStats(stats)){
                failedStatsPolls=0;
                s.remoteResident=stats.residentBytes;
                s.remoteReadHits=stats.hits;
                s.remoteDiskBytes=stats.diskBytes;
                asc::native::CacheCoverage coverage{};
                if(!demandWaiting() && session->CacheCoverage(coverage)){
                    s.warmArchives=coverage.archives;
                    s.warmTotalPages=coverage.expectedPages;
                    s.warmResidentPages=coverage.residentPages;
                    s.warmDeferred=coverage.deferred;
                    s.warmFailed=coverage.failures;
                    s.cacheWaits=coverage.waits;
                    s.remoteAllocated=coverage.allocatedBytes;
                    s.remotePending=coverage.pendingBytes;
                    s.remoteCapacity=coverage.capacityBytes;
                }
            }else if(++failedStatsPolls>=2){
                session.reset();
                s.connected=false;
                s.remoteResident=0;
                s.remoteAllocated=0;s.remotePending=0;s.remoteCapacity=0;
                s.warmArchives=0;s.warmTotalPages=0;s.warmResidentPages=0;
                s.warmDeferred=0;s.warmFailed=0;s.cacheWaits=0;
                for(auto h:retainedSources)CloseHandle(h);
                retainedSources.clear();
                archivesWarmed=false;
                discovered.clear();nextArchive=0;
                observedWarmFiles.clear();admittedImgMembers.clear();observedWarmUsage={};
                s.observedWarmSources=0;s.observedWarmBytes=0;
                s.observedWarmDenied=0;s.observedWarmErrors=0;
                s.dependencyWarmMatched=0;s.dependencyWarmUnmatched=0;
                s.dependencyWarmNoHints=0;s.dependencyWarmUnsafe=0;
                s.imgExtentMatched=0;s.imgExtentRejected=0;s.imgExtentBytes=0;
                retryAfter=now+10000;
            }
        }
        Work job{};
        {
            std::unique_lock guard(s.mutex);
            s.cv.wait_for(guard,std::chrono::milliseconds(150),[&]{
                return s.stop.load()||!s.pending.empty();
            });
            if(s.stop)break;
            if(s.pending.empty())continue;
            job=std::move(s.pending.front());s.pending.pop_front();
        }
        if(session){
            try {
                auto bytes=std::make_shared<std::vector<uint8_t>>(job.key.length);
                DWORD got=0;
                if(session->ReadAsset(job.duplicated,job.key.offset,
                                      bytes->data(),job.key.length,got) &&
                   got==job.key.length) {
                    Key current{};
                    // An archive may change while an async request is in
                    // flight; never publish its new data under the old key.
                    if(!SourceKey(job.duplicated,job.key.offset,
                                  job.key.length,current)||!(current==job.key)){
                        ++s.errors;
                    } else {
                        // Only proven selected Mod Loader source handles.
                        // An IPC failure cannot change GTA's already-native
                        // ReadFile completion; warming remains best effort.
                        bool promote=true;
                        {
                            std::lock_guard guard(s.mutex);
                            auto& reuse=ReuseFor(s,job.key);
                            reuse.fetched+=got;
                            // Whole-source/member warming is speculative. First
                            // establish useful ready hits for this exact source.
                            promote=!s.cfg.adaptiveSourceWarming ||
                                (reuse.served!=0 && reuse.served>=reuse.fetched/8);
                        }
                        if(promote){
                            try {
                                PromoteObservedSource(session,job,observedWarmFiles,
                                                       admittedImgMembers,observedWarmUsage);
                            }catch(...){++s.observedWarmErrors;}
                        }else ++s.lowReuseWarmSkipped;
                        s.fetchedBytes.fetch_add(got,std::memory_order_relaxed);
                        std::lock_guard guard(s.mutex);
                        const size_t cap=size_t(s.cfg.localCacheMiB)*1048576ull;
                        if(!s.stop&&!s.local.contains(job.key) &&
                           bytes->size()<=cap){
                            // Keep an index owner while a hook copies outside
                            // the lock. Its final release/free then stays on
                            // this worker during normal cache eviction.
                            size_t examined=0;
                            const size_t entries=s.lru.size();
                            while(s.localBytes+bytes->size()>cap &&
                                  !s.lru.empty() && examined++<entries){
                                const auto evicted=s.lru.back();
                                auto it=s.local.find(evicted);
                                if(it!=s.local.end() && it->second.bytes.use_count()>1){
                                    s.lru.splice(s.lru.begin(),s.lru,std::prev(s.lru.end()));
                                    continue;
                                }
                                if(it!=s.local.end()){
                                    s.localBytes-=it->second.bytes->size();
                                    s.local.erase(it);
                                }
                                s.lru.pop_back();
                            }
                            if(s.localBytes+bytes->size()>cap){
                                ++s.pinnedPublicationSkips;
                            }else{
                                s.lru.push_front(job.key);
                                try {
                                    const auto [it,inserted]=s.local.emplace(
                                        job.key,Entry{bytes,s.lru.begin()});
                                    if(inserted){
                                        s.localBytes+=bytes->size();
                                        ++s.fetched;
                                    }else s.lru.pop_front();
                                }catch(...){
                                    s.lru.pop_front();
                                    throw;
                                }
                            }
                        }
                    }
                }else ++s.errors;
            }catch(...){++s.errors;}
        }
        if(session && job.duplicated!=INVALID_HANDLE_VALUE)
            session->CloseAssetSource(job.duplicated);
        if(job.duplicated!=INVALID_HANDLE_VALUE)CloseHandle(job.duplicated);
        {std::lock_guard guard(s.mutex);s.inFlight.erase(job.key);}
    }
    session.reset();s.connected=false;
    for(auto h:retainedSources)CloseHandle(h);
}
} // namespace (internal helpers)

bool ReadyForNativeReclaim(unsigned minimumCachedMiB) noexcept {
    const auto& s=S();
    return s.active.load(std::memory_order_relaxed) &&
        s.connected.load(std::memory_order_acquire) &&
        s.modLoaderHookState.load(std::memory_order_relaxed)==2 &&
        s.remoteResident.load(std::memory_order_relaxed) >=
            uint64_t(minimumCachedMiB)*1048576ull;
}

bool TryCatalogHints(asc::native::CatalogCellReply& out,
                     uint64_t& generation) noexcept {
    auto& s=S();
    if(!s.cfg.worldCatalogEnabled||!s.connected.load())return false;
    std::unique_lock guard(s.catalogHintMutex,std::try_to_lock);
    if(!guard.owns_lock()||!s.catalogHintSerial)return false;
    out=s.catalogHints;
    generation=s.catalogHintSerial;
    return true;
}
void WorldPositionHint(float x,float y) noexcept {
    auto& s=S();
    if(!s.cfg.worldCatalogEnabled || !std::isfinite(x) || !std::isfinite(y))return;
    if(std::fabs(x)>1000000.0f || std::fabs(y)>1000000.0f)return;
    s.worldCellX.store(int32_t(std::floor(double(x)/256.0)),std::memory_order_relaxed);
    s.worldCellY.store(int32_t(std::floor(double(y)/256.0)),std::memory_order_relaxed);
}
void Start(const std::string& directory,Settings cfg,std::ostream& log){
    auto& s=S();
    if(!cfg.enabled)return;
    s.cfg=cfg;
    // Converting ASI path via Win32 API avoids lossy multibyte assumptions.
    const std::string path=directory+"\\DistantStreaming_AssetCache64.exe";
    int size=MultiByteToWideChar(CP_ACP,0,path.c_str(),-1,nullptr,0);
    if(size<=0)return;
    std::wstring wide(size,L'\0');
    MultiByteToWideChar(CP_ACP,0,path.c_str(),-1,wide.data(),size);
    wide.pop_back();s.helperPath=std::move(wide);
    if(GetFileAttributesW(s.helperPath.c_str())==INVALID_FILE_ATTRIBUTES){
        log<<"AssetCache64: helper EXE missing, cache not started\n";return;
    }
    s.stop=false;
    try {s.worker=std::thread(WorkThread);}
    catch(...){log<<"AssetCache64: worker start failed\n";return;}
    if(cfg.interceptCdStream && cfg.serveCdStreamHits && PatchCdStreamRead(log)) {
        // Primary: GTA's native sector dispatcher, not Windows ReadFile.
    } else if(cfg.interceptSynchronousRead && cfg.interceptProcessRead) {
        // Legacy opt-in fallback only; no process-wide hook by default.
        if(!PatchProcessReadFile(log))PatchGtaReadFile(log);
    }
    log<<"AssetCache64: enabled; 64-bit CPU cache target MiB="<<cfg.helperBudgetMiB
       <<", game-local queue cache MiB="<<cfg.localCacheMiB
       <<", background archive warming="<<cfg.backgroundArchiveWarming
       <<", observed Mod Loader source warming="<<cfg.warmObservedModLoaderSources
       <<", dependency-only warming="<<cfg.warmCatalogDependencies
        <<", verified IMG-member warming="<<cfg.warmVerifiedImgMembers
       <<", ready-hit substitution="<<cfg.serveCdStreamHits
       <<", IPC only on background worker\n";
    if(cfg.interceptCdStream && !cfg.serveCdStreamHits)
        log<<"AssetCache64: CdStream observer skipped: ready-hit serving disabled; "
             "selected Mod Loader caching remains independent\n";
}
void Report(uint32_t now,std::ostream& log){
    auto& s=S();
    if(!s.cfg.enabled||uint32_t(now-s.lastReport)<s.cfg.reportMs)return;
    size_t localBytes=0;
    {
        // Cache publication/eviction may free large buffers on the worker.
        // Diagnostics must never wait for that work on GTA's thread.
        std::unique_lock guard(s.mutex,std::try_to_lock);
        if(!guard.owns_lock())return;
        localBytes=s.localBytes;
    }
    s.lastReport=now;
    const uint64_t served=s.suppliedBytes.load(std::memory_order_relaxed);
    const uint64_t disk=s.remoteDiskBytes.load(std::memory_order_relaxed);
    const uint64_t queuedBytes=s.queuedBytes.load(std::memory_order_relaxed);
    const DWORD helperPid=s.helperPid.load(std::memory_order_relaxed);
    const uint64_t servedDelta=served>=s.reportServedBytes?served-s.reportServedBytes:0;
    // Remote counters restart with the helper; do not compare across sessions.
    const bool sameHelper=helperPid==s.reportHelperPid;
    const uint64_t diskDelta=sameHelper && disk>=s.reportDiskBytes?disk-s.reportDiskBytes:0;
    const uint64_t queuedDelta=queuedBytes>=s.reportQueuedBytes?queuedBytes-s.reportQueuedBytes:0;
    s.reportServedBytes=served;s.reportDiskBytes=disk;
    s.reportQueuedBytes=queuedBytes;s.reportHelperPid=helperPid;
    // Observe the host IMG handle table without substituting guessed handles.
    uint32_t cdHandleSlotsOpen=0;
    const auto* handleSlots=reinterpret_cast<const HANDLE*>(kCdHandlesAddress);
    if(Committed(handleSlots,32*sizeof(HANDLE))) {
        for(unsigned i=0;i<32;++i)
            if(handleSlots[i] && handleSlots[i]!=INVALID_HANDLE_VALUE)
                ++cdHandleSlotsOpen;
    }
    if(s.cfg.worldCatalogEnabled)
        log<<"WorldCatalog64: ready="<<s.catalogReady.load()
           <<", scanning="<<s.catalogScanning.load()
           <<", fromDiskCache="<<s.catalogLoadedFromCache.load()
           <<", generation="<<s.catalogGeneration.load()
           <<", placements="<<s.catalogPlacements.load()
           <<", resolved="<<s.catalogResolved.load()
           <<", unresolved="<<s.catalogUnresolved.load()
           <<", cells="<<s.catalogCells.load()
           <<", binarySources="<<s.catalogBinarySources.load()
           <<", binaryPlacements="<<s.catalogBinaryPlacements.load()
           <<", imgArchives="<<s.catalogImgArchives.load()
           <<", ambiguousIDs="<<s.catalogAmbiguous.load()
           <<", archiveAssetCandidates="<<s.catalogAssetCandidates.load()
           <<", ambiguousAssetNames="<<s.catalogAmbiguousAssets.load()
           <<", IDEtoTXDEdges="<<s.catalogDependencyEdges.load()
           <<", modelsWithArchiveCandidates="<<s.catalogMatchedModels.load()
           <<", currentCellPlacements="<<s.catalogCellPlacements.load()
           <<", uniqueModelHints="<<s.catalogCellModels.load()
           <<" (catalog indices only; native requests are separately opt-in)\n";
    log<<"AssetCache64: connected="<<s.connected.load()
       <<", pid="<<s.helperPid
       <<", queued="<<s.queued.load()<<", fetched="<<s.fetched.load()
       <<", lowReuseSkipped="<<s.lowReuseSkipped.load()
       <<", lowReuseWarmSkipped="<<s.lowReuseWarmSkipped.load()
       <<", readMode="<<(s.cdHookTarget?"CdStream-native":(s.processReaderHook?"process-ReadFile":
              (s.importSlot?(s.safeSourceReader?"IAT-read-through":"IAT-observe"):"no-hook")))
       <<", cdCalls="<<s.cdCalls.load()
       <<", cdEligible="<<s.cdEligible.load()
       <<", cdHits="<<s.cdHits.load()
       <<", cdMisses="<<s.cdMisses.load()
       <<", cdQueued="<<s.cdQueued.load()
       <<", cdSkipped="<<s.cdSkipped.load()
       <<", cdSkipDisconnected="<<s.cdSkipDisconnected.load()
       <<", cdSkipAddresses="<<s.cdSkipAddresses.load()
       <<", cdLayoutRejected="<<s.cdLayoutRejected.load()
       <<", cdSkipChannels="<<s.cdSkipChannels.load()
       <<", cdSkipDecode="<<s.cdSkipDecode.load()
       <<", cdSkipHandle="<<s.cdSkipHandle.load()
       <<", cdSkipHandleNull="<<s.cdSkipHandleNull.load()
       <<", cdSkipHandleInvalid="<<s.cdSkipHandleInvalid.load()
       <<", cdHandleSlotsOpen="<<cdHandleSlotsOpen
       <<", cdLastMissingFileId="<<s.cdLastMissingFileId.load()
       <<", cdNullHandleChannelFilePresent="<<s.cdNullHandleChannelFilePresent.load()
       <<", cdNullHandleNativeAccepted="<<s.cdNullHandleNativeAccepted.load()
       <<", cdSkipSource="<<s.cdSkipSource.load()
       <<", readyHitSubstitution="<<s.cfg.serveCdStreamHits
       <<", warmingEnabled="<<s.cfg.backgroundArchiveWarming
       <<", observedWarmEnabled="<<s.cfg.warmObservedModLoaderSources
       <<", observedWarmSources="<<s.observedWarmSources.load()
       <<", observedWarmMiB="<<(s.observedWarmBytes.load()/1048576ull)
       <<", observedWarmDenied="<<s.observedWarmDenied.load()
       <<", observedWarmErrors="<<s.observedWarmErrors.load()
       <<", dependencyWarmMatched="<<s.dependencyWarmMatched.load()
       <<", dependencyWarmUnmatched="<<s.dependencyWarmUnmatched.load()
       <<", dependencyWarmNoHints="<<s.dependencyWarmNoHints.load()
       <<", dependencyWarmUnsafe="<<s.dependencyWarmUnsafe.load()
        <<", imgExtentMatched="<<s.imgExtentMatched.load()
        <<", imgExtentRejected="<<s.imgExtentRejected.load()
        <<", imgExtentMiB="<<(s.imgExtentBytes.load()/1048576ull)
       <<", modLoaderIatState="<<s.modLoaderHookState.load()
       <<", modLoaderCalls="<<s.modLoaderCalls.load()
       <<", modLoaderImgReads="<<s.modLoaderImgReads.load()
       <<", modLoaderLooseReads="<<s.modLoaderLooseReads.load()
       <<", modLoaderImgHits="<<s.modLoaderImgHits.load()
       <<", modLoaderLooseHits="<<s.modLoaderLooseHits.load()
       <<", modLoaderEligible="<<s.modLoaderEligible.load()
       <<", modLoaderHits="<<s.modLoaderHits.load()
       <<", modLoaderMisses="<<s.modLoaderMisses.load()
       <<", modLoaderQueued="<<s.modLoaderQueued.load()
       <<", modLoaderPending="<<s.modLoaderNativePending.load()
       <<", modLoaderPassThrough="<<s.modLoaderPassThrough.load()
       <<", modLoaderServe="<<s.cfg.serveModLoaderReadyHits
       <<", interceptedCalls="<<s.interceptedCalls.load()
       <<", syncCalls="<<s.synchronousCalls.load()
       <<", overlappedCalls="<<s.overlappedCalls.load()
       <<", nativePending="<<s.nativePending.load()
       <<", validIMG="<<s.inspectedImg.load()
       <<", rejectedSource="<<s.rejectedSource.load()
       <<", oversized="<<s.oversizedCalls.load()
       <<", overlappedQueued="<<s.overlappedQueued.load()
       <<", eligibleCalls="<<s.eligibleCalls.load()
       <<", observedReads="<<s.observedReads.load()
       <<", observedMiB="<<s.observedBytes.load()/1048576ull
       <<", syncReadHits="<<s.hits.load()<<", misses="<<s.misses.load()
       <<", drops="<<s.dropped.load()<<", bytesServed="<<s.suppliedBytes.load()
       <<", errors="<<s.errors.load()
       <<", gameCacheMiB="<<localBytes/1048576ull
       <<", helperCachedMiB="<<s.remoteResident.load()/1048576ull
       <<", helperCacheHits="<<s.remoteReadHits.load()
       <<", helperDiskMiB="<<s.remoteDiskBytes.load()/1048576ull
       <<", helperLivePageMiB="<<s.remoteAllocated.load()/1048576ull
       <<", helperPendingReservationMiB="<<s.remotePending.load()/1048576ull
       <<", helperPageCapacityMiB="<<s.remoteCapacity.load()/1048576ull
       <<", lookupHits/attempts="<<s.lookupHits.load()<<"/"<<s.lookupCost.calls.load()
       <<", lookupLockMisses="<<s.lookupContention.load()
       <<", pinnedPublicationSkips="<<s.pinnedPublicationSkips.load()
       <<", copiedBytes="<<s.copiedBytes.load()
       <<", queuedSourceBytes="<<s.queuedBytes.load()
       <<", fetchedSourceBytes="<<s.fetchedBytes.load()
       <<", reportServed/queuedBytes="<<servedDelta<<"/"<<queuedDelta
       <<", reportHelperDiskBytes="<<diskDelta
       <<", reportHelperDiskComparable="<<sameHelper
       <<", discoveredIMG="<<s.discoverCandidates.load()
       <<", registeredIMG="<<s.discoverOpened.load()
       <<", rejectedIMG="<<s.discoverRejected.load()
       <<", warmArchives="<<s.warmArchives.load()
       <<", warmPages="<<s.warmResidentPages.load()<<"/"<<s.warmTotalPages.load()
       <<", candidateCoveragePct="
       <<asc::world_cache::CoveragePercent(
           s.warmResidentPages.load(),s.warmTotalPages.load())
       <<", registeredSourcesFullyResident="
       <<asc::world_cache::RegisteredSourcesComplete(
           s.discoverCandidates.load(),s.discoverOpened.load(),
           s.warmArchives.load(),s.warmTotalPages.load(),
           s.warmResidentPages.load())
       <<", cacheWaits="<<s.cacheWaits.load()
       <<", cacheDeferred="<<s.warmDeferred.load()
       <<", cacheFailures="<<s.warmFailed.load()<<"\n";
    LARGE_INTEGER frequency{};
    if(QueryPerformanceFrequency(&frequency) && frequency.QuadPart>0){
        log<<"AssetCache64 sampled elapsed ns avg/peak/samples/calls (1 in 64; hook/native inclusive): ";
        const auto cost=[&](const char* name,const CostMetric& metric){
            const auto samples=metric.samples.load(std::memory_order_relaxed);
            const double nsPerTick=1.0e9/double(frequency.QuadPart);
            const auto avg=samples?uint64_t(double(metric.ticks.load(std::memory_order_relaxed))/double(samples)*nsPerTick):0;
            const auto peak=uint64_t(double(metric.peakTicks.load(std::memory_order_relaxed))*nsPerTick);
            log<<name<<"="<<avg<<"/"<<peak<<"/"<<samples<<"/"<<metric.calls.load()<<" ";
        };
        cost("metadata",s.metadataCost);cost("lookup",s.lookupCost);
        cost("copy",s.copyCost);cost("queue",s.queueCost);
        cost("cdHook",s.cdHookCost);cost("modLoaderHook",s.modLoaderHookCost);
        cost("readHook",s.readHookCost);cost("nativeRead",s.nativeReadCost);
        log<<"\n";
    }
}
void Stop(){
    auto& s=S();
    // Stop the worker before restoring module-local IAT (prevents re-patch).
    s.stop=true;s.cv.notify_all();
    if(s.worker.joinable())s.worker.join();
    s.modLoaderIatActive.store(false,std::memory_order_release);
    if(s.modLoaderImportSlot && s.modLoaderOriginal &&
       GetModuleHandleW(L"std.stream.dll")==s.modLoaderModule)
        Replace(s.modLoaderImportSlot,reinterpret_cast<void*>(&ModLoaderReadDetour),
                reinterpret_cast<void*>(s.modLoaderOriginal));
    s.modLoaderImportSlot=nullptr;
    s.modLoaderModule=nullptr;
    s.active=false;
    s.cdActive.store(false,std::memory_order_release);
    if(s.cdHookTarget){
        MH_DisableHook(s.cdHookTarget);
        MH_RemoveHook(s.cdHookTarget);
        s.cdHookTarget=nullptr;
        // Keep the original trampoline pointer until DLL unload; any
        // already-running detour must never see it nulled mid-call.
    }
    if(s.nativeHookTarget){
        MH_DisableHook(s.nativeHookTarget);
        MH_RemoveHook(s.nativeHookTarget);
        s.nativeHookTarget=nullptr;
        s.processReaderHook=false;
    }
    if(s.importSlot)
        Replace(s.importSlot,reinterpret_cast<void*>(&ReadDetour),
                reinterpret_cast<void*>(s.original));
    s.importSlot=nullptr;
    std::lock_guard guard(s.mutex);
    for(auto& job:s.pending)if(job.duplicated!=INVALID_HANDLE_VALUE)CloseHandle(job.duplicated);
    s.pending.clear();s.inFlight.clear();s.local.clear();s.lru.clear();s.localBytes=0;
    s.sourceReuse={};
}
} // namespace asc::external_cache
