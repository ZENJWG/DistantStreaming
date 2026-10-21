// Distant Streaming: GPU-direct D3D9 resource accounting / DXGI WDDM budget.
// No helper process, pipe, resource proxy, cross-process copies, or artificial 4 GiB GPU cap.
// Original D3D9 Create* methods always make the allocations. The driver owns physical residency.
// All observations and tags are best-effort; NEVER change HRESULT/creation contract.
#include "GpuResidencyWin.h"
#include "asc/FramePacingProbe.h"
#include "asc/RenderCostProbe.h"
#include "asc/GpuHotResidencyPolicy.h"
#include "asc/RendererCoexistence.h"
#include <windows.h>
#include <dxgi1_4.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <ostream>
#include <thread>
#include <system_error>
#include <vector>
#include <chrono>
#include <deque>

namespace gpu_residency {
namespace {
constexpr uint64_t MiB=1024ull*1024ull;
constexpr size_t DeviceMethods=119, MaxVtableSlots=4096;
// Unique private-data key: not used by GTA or other mods.
const GUID kResourceTag={0x8b179f14,0x3abc,0x457d,{0xb2,0x81,0xe1,0x23,0x7c,0x92,0xf9,0x46}};
enum class Kind : unsigned { Texture, Buffer, Surface, Count };
struct Counters {
    std::atomic<uint64_t> live{0}, bytes{0}, created{0}, destroyed{0};
};
struct State {
    Settings settings{};
    IDirect3DDevice9* device=nullptr;
    void** original=nullptr;
    std::vector<void*> clone;
    Counters counts[static_cast<unsigned>(Kind::Count)];
    std::atomic<uint64_t> localUsage{0}, localBudget{0}, nonLocalUsage{0};
    std::atomic<bool> budgetKnown{false}, pressure{false};
    std::atomic<bool> streamingPressure{false};
    std::atomic<bool> stopping{false};
    std::thread sampler;
    std::mutex wakeMutex;
    std::condition_variable wake;
    DWORD vendor=0, deviceId=0;
    uint32_t lastReport=0;
    bool warnedHookOrder=false;
    std::atomic<uint64_t> attachFailed{0};
    struct PreloadJob {
        IDirect3DResource9* resource=nullptr; // A temporary AddRef owned by queue
        uint64_t estimatedBytes=0;
        uint32_t createdMs=0;
    };
    std::mutex queueMutex;
    std::deque<PreloadJob> queue;
    uint64_t queuedBytes=0;
    uint32_t nextPreloadMs=0;
    std::atomic<uint64_t> expiredEarly{0};
    std::atomic<uint64_t> queuedTotal{0}, preloaded{0}, preloadSkipped{0};
    std::atomic<uint64_t> preloadSlow{0}, preloadMaxUs{0};
    std::atomic<uint64_t> queueDrops{0};
    std::atomic<uint64_t> creationFailures{0}, videoMemoryFailures{0};
    std::atomic<uint64_t> oomCooloffUntil{0};
    std::atomic<uint64_t> managedBytes{0}, defaultBytes{0}, otherBytes{0};
    std::atomic<uint64_t> bindCalls{0}, bindSampled{0}, bindPromoted{0};
    std::atomic<uint64_t> bindMissingTags{0};
    std::atomic<uint64_t> effectiveGpuLimitBytes{0};
    // D3D9 methods can be invoked from threads other than the scheduler.
    // The lock never encloses driver calls or logging.
    std::mutex pacingMutex;
    asc::frame_pacing::Probe pacing;
    asc::render_cost::StageProbe renderStages;
    asc::render_cost::DrawProbe drawCalls;

    void** Table() { return clone.data()+1; }
};
State& S() { static auto* state=new State; return *state; }
uint64_t ClockUs() noexcept {
    LARGE_INTEGER ticks{};
    // QPF is stable within the process; cache it to avoid repeated system calls.
    static const int64_t hz=[]{
        LARGE_INTEGER f{};
        return QueryPerformanceFrequency(&f) && f.QuadPart>0 ?
            f.QuadPart : int64_t(1000000);
    }();
    QueryPerformanceCounter(&ticks);
    const uint64_t raw=ticks.QuadPart>0?uint64_t(ticks.QuadPart):0;
    const uint64_t frequency=uint64_t(hz);
    return (raw/frequency)*1000000ull+
           ((raw%frequency)*1000000ull)/frequency;
}
uint64_t ClockTicks() noexcept {
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return t.QuadPart>0?uint64_t(t.QuadPart):0;
}
uint64_t ClockFrequency() noexcept {
    static const uint64_t frequency=[] {
        LARGE_INTEGER value{};
        return QueryPerformanceFrequency(&value)&&value.QuadPart>0
            ?uint64_t(value.QuadPart):uint64_t(1000000);
    }();
    return frequency;
}
bool Readable(const void* p,size_t len) {
    MEMORY_BASIC_INFORMATION mbi{};
    if(!VirtualQuery(p,&mbi,sizeof(mbi)) || mbi.State!=MEM_COMMIT ||
       (mbi.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
    const DWORD flags=mbi.Protect&0xff;
    if(flags!=PAGE_READONLY && flags!=PAGE_READWRITE &&
       flags!=PAGE_WRITECOPY && flags!=PAGE_EXECUTE_READ &&
       flags!=PAGE_EXECUTE_READWRITE && flags!=PAGE_EXECUTE_WRITECOPY)return false;
    const uintptr_t a=reinterpret_cast<uintptr_t>(p);
    const uintptr_t start=reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    if(a<start)return false;
    return len<=mbi.RegionSize-(a-start);
}
bool ChangeTable(IDirect3DDevice9* device,void** value) {
    if(!Readable(device,sizeof(void*)))return false;
    DWORD before=0;
    if(!VirtualProtect(device,sizeof(void*),PAGE_READWRITE,&before))return false;
    *reinterpret_cast<void***>(device)=value;
    DWORD ignore=0;VirtualProtect(device,sizeof(void*),before,&ignore);
    return true;
}
uint64_t SatAdd(uint64_t a,uint64_t b) {
    return a>UINT64_MAX-b?UINT64_MAX:a+b;
}
uint64_t PixelBytes(uint32_t w,uint32_t h,D3DFORMAT fmt) {
    w=std::max(1u,w);h=std::max(1u,h);
    uint64_t blocks=(uint64_t((w+3)/4)*((h+3)/4));
    switch(fmt) {
    case D3DFMT_DXT1: return blocks*8;
    case D3DFMT_DXT2: case D3DFMT_DXT3:
    case D3DFMT_DXT4: case D3DFMT_DXT5: return blocks*16;
    default: break;
    }
    uint32_t bpp=4;
    switch(fmt) {
    case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4: case D3DFMT_V8U8: case D3DFMT_R16F: case D3DFMT_D16:
        bpp=2;break;
    case D3DFMT_A8: case D3DFMT_L8: bpp=1;break;
    case D3DFMT_A16B16G16R16F: case D3DFMT_A16B16G16R16:
        bpp=8;break;
    case D3DFMT_A32B32G32R32F: bpp=16;break;
    default:break;
    }
    return uint64_t(w)*h*bpp;
}
uint64_t MipBytes(uint32_t w,uint32_t h,uint32_t depth,
                  UINT levels,D3DFORMAT format,unsigned faces=1) {
    uint64_t total=0;
    // 0 means full chain; clamp to 32 to avoid overflow/malformed inputs.
    const UINT count=levels?std::min(levels,32u):32u;
    for(UINT i=0;i<count;++i) {
        const uint64_t n=PixelBytes(w,h,format)*uint64_t(std::max(1u,depth))*faces;
        total=SatAdd(total,n);
        if(w==1&&h==1&&depth==1)break;
        w=std::max(1u,w/2);h=std::max(1u,h/2);depth=std::max(1u,depth/2);
    }
    return total;
}
// Lifetime sentinel relies on D3DSPD_IUNKNOWN. D3D9 owns an AddRef and
// releases it when the resource is destroyed. No per-resource vtable patches.
class Lifetime final : public IUnknown {
public:
    Lifetime(Kind kind,uint64_t estimate,D3DPOOL pool):kind_(kind),bytes_(estimate),pool_(pool){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(iid==IID_IUnknown) {*out=static_cast<IUnknown*>(this);AddRef();return S_OK;}
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return refs_.fetch_add(1,std::memory_order_relaxed)+1;}
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n=refs_.fetch_sub(1,std::memory_order_acq_rel)-1;
        if(!n)delete this;
        return n;
    }
    bool PromoteIfManaged(IDirect3DResource9* resource,DWORD priority) noexcept {
        if(pool_!=D3DPOOL_MANAGED || !resource || !priority)return false;
        // Never lower priority another mod may already have assigned.
        if(resource->GetPriority()>=priority)return false;
        bool expected=false;
        if(!promoted_.compare_exchange_strong(expected,true,std::memory_order_acq_rel))
            return false;
        resource->SetPriority(priority);
        return true;
    }
    void Count() {
        if(counted_.exchange(true,std::memory_order_acq_rel))return;
        auto& c=S().counts[static_cast<unsigned>(kind_)];
        c.live.fetch_add(1,std::memory_order_relaxed);
        c.created.fetch_add(1,std::memory_order_relaxed);
        c.bytes.fetch_add(bytes_,std::memory_order_relaxed);
        PoolCounter().fetch_add(bytes_,std::memory_order_relaxed);
    }
private:
    ~Lifetime() {
        if(counted_.load(std::memory_order_acquire)){
            auto& c=S().counts[static_cast<unsigned>(kind_)];
            c.live.fetch_sub(1,std::memory_order_relaxed);
            c.destroyed.fetch_add(1,std::memory_order_relaxed);
            c.bytes.fetch_sub(bytes_,std::memory_order_relaxed);
            PoolCounter().fetch_sub(bytes_,std::memory_order_relaxed);
        }
    }
    std::atomic<uint64_t>& PoolCounter() const {
        auto& s=S();
        if(pool_==D3DPOOL_MANAGED)return s.managedBytes;
        if(pool_==D3DPOOL_DEFAULT)return s.defaultBytes;
        return s.otherBytes;
    }
    std::atomic<ULONG> refs_{1};
    std::atomic<bool> counted_{false};
    std::atomic<bool> promoted_{false};
    Kind kind_;
    uint64_t bytes_;
    D3DPOOL pool_;
};
void Register(IDirect3DResource9* resource,Kind kind,uint64_t estimatedBytes,D3DPOOL pool){
    if(!resource)return;
    auto* tag=new(std::nothrow) Lifetime(kind,estimatedBytes,pool);
    if(!tag)return;
    // D3DSPD_IUNKNOWN consumes pData as an IUnknown pointer, not pointer-to-pointer.
    const HRESULT hr=resource->SetPrivateData(kResourceTag,
        static_cast<IUnknown*>(tag),sizeof(IUnknown*),D3DSPD_IUNKNOWN);
    if(SUCCEEDED(hr))tag->Count();
    else S().attachFailed.fetch_add(1,std::memory_order_relaxed);
    tag->Release(); // Drop our construction reference; D3D9 owns remaining ref.
}
// Register managed resources for a small, bounded "will-use-soon" queue.
// COM AddRef is temporary. These jobs do not pin abandoned assets indefinitely,
// and neither the game nor the driver is required to allocate up to TargetMiB.
void EnqueueManaged(IDirect3DResource9* resource,D3DPOOL pool,DWORD usage,uint64_t bytes) {
    auto& s=S();
    if(s.stopping.load(std::memory_order_acquire) ||
       GetTickCount64()<s.oomCooloffUntil.load(std::memory_order_relaxed) ||
       s.pressure.load(std::memory_order_acquire) ||
       s.streamingPressure.load(std::memory_order_relaxed) ||
       !s.settings.manageResources || !resource || pool!=D3DPOOL_MANAGED ||
       (usage&(D3DUSAGE_DYNAMIC|D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL)))
        return;
    // An existing D3D9 managed asset can receive a residency priority hint,
    // without altering Create*, COM identity, or the memory pool.
    if(s.settings.managedPriority)
        resource->SetPriority(s.settings.managedPriority);
    if(!s.settings.preloadManaged || !bytes ||
       bytes>uint64_t(s.settings.preloadMaxResourceMiB)*MiB)
        return;
    // Creation may run in a loading callback: never wait for the render thread.
    std::unique_lock<std::mutex> lock(s.queueMutex,std::try_to_lock);
    if(!lock.owns_lock()) {s.queueDrops.fetch_add(1);return;}
    if(s.queue.size()>=32 ||
       bytes>uint64_t(s.settings.preloadQueueMiB)*MiB ||
       s.queuedBytes>uint64_t(s.settings.preloadQueueMiB)*MiB-bytes) {
        s.queueDrops.fetch_add(1);return;
    }
    resource->AddRef();
    try {
        s.queue.push_back({resource,bytes,GetTickCount()});
        s.queuedBytes+=bytes;
        s.queuedTotal.fetch_add(1,std::memory_order_relaxed);
    } catch(...) {
        resource->Release();
        s.queueDrops.fetch_add(1,std::memory_order_relaxed);
    }
}
// Called on GTA's D3D/render thread; PreLoad is NOT thread-safe for an
// arbitrary device and may block. At most a small number of small objects
// are submitted; slow calls trigger a cool-off rather than a burst.
void DrainPreload(uint32_t now) {
    auto& s=S();
    if(!s.settings.manageResources || !s.settings.preloadManaged ||
       s.stopping.load(std::memory_order_acquire))return;
    const bool pressure=s.pressure.load(std::memory_order_acquire) ||
        s.streamingPressure.load(std::memory_order_relaxed);
    // Clean a bounded number of stale COM references even under pressure.
    // Otherwise thousands of aged jobs crowd out useful recent resources.
    for(unsigned i=0;i<4;++i) {
        State::PreloadJob old{};
        {
            std::unique_lock<std::mutex> lock(s.queueMutex,std::try_to_lock);
            if(!lock.owns_lock()||s.queue.empty()||
               (!pressure&&uint32_t(now-s.queue.front().createdMs)<=s.settings.preloadMaxAgeMs))
                break;
            old=s.queue.front();
            s.queue.pop_front();
            s.queuedBytes-=old.estimatedBytes;
        }
        old.resource->Release();
        ++s.preloadSkipped;
        ++s.expiredEarly;
    }
    if(pressure ||
       GetTickCount64()<s.oomCooloffUntil.load(std::memory_order_relaxed) ||
       int32_t(now-s.nextPreloadMs)<0)return;
    const auto start=std::chrono::steady_clock::now();
    unsigned completed=0;
    while(completed<s.settings.preloadJobsPerFrame) {
        State::PreloadJob job{};
        {
            std::unique_lock<std::mutex> lock(s.queueMutex,std::try_to_lock);
            if(!lock.owns_lock()||s.queue.empty())break;
            if(uint32_t(now-s.queue.front().createdMs)<s.settings.preloadMinAgeMs)
                break;
            job=s.queue.front();
            s.queue.pop_front();
            s.queuedBytes-=job.estimatedBytes;
        }
        const bool stale=uint32_t(now-job.createdMs)>s.settings.preloadMaxAgeMs;
        if(!stale) {
            const auto before=std::chrono::steady_clock::now();
            // D3D9 PreLoad is synchronous. The time budget can bound call
            // COUNT, not preempt a driver call already in progress.
            job.resource->PreLoad();
            const auto spent=uint64_t(std::chrono::duration_cast<
                std::chrono::microseconds>(
                    std::chrono::steady_clock::now()-before).count());
            s.preloaded.fetch_add(1,std::memory_order_relaxed);
            auto max=s.preloadMaxUs.load(std::memory_order_relaxed);
            while(max<spent && !s.preloadMaxUs.compare_exchange_weak(max,spent)) {}
            if(spent>s.settings.preloadBudgetUs) {
                s.preloadSlow.fetch_add(1,std::memory_order_relaxed);
                // Back off for a full 1.5 seconds after a slow driver call.
                s.nextPreloadMs=now+1500;
            }
        } else s.preloadSkipped.fetch_add(1,std::memory_order_relaxed);
        job.resource->Release();
        ++completed;
        if(int32_t(now-s.nextPreloadMs)>=0)
            s.nextPreloadMs=now+s.settings.preloadIntervalMs;
        // Respect spacing even when an INI allows several jobs per frame.
        if(int32_t(now-s.nextPreloadMs)<0)break;
        const auto total=std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now()-start).count();
        if(total>=s.settings.preloadBudgetUs)break;
    }
}
void ClearPreload() {
    auto& s=S();
    std::deque<State::PreloadJob> old;
    {
        std::lock_guard lock(s.queueMutex);
        old.swap(s.queue);
        s.queuedBytes=0;
    }
    for(auto& job:old)job.resource->Release();
}
template<typename T> T Original(size_t slot) {
    return reinterpret_cast<T>(S().original[slot]);
}
// A D3D9 OOM may be caused by GTA's x86 VA, not physical VRAM exhaustion.
void RecordGpuFailure(HRESULT hr) {
    auto& s=S();
    s.creationFailures.fetch_add(1,std::memory_order_relaxed);
    if(hr==D3DERR_OUTOFVIDEOMEMORY || hr==E_OUTOFMEMORY) {
        s.videoMemoryFailures.fetch_add(1,std::memory_order_relaxed);
        s.oomCooloffUntil.store(GetTickCount64()+10000,std::memory_order_release);
    }
}
HRESULT STDMETHODCALLTYPE MakeTexture(IDirect3DDevice9* d,UINT w,UINT h,UINT levels,
        DWORD usage,D3DFORMAT fmt,D3DPOOL pool,IDirect3DTexture9** out,HANDLE* shared) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,UINT,UINT,DWORD,D3DFORMAT,
                                         D3DPOOL,IDirect3DTexture9**,HANDLE*);
    const HRESULT hr=Original<Fn>(23)(d,w,h,levels,usage,fmt,pool,out,shared);
    if(FAILED(hr)) {
        RecordGpuFailure(hr);
    }
    if(SUCCEEDED(hr)&&out&&*out){ const auto bytes=MipBytes(w,h,1,levels,fmt);
        Register(*out,Kind::Texture,bytes,pool);
        EnqueueManaged(*out,pool,usage,bytes); }
    return hr;
}
HRESULT STDMETHODCALLTYPE MakeVolume(IDirect3DDevice9* d,UINT w,UINT h,UINT z,UINT levels,
        DWORD usage,D3DFORMAT fmt,D3DPOOL pool,IDirect3DVolumeTexture9** out,HANDLE* shared) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,UINT,UINT,UINT,DWORD,
                                         D3DFORMAT,D3DPOOL,IDirect3DVolumeTexture9**,HANDLE*);
    const HRESULT hr=Original<Fn>(24)(d,w,h,z,levels,usage,fmt,pool,out,shared);
    if(FAILED(hr)) {
        RecordGpuFailure(hr);
    }
    if(SUCCEEDED(hr)&&out&&*out){ const auto bytes=MipBytes(w,h,z,levels,fmt);
        Register(*out,Kind::Texture,bytes,pool);
        EnqueueManaged(*out,pool,usage,bytes); }
    return hr;
}
HRESULT STDMETHODCALLTYPE MakeCube(IDirect3DDevice9* d,UINT size,UINT levels,DWORD usage,
    D3DFORMAT fmt,D3DPOOL pool,IDirect3DCubeTexture9** out,HANDLE* shared) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,UINT,DWORD,D3DFORMAT,
                                         D3DPOOL,IDirect3DCubeTexture9**,HANDLE*);
    const HRESULT hr=Original<Fn>(25)(d,size,levels,usage,fmt,pool,out,shared);
    if(FAILED(hr)) {
        RecordGpuFailure(hr);
    }
    if(SUCCEEDED(hr)&&out&&*out){ const auto bytes=MipBytes(size,size,1,levels,fmt,6);
        Register(*out,Kind::Texture,bytes,pool);
        EnqueueManaged(*out,pool,usage,bytes); }
    return hr;
}
HRESULT STDMETHODCALLTYPE MakeVB(IDirect3DDevice9* d,UINT bytes,DWORD usage,DWORD fvf,
    D3DPOOL pool,IDirect3DVertexBuffer9** out,HANDLE* shared) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,DWORD,DWORD,D3DPOOL,
                                         IDirect3DVertexBuffer9**,HANDLE*);
    const HRESULT hr=Original<Fn>(26)(d,bytes,usage,fvf,pool,out,shared);
    if(FAILED(hr)) {
        RecordGpuFailure(hr);
    }
    if(SUCCEEDED(hr)&&out&&*out){ Register(*out,Kind::Buffer,bytes,pool);
        EnqueueManaged(*out,pool,usage,bytes); }
    return hr;
}
HRESULT STDMETHODCALLTYPE MakeIB(IDirect3DDevice9* d,UINT bytes,DWORD usage,D3DFORMAT fmt,
    D3DPOOL pool,IDirect3DIndexBuffer9** out,HANDLE* shared) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,DWORD,D3DFORMAT,
                                         D3DPOOL,IDirect3DIndexBuffer9**,HANDLE*);
    const HRESULT hr=Original<Fn>(27)(d,bytes,usage,fmt,pool,out,shared);
    if(FAILED(hr)) {
        RecordGpuFailure(hr);
    }
    if(SUCCEEDED(hr)&&out&&*out){ Register(*out,Kind::Buffer,bytes,pool);
        EnqueueManaged(*out,pool,usage,bytes); }
    return hr;
}
HRESULT STDMETHODCALLTYPE MakeRT(IDirect3DDevice9* d,UINT w,UINT h,D3DFORMAT fmt,
    D3DMULTISAMPLE_TYPE multi,DWORD quality,BOOL lockable,IDirect3DSurface9** out,HANDLE* sh) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,UINT,D3DFORMAT,
               D3DMULTISAMPLE_TYPE,DWORD,BOOL,IDirect3DSurface9**,HANDLE*);
    const HRESULT hr=Original<Fn>(28)(d,w,h,fmt,multi,quality,lockable,out,sh);
    if(FAILED(hr)) {
        RecordGpuFailure(hr);
    }
    if(SUCCEEDED(hr)&&out&&*out)Register(*out,Kind::Surface,
        PixelBytes(w,h,fmt)*std::max(1u,static_cast<unsigned>(multi)),D3DPOOL_DEFAULT);
    return hr;
}
HRESULT STDMETHODCALLTYPE MakeDS(IDirect3DDevice9* d,UINT w,UINT h,D3DFORMAT fmt,
    D3DMULTISAMPLE_TYPE multi,DWORD quality,BOOL discard,IDirect3DSurface9** out,HANDLE* sh) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,UINT,D3DFORMAT,
               D3DMULTISAMPLE_TYPE,DWORD,BOOL,IDirect3DSurface9**,HANDLE*);
    const HRESULT hr=Original<Fn>(29)(d,w,h,fmt,multi,quality,discard,out,sh);
    if(FAILED(hr)) {
        RecordGpuFailure(hr);
    }
    if(SUCCEEDED(hr)&&out&&*out)Register(*out,Kind::Surface,
        PixelBytes(w,h,fmt)*std::max(1u,static_cast<unsigned>(multi)),D3DPOOL_DEFAULT);
    return hr;
}
HRESULT STDMETHODCALLTYPE MakeOffscreen(IDirect3DDevice9* d,UINT w,UINT h,D3DFORMAT fmt,
    D3DPOOL pool,IDirect3DSurface9** out,HANDLE* sh) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,UINT,D3DFORMAT,
                                         D3DPOOL,IDirect3DSurface9**,HANDLE*);
    const HRESULT hr=Original<Fn>(36)(d,w,h,fmt,pool,out,sh);
    if(FAILED(hr)) {
        RecordGpuFailure(hr);
    }
    if(SUCCEEDED(hr)&&out&&*out)Register(*out,Kind::Surface,PixelBytes(w,h,fmt),pool);
    return hr;
}
// Observe actual texture and buffer use rather than resource creation alone.
// Sampling limits private-data COM queries to ~1/128 bind calls.
// D3DPOOL_MANAGED still retains its D3D9 system-memory backing copy.
void PromoteBound(IDirect3DResource9* resource) noexcept {
    auto& s=S();
    if(!s.settings.manageResources || !s.settings.prioritizeUsedManaged ||
       !resource || !s.settings.usedManagedPriority)return;
    const uint64_t index=s.bindCalls.fetch_add(1,std::memory_order_relaxed)+1;
    if(!asc::gpu_hot_residency::ShouldSampleBind(
         index,s.settings.bindPrioritySampleRate))return;
    s.bindSampled.fetch_add(1,std::memory_order_relaxed);
    IUnknown* unknown=nullptr;
    DWORD length=sizeof(unknown);
    const HRESULT hr=resource->GetPrivateData(kResourceTag,&unknown,&length);
    if(FAILED(hr) || !unknown || length!=sizeof(unknown)) {
        if(unknown)unknown->Release();
        s.bindMissingTags.fetch_add(1,std::memory_order_relaxed);
        return;
    }
    auto* tag=static_cast<Lifetime*>(unknown); // unique private-data GUID
    if(tag->PromoteIfManaged(resource,s.settings.usedManagedPriority))
        s.bindPromoted.fetch_add(1,std::memory_order_relaxed);
    unknown->Release();
}
HRESULT STDMETHODCALLTYPE ObserveSetTexture(IDirect3DDevice9* d,DWORD stage,
                                           IDirect3DBaseTexture9* texture) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9*);
    const HRESULT hr=Original<Fn>(65)(d,stage,texture);
    if(SUCCEEDED(hr))PromoteBound(texture);
    return hr;
}
HRESULT STDMETHODCALLTYPE ObserveSetStreamSource(IDirect3DDevice9* d,UINT stream,
        IDirect3DVertexBuffer9* buffer,UINT offset,UINT stride) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,
                                         IDirect3DVertexBuffer9*,UINT,UINT);
    const HRESULT hr=Original<Fn>(100)(d,stream,buffer,offset,stride);
    if(SUCCEEDED(hr))PromoteBound(buffer);
    return hr;
}
HRESULT STDMETHODCALLTYPE ObserveSetIndices(IDirect3DDevice9* d,
                                           IDirect3DIndexBuffer9* buffer) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DIndexBuffer9*);
    const HRESULT hr=Original<Fn>(104)(d,buffer);
    if(SUCCEEDED(hr))PromoteBound(buffer);
    return hr;
}
// COM slot 17 (device Present), slot 42 (EndScene). These are vtable
// overrides only: the original slot may already point through another mod.
// Never change its code or call it with modified arguments.
HRESULT STDMETHODCALLTYPE ObservePresent(IDirect3DDevice9* d,const RECT* source,
    const RECT* destination,HWND window,const RGNDATA* dirty) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,const RECT*,
                                        const RECT*,HWND,const RGNDATA*);
    auto& s=S();
    const auto start=ClockUs();
    {
        std::lock_guard lock(s.pacingMutex);
        s.pacing.PresentEnter(start);
    }
    const HRESULT hr=Original<Fn>(17)(d,source,destination,window,dirty);
    {
        std::lock_guard lock(s.pacingMutex);
        s.pacing.PresentExit(ClockUs(),SUCCEEDED(hr));
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE ObserveEndScene(IDirect3DDevice9* d) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*);
    auto& s=S();
    const auto start=ClockUs();
    {
        std::lock_guard lock(s.pacingMutex);
        s.pacing.EndSceneEnter(start);
    }
    const HRESULT hr=Original<Fn>(42)(d);
    {
        std::lock_guard lock(s.pacingMutex);
        s.pacing.EndSceneExit(ClockUs());
    }
    return hr;
}
// D3D9 device vtable slots 81-84. Measure 1 of every 128 calls
// independently for each draw family. Unsampled draws pay one relaxed atomic
// increment, not QueryPerformanceCounter or a mutex. Forward all arguments,
// HRESULTs, and calls untouched, even when GTA/mod code sends unusual draws.
HRESULT STDMETHODCALLTYPE ObserveDrawPrimitive(
    IDirect3DDevice9* d,D3DPRIMITIVETYPE type,UINT start,UINT count) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT);
    constexpr auto kind=asc::render_cost::DrawKind::Primitive;
    const bool sample=S().drawCalls.Begin(kind);
    const uint64_t entered=sample?ClockTicks():0;
    const HRESULT result=Original<Fn>(81)(d,type,start,count);
    if(sample) {
        const uint64_t exited=ClockTicks();
        S().drawCalls.RecordSample(kind,exited>=entered?exited-entered:0,
            SUCCEEDED(result),ClockFrequency());
    }
    return result;
}
HRESULT STDMETHODCALLTYPE ObserveDrawIndexedPrimitive(
    IDirect3DDevice9* d,D3DPRIMITIVETYPE type,INT base,UINT minVertex,
    UINT numVertices,UINT startIndex,UINT primitiveCount) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,
        INT,UINT,UINT,UINT,UINT);
    constexpr auto kind=asc::render_cost::DrawKind::IndexedPrimitive;
    const bool sample=S().drawCalls.Begin(kind);
    const uint64_t entered=sample?ClockTicks():0;
    const HRESULT result=Original<Fn>(82)(d,type,base,minVertex,numVertices,startIndex,primitiveCount);
    if(sample) {
        const uint64_t exited=ClockTicks();
        S().drawCalls.RecordSample(kind,exited>=entered?exited-entered:0,
            SUCCEEDED(result),ClockFrequency());
    }
    return result;
}
HRESULT STDMETHODCALLTYPE ObserveDrawPrimitiveUP(
    IDirect3DDevice9* d,D3DPRIMITIVETYPE type,UINT primitiveCount,
    const void* vertexData,UINT vertexStride) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,
        UINT,const void*,UINT);
    constexpr auto kind=asc::render_cost::DrawKind::PrimitiveUP;
    const bool sample=S().drawCalls.Begin(kind);
    const uint64_t entered=sample?ClockTicks():0;
    const HRESULT result=Original<Fn>(83)(d,type,primitiveCount,vertexData,vertexStride);
    if(sample) {
        const uint64_t exited=ClockTicks();
        S().drawCalls.RecordSample(kind,exited>=entered?exited-entered:0,
            SUCCEEDED(result),ClockFrequency());
    }
    return result;
}
HRESULT STDMETHODCALLTYPE ObserveDrawIndexedPrimitiveUP(
    IDirect3DDevice9* d,D3DPRIMITIVETYPE type,UINT minVertex,UINT numVertices,
    UINT primitiveCount,const void* indices,D3DFORMAT indexFormat,
    const void* vertices,UINT vertexStride) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,
        UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT);
    constexpr auto kind=asc::render_cost::DrawKind::IndexedPrimitiveUP;
    const bool sample=S().drawCalls.Begin(kind);
    const uint64_t entered=sample?ClockTicks():0;
    const HRESULT result=Original<Fn>(84)(d,type,minVertex,numVertices,primitiveCount,
        indices,indexFormat,vertices,vertexStride);
    if(sample) {
        const uint64_t exited=ClockTicks();
        S().drawCalls.RecordSample(kind,exited>=entered?exited-entered:0,
            SUCCEEDED(result),ClockFrequency());
    }
    return result;
}
HRESULT STDMETHODCALLTYPE ResetDevice(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* params) {
    using Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
    // Release temporary COM refs before resetting the device.
    ClearPreload();
    if(S().settings.framePacingTelemetry) {
        std::lock_guard lock(S().pacingMutex);
        S().pacing.DeviceReset();
    }
    return Original<Fn>(16)(d,params);
}
bool Install(IDirect3DDevice9* d,std::ostream& log) {
    auto& s=S();
    if(!s.settings.enabled||!d)return false;
    if(s.device){
        if(s.device!=d || !Readable(d,sizeof(void*)) ||
           *reinterpret_cast<void***>(d)!=s.Table()) {
            if(!s.warnedHookOrder) {
                s.warnedHookOrder=true;
                log<<"GPU residency: device ownership changed; not overwriting a foreign D3D9 hook\n";
            }
        }
        return s.device==d;
    }
    if(!Readable(d,sizeof(void*)))return false;
    // SanFrame's own Present hook already measures frames and submits the OSD.
    // Do not add optional DistantStreaming Present/EndScene observers on top.
    // This must be evaluated at installation, not at ASI DLL load: the loader
    // order is unspecified. SanFrame itself defaults to Present-only OSD so
    // a later SanFrame load also avoids its optional EndScene detour.
    const bool sanFrameLoaded =
        GetModuleHandleW(L"SanFrameOptimizer.asi") != nullptr;
    const bool probeEnabled = asc::renderer_coexistence::enableDistantFrameProbe(
        s.settings.framePacingTelemetry, sanFrameLoaded);
    if(s.settings.framePacingTelemetry && !probeEnabled) {
        s.settings.framePacingTelemetry = false;
        log<<"GPU residency: SanFrame detected; skipping optional DistantStreaming "
              "Present/EndScene frame probes. Resource tracking and DXGI budgets "
              "remain enabled.\\n";
    }
    void** orig=*reinterpret_cast<void***>(d);
    if(!Readable(orig,DeviceMethods*sizeof(void*)))return false;
    MEMORY_BASIC_INFORMATION region{};
    if(!VirtualQuery(orig,&region,sizeof(region)))return false;
    const uintptr_t end=reinterpret_cast<uintptr_t>(region.BaseAddress)+region.RegionSize;
    const uintptr_t begin=reinterpret_cast<uintptr_t>(orig);
    if(end<begin)return false;
    const size_t slots=std::min(static_cast<size_t>((end-begin)/sizeof(void*)),MaxVtableSlots);
    if(slots<DeviceMethods)return false;
    try {s.clone.resize(slots+1);} catch(const std::bad_alloc&) {return false;}
    if(Readable(orig-1,sizeof(void*)))s.clone[0]=orig[-1];
    std::copy_n(orig,slots,s.Table());
    s.original=orig;
    s.Table()[16]=reinterpret_cast<void*>(&ResetDevice);
    if(s.settings.framePacingTelemetry) {
        s.Table()[17]=reinterpret_cast<void*>(&ObservePresent);
        s.Table()[42]=reinterpret_cast<void*>(&ObserveEndScene);
    }
    if(s.settings.drawCallTelemetry) {
        s.Table()[81]=reinterpret_cast<void*>(&ObserveDrawPrimitive);
        s.Table()[82]=reinterpret_cast<void*>(&ObserveDrawIndexedPrimitive);
        s.Table()[83]=reinterpret_cast<void*>(&ObserveDrawPrimitiveUP);
        s.Table()[84]=reinterpret_cast<void*>(&ObserveDrawIndexedPrimitiveUP);
    }
    if(s.settings.manageResources && s.settings.prioritizeUsedManaged) {
        s.Table()[65]=reinterpret_cast<void*>(&ObserveSetTexture);
        s.Table()[100]=reinterpret_cast<void*>(&ObserveSetStreamSource);
        s.Table()[104]=reinterpret_cast<void*>(&ObserveSetIndices);
    }
    s.Table()[23]=reinterpret_cast<void*>(&MakeTexture);
    s.Table()[24]=reinterpret_cast<void*>(&MakeVolume);
    s.Table()[25]=reinterpret_cast<void*>(&MakeCube);
    s.Table()[26]=reinterpret_cast<void*>(&MakeVB);
    s.Table()[27]=reinterpret_cast<void*>(&MakeIB);
    s.Table()[28]=reinterpret_cast<void*>(&MakeRT);
    s.Table()[29]=reinterpret_cast<void*>(&MakeDS);
    s.Table()[36]=reinterpret_cast<void*>(&MakeOffscreen);
    if(!ChangeTable(d,s.Table())) {s.original=nullptr;s.clone.clear();return false;}
    s.device=d;
    D3DDEVICE_CREATION_PARAMETERS creation{};
    if(SUCCEEDED(d->GetCreationParameters(&creation))) {
        IDirect3D9* d3d=nullptr;
        if(SUCCEEDED(d->GetDirect3D(&d3d))&&d3d) {
            D3DADAPTER_IDENTIFIER9 id{};
            if(SUCCEEDED(d3d->GetAdapterIdentifier(creation.AdapterOrdinal,0,&id))) {
                s.vendor=id.VendorId;s.deviceId=id.DeviceId;
            }
            d3d->Release();
        }
    }
    log<<"GPU residency: observed-use managed promotion="
        <<(s.settings.prioritizeUsedManaged?"enabled":"disabled")
        <<", priority="<<s.settings.usedManagedPriority
        <<", bind sample rate=1/"<<s.settings.bindPrioritySampleRate
        <<"; leaves D3D9 resource pools and CPU-backed managed copies unchanged\n";
    log<<"GPU residency: managed priority="<<s.settings.managedPriority
       <<", queued preload="<<(s.settings.preloadManaged?"enabled":"disabled")
       <<", per-frame calls="<<s.settings.preloadJobsPerFrame
       <<", per-frame soft budget us="<<s.settings.preloadBudgetUs<<"\\n";
    log<<"Frame pacing: D3D9 vtable Present/EndScene observation="
       <<(s.settings.framePacingTelemetry?"enabled":"disabled")
       <<", chaining original COM slots; no executable patches, no sleep or limiter. "
         "PresentEx/additional swap chains are not observed.\\n";
    log<<"Frame pacing: sampled D3D9 DrawPrimitive families="
       <<(s.settings.drawCallTelemetry?"enabled":"disabled")
       <<", rate=1/"<<asc::render_cost::DrawSampleRate
       <<", original COM slots forwarded, no GPU queries.\n";
    log<<"GPU residency: original D3D9 allocation hooks for 8 creation families;"
          " no helper / resource proxies; 64-bit accounting active, clone slots="
       <<slots<<"\n";
    return true;
}
void BudgetWorker() {
    auto& s=S();
    // Query budget off the game thread. Match physical adapter vendor/device;
    // refuse ambiguous multi-GPU matches rather than log another GPU's budget.
    IDXGIFactory1* factory=nullptr;
    IDXGIAdapter3* selected=nullptr;
    if(SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                reinterpret_cast<void**>(&factory)))&&factory) {
        unsigned matches=0;
        for(UINT i=0;;++i) {
            IDXGIAdapter1* adapter=nullptr;
            if(factory->EnumAdapters1(i,&adapter)==DXGI_ERROR_NOT_FOUND)break;
            if(!adapter)break;
            DXGI_ADAPTER_DESC1 desc{};
            if(SUCCEEDED(adapter->GetDesc1(&desc)) &&
               s.vendor!=0 && desc.VendorId==s.vendor && desc.DeviceId==s.deviceId) {
                ++matches;
                if(matches==1)adapter->QueryInterface(__uuidof(IDXGIAdapter3),
                                               reinterpret_cast<void**>(&selected));
            }
            adapter->Release();
        }
        if(matches!=1) {if(selected){selected->Release();selected=nullptr;}}
        factory->Release();
    }
    while(!s.stopping.load(std::memory_order_acquire)) {
        if(selected) {
            DXGI_QUERY_VIDEO_MEMORY_INFO local{},nonlocal{};
            if(SUCCEEDED(selected->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&local))) {
                s.localBudget.store(local.Budget,std::memory_order_relaxed);
                s.localUsage.store(local.CurrentUsage,std::memory_order_relaxed);
                s.budgetKnown.store(true,std::memory_order_release);
                // Avoid speculative streaming when Windows assigns a smaller
                // budget than our user target or usage is near the budget.
                const auto decision=asc::gpu_hot_residency::Decide({
                    local.CurrentUsage,local.Budget,
                    uint64_t(s.settings.targetMiB)*MiB,
                    uint64_t(s.settings.headroomMiB)*MiB,true});
                s.effectiveGpuLimitBytes.store(
                    decision.effectiveTargetBytes,std::memory_order_relaxed);
                s.pressure.store(!decision.allowSpeculativeUpload,
                                 std::memory_order_release);
            }
            if(SUCCEEDED(selected->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&nonlocal)))
                s.nonLocalUsage.store(nonlocal.CurrentUsage,std::memory_order_relaxed);
        }
        std::unique_lock lk(s.wakeMutex);
        s.wake.wait_for(lk,std::chrono::milliseconds(s.settings.sampleMs),
            [&s]{return s.stopping.load(std::memory_order_acquire);});
    }
    if(selected)selected->Release();
}
}
uint64_t BeginCallbackSample() noexcept {
    return S().settings.framePacingTelemetry?ClockUs():0;
}
void EndCallbackSample(bool drawing,uint64_t startUs) noexcept {
    auto& s=S();
    if(!s.settings.framePacingTelemetry || !startUs)return;
    const uint64_t end=ClockUs();
    if(end<startUs)return;
    std::lock_guard lock(s.pacingMutex);
    if(drawing)s.pacing.DrawingCallback(end-startUs);
    else s.pacing.ProcessCallback(end-startUs);
}
void EndRenderStage(asc::render_cost::Stage stage,uint64_t startedUs) noexcept {
    auto& s=S();
    if(!s.settings.framePacingTelemetry || !startedUs)return;
    const uint64_t ended=ClockUs();
    if(ended<startedUs)return;
    std::lock_guard lock(s.pacingMutex);
    s.renderStages.Add(stage,ended-startedUs);
}
void Configure(Settings settings) {
    S().settings=settings;
    S().settings.targetMiB=std::clamp(settings.targetMiB,512u,32768u);
    S().settings.headroomMiB=std::clamp(settings.headroomMiB,128u,4096u);
    S().settings.sampleMs=std::clamp(settings.sampleMs,250u,5000u);
    S().settings.reportMs=std::clamp(settings.reportMs,1000u,60000u);
    S().settings.usedManagedPriority=std::clamp(settings.usedManagedPriority,0u,256u);
    S().settings.bindPrioritySampleRate=std::clamp(settings.bindPrioritySampleRate,32u,1024u);
}
bool UnderPressure() noexcept {return S().pressure.load(std::memory_order_acquire);}
void Frame(IDirect3DDevice9* device,uint32_t now,bool streamingPressure,std::ostream& log) {
    auto& s=S();
    s.streamingPressure.store(streamingPressure,std::memory_order_relaxed);
    if(!s.settings.enabled||!device||s.stopping.load(std::memory_order_acquire))return;
    if(!s.device&&Install(device,log)) {
        try {s.sampler=std::thread(&BudgetWorker);}
        catch(const std::system_error&) {
            log<<"GPU residency: DXGI budget worker unavailable; resource tracking remains enabled\n";
        }
    }
    if(!s.device)return;
    DrainPreload(now);
    if(uint32_t(now-s.lastReport)<s.settings.reportMs)return;
    s.lastReport=now;
    const auto& t=s.counts[static_cast<unsigned>(Kind::Texture)];
    const auto& b=s.counts[static_cast<unsigned>(Kind::Buffer)];
    const auto& f=s.counts[static_cast<unsigned>(Kind::Surface)];
    log<<"GPU residency: textures="<<t.live.load()<<", buffers="<<b.live.load()
       <<", surfaces="<<f.live.load()
       <<", tracked estimated allocation MiB="
       <<(t.bytes.load()+b.bytes.load()+f.bytes.load())/MiB
       <<", created="<<t.created.load()+b.created.load()+f.created.load()
       <<", destroyed="<<t.destroyed.load()+b.destroyed.load()+f.destroyed.load()
       <<", managed estimate MiB="<<s.managedBytes.load()/MiB
       <<", default estimate MiB="<<s.defaultBytes.load()/MiB
       <<", other estimate MiB="<<s.otherBytes.load()/MiB
       <<", tag failures="<<s.attachFailed.load()
       <<", preload queued total="<<s.queuedTotal.load()
       <<", preloaded="<<s.preloaded.load()
       <<", preload stale="<<s.preloadSkipped.load()
       <<", preload early-expired="<<s.expiredEarly.load()
       <<", preload slow="<<s.preloadSlow.load()
       <<", preload max us="<<s.preloadMaxUs.load()
       <<", preload drops="<<s.queueDrops.load()
       <<", failed GPU creations="<<s.creationFailures.load()
       <<", OOM creations="<<s.videoMemoryFailures.load()
       <<", speculativeOomCooldown="
       <<(GetTickCount64()<s.oomCooloffUntil.load() ? 1 : 0);
    if(s.budgetKnown.load(std::memory_order_acquire))
        log<<", WDDM LOCAL process usage MiB="<<s.localUsage.load()/MiB
           <<", WDDM LOCAL process budget MiB="<<s.localBudget.load()/MiB
           <<", WDDM NON_LOCAL process usage MiB="<<s.nonLocalUsage.load()/MiB
           <<", pressure="<<(UnderPressure()?"high":"normal")
           <<", safe speculative GPU threshold MiB="
           <<s.effectiveGpuLimitBytes.load()/MiB;
    else log<<", WDDM budget=unavailable/ambiguous adapter";
    log<<", target MiB="<<s.settings.targetMiB
       <<" (target is NOT guaranteed allocation or physical residency)\n";
    if(s.settings.framePacingTelemetry) {
        asc::frame_pacing::Snapshot p{};
        {
            std::lock_guard lock(s.pacingMutex);
            p=s.pacing.TakeWindow();
        }
        // Intervals are CPU wall time, not GPU timings or an FPS limiter.
        auto reportTiming=[&](const char* label,const asc::frame_pacing::Timing& t) {
            log<<", "<<label<<" avg/peak/count us="
               <<t.Average()<<"/"<<t.maxUs<<"/"<<t.count;
        };
        log<<"Frame pacing D3D9: Present="<<p.presentCalls
           <<", failed="<<p.presentFailures
           <<", EndScene="<<p.endSceneCalls
           <<", resets="<<p.resets
           <<", clockRegressions="<<p.backwardClock
           <<", interval buckets (<8/8-25/25-42/42-58/58-83/>=83ms)=";
        for(size_t i=0;i<p.presentBuckets.size();++i) {
            if(i)log<<"/";
            log<<p.presentBuckets[i];
        }
        reportTiming("Present interval",p.presentInterval);
        reportTiming("in Present",p.presentCall);
        reportTiming("outside Present",p.outsidePresent);
        reportTiming("EndScene call",p.endSceneCall);
        reportTiming("EndScene to Present",p.endSceneToPresent);
        log<<"\n";
        log<<"Frame pacing callbacks (plugin time only, not full GTA frame):";
        reportTiming("drawingEvent",p.drawingCallback);
        reportTiming("gameProcessEvent",p.processCallback);
        log<<"\n";
        asc::render_cost::StageSnapshot stageWindow{};
        {
            std::lock_guard lock(s.pacingMutex);
            stageWindow=s.renderStages.TakeWindow();
        }
        log<<"Frame pacing drawing stages (plugin CPU wall us; not total GTA frame):";
        for(unsigned i=0;i<stageWindow.stages.size();++i) {
            const auto& t=stageWindow.stages[i];
            log<<", "<<asc::render_cost::StageNames[i]<<"="
               <<t.Average()<<"/"<<t.peakUs<<"/"<<t.calls;
        }
        log<<"\n";
        if(!p.presentCalls)
            log<<"Frame pacing NOTE: no device Present calls observed; the game "
                  "may use PresentEx, a swap-chain Present, or a foreign "
                  "device table (not proof of absent frames).\n";
    }
    if(s.settings.drawCallTelemetry) {
        const auto sample=s.drawCalls.TakeWindow();
        const auto frequency=ClockFrequency();
        log<<"Frame pacing draw calls (calls/samples, sampled avg/peak CPU us; rate=1/"
           <<asc::render_cost::DrawSampleRate<<"):";
        for(unsigned i=0;i<sample.kinds.size();++i) {
            const auto& k=sample.kinds[i];
            const uint64_t meanTicks=k.samples?k.totalTicks/k.samples:0;
            log<<", "<<asc::render_cost::DrawNames[i]<<"="<<k.calls<<"/"<<k.samples
               <<"/"<<asc::render_cost::TicksToUs(meanTicks,frequency)
               <<"/"<<asc::render_cost::TicksToUs(k.peakTicks,frequency)
               <<"/slow1ms="<<k.slow1ms
               <<"/slow5ms="<<k.slow5ms
               <<"/slow15ms="<<k.slow15ms
               <<"/failedSample="<<k.failures;
        }
        log<<" (sampled driver wall time only; not GPU execution)\n";
    }
    log.flush();
}
void Stop() {
    auto& s=S();
    s.stopping.store(true,std::memory_order_release);
    // Shutdown callback is on game thread, not DllMain.
    ClearPreload();
    s.wake.notify_all();
    if(s.sampler.joinable())s.sampler.join();
    if(s.device&&Readable(s.device,sizeof(void*)) &&
       *reinterpret_cast<void***>(s.device)==s.Table())ChangeTable(s.device,s.original);
    s.device=nullptr;
    // Retain clone allocation until process exit: callbacks from a chained
    // third-party hook may still reference it during shutdown.
}
}
