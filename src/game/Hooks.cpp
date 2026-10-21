#include "Hooks.h"
#include "asc/Core.h"
#include "asc/OffscreenCleanup.h"
#include "asc/ArchiveSampling.h"
#include "asc/HookIntegrity.h"
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <array>
#include <string>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <intrin.h>
#include <algorithm>

namespace hooks {
namespace {
using RequestFn=void(__cdecl*)(int,int);
using LoadFn=void(__cdecl*)();
using ChooseFn=int(__cdecl*)(uint32_t,bool);
constexpr uintptr_t kRequest=0x4087E0,kLoad=0x40E3A0,kChoose=0x408E20;
RequestFn origRequest=nullptr;
LoadFn origLoad=nullptr;
ChooseFn origChoose=nullptr;
thread_local unsigned pluginDepth=0;
bool installed=false, archiveActive=false;
bool requestOwnershipLost=false;
std::string installReport;
unsigned archiveSampleMask=15;
unsigned archiveSlowThresholdUs=1000;
LARGE_INTEGER frequency{};
DWORD mainThreadId=0;
// Accessed only from the installing/game thread. Never read GTA model state
// or allocate memory in the hook; non-main-thread traffic stays counter-only.
RequestProfileBatch requestProfileBatch{};
uint32_t nativeSampleSequence=0;

// One pending record per model/thread lane; repeats cannot overflow a queue.
asc::RequestObservations<asc::kVanillaModelInfoCountCap> observations;
asc::offscreen::RecentRequests<25000> recentRequests; // models + vanilla TXDs
std::atomic<uint64_t> coalesced{0},ignored{0},requests{0},
                       nativeRequests{0},pluginRequests{0},nonMainNative{0},loads{0},
                      loadUs{0},peakLoadUs{0},
                      choices{0},chooseUs{0},archiveSamples{0},
                      archivePeak{0},archiveSlow{0},archiveNoFile{0};
std::array<std::atomic<uint64_t>,8> archiveBins{};

uint64_t Duration(LARGE_INTEGER a,LARGE_INTEGER b) {
    if(frequency.QuadPart<=0||b.QuadPart<a.QuadPart)return 0;
    return uint64_t((static_cast<long double>(b.QuadPart-a.QuadPart)*1000000.0L)/
                    frequency.QuadPart);
}
void Maximum(std::atomic<uint64_t>&peak,uint64_t n){
    uint64_t old=peak.load(std::memory_order_relaxed);
    while(old<n&&!peak.compare_exchange_weak(old,n,
            std::memory_order_relaxed)){}
}
__declspec(noinline) void __cdecl RequestDetour(int id,int flags){
    const DWORD tid=GetCurrentThreadId();
    const bool native=pluginDepth==0;
    const bool onMain=tid==mainThreadId;
    const bool sample=native && onMain && (++nativeSampleSequence & 127u)==0;
    LARGE_INTEGER t0{},t1{},t2{};
    if(sample)QueryPerformanceCounter(&t0);
    const uint32_t now=GetTickCount();
    recentRequests.Record(id,now);
    if(native){
        nativeRequests.fetch_add(1,std::memory_order_relaxed);
        if(!onMain)nonMainNative.fetch_add(1,std::memory_order_relaxed);
        const auto result=observations.Observe(id,flags,now,onMain);
        if(result==asc::ObservationResult::Coalesced)
            coalesced.fetch_add(1,std::memory_order_relaxed);
        else if(result==asc::ObservationResult::Ignored)
            ignored.fetch_add(1,std::memory_order_relaxed);
    } else {
        pluginRequests.fetch_add(1,std::memory_order_relaxed);
    }
    requests.fetch_add(1,std::memory_order_relaxed);
    if(sample){
        // The native caller's return address (not a guessed hook trampoline)
        // is captured only on the sampled path. No stack unwind is attempted.
        if(requestProfileBatch.count<requestProfileBatch.samples.size()){
            requestProfileBatch.samples[requestProfileBatch.count++]={
                reinterpret_cast<uintptr_t>(_ReturnAddress()),id,now};
        }else ++requestProfileBatch.dropped;
        QueryPerformanceCounter(&t1);
    }
    // Always forward original arguments; don't coalesce/drop native requests.
    origRequest(id,flags);
    if(sample){
        const DWORD nativeError=GetLastError();
        QueryPerformanceCounter(&t2);
        // Preserve raw QPC ticks; integer microseconds rounded nearly every
        // sampled call to zero in the previous profiler.
        const uint64_t ours=t1.QuadPart>=t0.QuadPart
            ? uint64_t(t1.QuadPart-t0.QuadPart):0;
        const uint64_t original=t2.QuadPart>=t1.QuadPart
            ? uint64_t(t2.QuadPart-t1.QuadPart):0;
        requestProfileBatch.hookTicks+=ours;
        requestProfileBatch.originalTicks+=original;
        requestProfileBatch.hookPeakTicks=std::max(requestProfileBatch.hookPeakTicks,ours);
        requestProfileBatch.originalPeakTicks=std::max(requestProfileBatch.originalPeakTicks,original);
        ++requestProfileBatch.timed;
        SetLastError(nativeError);
    }
}
void __cdecl LoadDetour(){
    LARGE_INTEGER a{},b{};
    QueryPerformanceCounter(&a);
    origLoad();
    QueryPerformanceCounter(&b);
    const uint64_t us=Duration(a,b);
    loads.fetch_add(1,std::memory_order_relaxed);
    loadUs.fetch_add(us,std::memory_order_relaxed);
    Maximum(peakLoadUs,us);
}
int __cdecl ChooseDetour(uint32_t pos,bool notPriority){
    // Low-overhead, unbiased per-thread sampling. Every call still returns
    // the exact original result. No engine structures are mutated.
    thread_local unsigned sampleCounter=0;
    const bool sampled=asc::archive::ShouldSample(++sampleCounter,archiveSampleMask+1);
    LARGE_INTEGER start{},end{};
    if(sampled)QueryPerformanceCounter(&start);
    const int result=origChoose(pos,notPriority);
    choices.fetch_add(1,std::memory_order_relaxed);
    if(!sampled)return result;
    QueryPerformanceCounter(&end);
    const uint64_t us=Duration(start,end);
    archiveSamples.fetch_add(1,std::memory_order_relaxed);
    chooseUs.fetch_add(us,std::memory_order_relaxed);
    Maximum(archivePeak,us);
    if(us>=archiveSlowThresholdUs)
        archiveSlow.fetch_add(1,std::memory_order_relaxed);
    if(result<0)
        archiveNoFile.fetch_add(1,std::memory_order_relaxed);
    // [0..7] = <25, <50, <100, <250, <500, <1000, <2500, >=2500 us.
    const unsigned bin=asc::archive::LatencyBin(us);
    archiveBins[bin].fetch_add(1,std::memory_order_relaxed);
    return result;
}
// These signatures were verified against the user's supplied GTA SA 1.0 US
// HOODLUM executable.  Do not relax them to accommodate unknown ASI patches.
// In particular, a jump installed by another ASI must not be overwritten.
constexpr unsigned char kRequestEntry[] = {
    0x53,0x8B,0x5C,0x24,0x0C,0x55,0x8B,0x6C,0x24,0x0C,0x56,0x57
};
constexpr unsigned char kHoodlumLoadEntry[] = {
    0xE9,0xFB,0x8C,0x15,0x01
};
// The pristine HOODLUM redirect destination is verified against the user's
// executable before recovering its page protection or hooking the entry.
constexpr uintptr_t kHoodlumLoadDestination=0x015670A0;
constexpr unsigned char kHoodlumLoadDestinationEntry[] = {
    0xA0,0x58,0x4A,0x8E,0x00,0x84,0xC0,0x74,
    0x09,0x33,0xC0,0xA3,0x34,0x55,0x96,0x00
};

bool IsExecutableProtection(DWORD flags){
    const DWORD p=flags&0xFF;
    return p==PAGE_EXECUTE || p==PAGE_EXECUTE_READ ||
           p==PAGE_EXECUTE_READWRITE || p==PAGE_EXECUTE_WRITECOPY;
}
bool IsReadableProtection(DWORD flags){
    const DWORD p=flags&0xFF;
    return p==PAGE_READONLY || p==PAGE_READWRITE ||
           p==PAGE_WRITECOPY || IsExecutableProtection(flags);
}
bool QueryCommitted(uintptr_t addr,MEMORY_BASIC_INFORMATION& mbi){
    return VirtualQuery(reinterpret_cast<const void*>(addr),&mbi,
                        sizeof(mbi))==sizeof(mbi) &&
           mbi.State==MEM_COMMIT &&
           !(mbi.Protect&(PAGE_GUARD|PAGE_NOACCESS));
}
// Verify that a hook is inside the host executable's *executable* PE section,
// not merely in an arbitrarily allocated page or a DLL that reused an address.
bool IsHostExecutableSection(uintptr_t address){
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if(base!=0x400000 || address<base)return false;
    const auto*dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE ||
       dos->e_lfanew<64 || dos->e_lfanew>4096)return false;
    const auto*nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE ||
       nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
       nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC)return false;
    const uintptr_t rva=address-base;
    if(rva>=nt->OptionalHeader.SizeOfImage)return false;
    const IMAGE_SECTION_HEADER*sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){
        const auto& sec=sections[i];
        const uintptr_t begin=sec.VirtualAddress;
        const uintptr_t size=sec.Misc.VirtualSize>sec.SizeOfRawData ?
                             sec.Misc.VirtualSize:sec.SizeOfRawData;
        if(rva>=begin && rva-begin<size)
            return (sec.Characteristics&IMAGE_SCN_MEM_EXECUTE)!=0;
    }
    return false;
}
bool HasEntry(uintptr_t addr,const unsigned char*bytes,size_t length,
              const MEMORY_BASIC_INFORMATION& mbi){
    const uintptr_t regionEnd=reinterpret_cast<uintptr_t>(mbi.BaseAddress)
                              +mbi.RegionSize;
    if(addr>regionEnd || length>regionEnd-addr ||
       !IsReadableProtection(mbi.Protect))return false;
    return std::memcmp(reinterpret_cast<const void*>(addr),bytes,length)==0;
}
bool SafeTarget(uintptr_t addr,const char* name){
    MEMORY_BASIC_INFORMATION mbi{};
    if(!QueryCommitted(addr,mbi)){
        installReport+=std::string(name)+": no readable committed target page\n";
        return false;
    }
    char line[256]{};
    std::snprintf(line,sizeof(line),
        "%s: address=0x%08lX state=0x%lX protect=0x%lX type=0x%lX",
        name,(unsigned long)addr,(unsigned long)mbi.State,
        (unsigned long)mbi.Protect,(unsigned long)mbi.Type);
    installReport+=line;
    if(mbi.Type!=MEM_IMAGE || !IsHostExecutableSection(addr)){
        installReport+=" SKIPPED (not executable section of game image)\n";
        return false;
    }

    if(addr==kRequest){
        // Some HOODLUM deployments leave this executable .text page as
        // PAGE_READWRITE (0x04), typically due to a prior code patch.
        // Only repair protection when the *live* bytes match the verified
        // pristine entry. This does not modify, unhook, or unload other ASIs.
        if(!HasEntry(addr,kRequestEntry,sizeof(kRequestEntry),mbi)){
            installReport+=" SKIPPED (unknown Request entry bytes)\n";
            return false;
        }
        if(!IsExecutableProtection(mbi.Protect)){
            // Never make arbitrary writable pages executable: only permit
            // precisely PAGE_READWRITE in a known PE executable section.
            if(mbi.Protect!=PAGE_READWRITE){
                installReport+=" SKIPPED (unsupported page protection)\n";
                return false;
            }
            DWORD previous=0;
            if(!VirtualProtect(reinterpret_cast<void*>(addr),
                               sizeof(kRequestEntry),PAGE_EXECUTE_READ,&previous)){
                installReport+=" SKIPPED (VirtualProtect RX failed)\n";
                return false;
            }
            MEMORY_BASIC_INFORMATION updated{};
            if(!QueryCommitted(addr,updated) ||
               !IsExecutableProtection(updated.Protect)){
                installReport+=" SKIPPED (RX protection verification failed)\n";
                return false;
            }
            // Leave the page executable after detour installation and removal:
            // restoring PAGE_READWRITE would make vanilla code non-executable.
            installReport+=" RECOVERED (verified Request code; RW -> RX)\n";
        }else{
            installReport+=" eligible (Request signature matched)\n";
        }
        return true;
    }

    if(!IsExecutableProtection(mbi.Protect)){
        installReport+=" SKIPPED (non-executable)\n";
        return false;
    }
    if(addr==kLoad &&
       HasEntry(addr,kHoodlumLoadEntry,sizeof(kHoodlumLoadEntry),mbi)){
        // A 5-byte E9 inside the disk image redirects into .HOODLUM.  It is
        // not itself evidence of a third-party hook. MinHook's trampoline
        // must preserve/chase this original relative jump.
        int32_t displacement=0;
        std::memcpy(&displacement,reinterpret_cast<const void*>(addr+1),
                    sizeof(displacement));
        const auto destination=static_cast<uintptr_t>(
            static_cast<int64_t>(addr)+5+displacement);
        // Report *why* a destination failed.  Earlier builds treated a
        // non-executable (but pristine) HOODLUM code page as a generic invalid
        // redirect, making the actual runtime blocker impossible to identify.
        MEMORY_BASIC_INFORMATION dst{};
        const SIZE_T queried=VirtualQuery(
            reinterpret_cast<const void*>(destination),&dst,sizeof(dst));
        char detail[256]{};
        std::snprintf(detail,sizeof(detail),
            " -> 0x%08lX [VirtualQuery=%lu state=0x%lX protect=0x%lX type=0x%lX]",
            (unsigned long)destination,(unsigned long)queried,
            (unsigned long)dst.State,(unsigned long)dst.Protect,
            (unsigned long)dst.Type);
        installReport+=detail;

        if(destination!=kHoodlumLoadDestination ||
           !IsHostExecutableSection(destination)){
            installReport+=" SKIPPED (unexpected destination / non-executable PE section)\n";
            return false;
        }
        if(!QueryCommitted(destination,dst) || dst.Type!=MEM_IMAGE){
            installReport+=" SKIPPED (destination not committed readable image memory)\n";
            return false;
        }
        if(!HasEntry(destination,kHoodlumLoadDestinationEntry,
                     sizeof(kHoodlumLoadDestinationEntry),dst)){
            installReport+=" SKIPPED (HOODLUM destination signature mismatch)\n";
            return false;
        }
        if(!IsExecutableProtection(dst.Protect)){
            // Only recover exactly PAGE_READWRITE on a pristine, committed,
            // executable-on-disk image section.  Do not touch guard pages,
            // privately allocated code, or unrecognized ASI modifications.
            if(dst.Protect!=PAGE_READWRITE){
                installReport+=" SKIPPED (unsupported HOODLUM destination protection)\n";
                return false;
            }
            DWORD previous=0;
            if(!VirtualProtect(reinterpret_cast<void*>(destination),
                               sizeof(kHoodlumLoadDestinationEntry),
                               PAGE_EXECUTE_READ,&previous)){
                installReport+=" SKIPPED (HOODLUM destination VirtualProtect RX failed)\n";
                return false;
            }
            MEMORY_BASIC_INFORMATION updated{};
            if(!QueryCommitted(destination,updated) ||
               !IsExecutableProtection(updated.Protect)){
                installReport+=" SKIPPED (HOODLUM destination RX verification failed)\n";
                return false;
            }
            installReport+=" RECOVERED (verified HOODLUM destination; RW -> RX; trampoline chained)\n";
        }else{
            installReport+=" eligible (verified HOODLUM destination; trampoline chained)\n";
        }
        return true;
    }
    const auto op=*reinterpret_cast<const unsigned char*>(addr);
    if(op==0xE9||op==0xEB||op==0xCC||op==0xC3){
        installReport+=" SKIPPED (unknown detour/invalid entry)\n";
        return false;
    }
    installReport+=" eligible\n";
    return true;
}
bool Create(uintptr_t addr,void*target,void**old,const char* name){
    if(!SafeTarget(addr,name))return false;
    const MH_STATUS status=MH_CreateHook(reinterpret_cast<void*>(addr),target,old);
    if(status!=MH_OK){
        installReport+=std::string(name)+": MH_CreateHook failed status="+
            std::to_string(static_cast<int>(status))+"\n";
        return false;
    }
    return true;
}
}
const char* InstallReport(){return installReport.c_str();}
bool Install(ArchiveProfileOptions options){
    if(installed)return true;
    installReport.clear();
    if(options.hookMode!=1 && options.hookMode!=2){
        installReport="DISABLED: invalid hook mode (requires 1 or 2)\n";
        return false;
    }
    // Request-only is the diagnostic default. Load instrumentation is optional
    // because legacy HOODLUM jump trampolines may interact with other ASIs.
    const bool wantLoad=options.hookMode==2;
    const MH_STATUS init=MH_Initialize();
    if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED){
        installReport="MH_Initialize failed: "+std::to_string(static_cast<int>(init))+"\n";
        return false;
    }
    requestOwnershipLost=false;
    observations.Reset();
    mainThreadId=GetCurrentThreadId();
    requestProfileBatch={};
    nativeSampleSequence=0;
    coalesced.store(0,std::memory_order_relaxed);
    ignored.store(0,std::memory_order_relaxed);
    nativeRequests.store(0,std::memory_order_relaxed);
    pluginRequests.store(0,std::memory_order_relaxed);
    nonMainNative.store(0,std::memory_order_relaxed);
    QueryPerformanceFrequency(&frequency);
    archiveActive=false;
    if(!asc::archive::ValidSampleRate(options.sampleEvery))
        options.sampleEvery=16;
    archiveSampleMask=options.sampleEvery-1;
    archiveSlowThresholdUs=options.slowThresholdUs?options.slowThresholdUs:1000;
    const bool requestOk=Create(kRequest,reinterpret_cast<void*>(&RequestDetour),
                                reinterpret_cast<void**>(&origRequest),"Request");
    bool loadOk=false;
    if(wantLoad && requestOk)
        loadOk=Create(kLoad,reinterpret_cast<void*>(&LoadDetour),
                      reinterpret_cast<void**>(&origLoad),"Load");
    if(requestOk && (!wantLoad || loadOk) && options.enabled && wantLoad)
        archiveActive=Create(kChoose,reinterpret_cast<void*>(&ChooseDetour),
                             reinterpret_cast<void**>(&origChoose),"Archive (optional)");
    if(!requestOk||(wantLoad&&!loadOk)){
        installReport+="DISABLED: requested hook unavailable\n";
        if(requestOk)MH_RemoveHook(reinterpret_cast<void*>(kRequest));
        if(loadOk)MH_RemoveHook(reinterpret_cast<void*>(kLoad));
        origRequest=nullptr;origLoad=nullptr;origChoose=nullptr;
        archiveActive=false;
        MH_Uninitialize();
        return false;
    }
    const MH_STATUS reqEnable=MH_EnableHook(reinterpret_cast<void*>(kRequest));
    const MH_STATUS loadEnable=wantLoad?
        MH_EnableHook(reinterpret_cast<void*>(kLoad)):MH_OK;
    if(reqEnable!=MH_OK||loadEnable!=MH_OK){
        installReport+="DISABLED: hook enable failed\n";
        if(reqEnable==MH_OK)MH_DisableHook(reinterpret_cast<void*>(kRequest));
        if(wantLoad&&loadEnable==MH_OK)
            MH_DisableHook(reinterpret_cast<void*>(kLoad));
        MH_RemoveHook(reinterpret_cast<void*>(kRequest));
        if(loadOk)MH_RemoveHook(reinterpret_cast<void*>(kLoad));
        if(archiveActive)MH_RemoveHook(reinterpret_cast<void*>(kChoose));
        MH_Uninitialize();
        origRequest=nullptr;origLoad=nullptr;origChoose=nullptr;
        archiveActive=false;
        return false;
    }
    if(archiveActive){
        if(MH_EnableHook(reinterpret_cast<void*>(kChoose))!=MH_OK){
            installReport+="DEGRADED: optional archive hook enable failed\n";
            MH_RemoveHook(reinterpret_cast<void*>(kChoose));
            origChoose=nullptr;
            archiveActive=false;
        }
    }
    installed=true;
    installReport+="Observations: per-model coalescing active; bounded fair drain\n";
    installReport+=wantLoad?
        "LEGACY: Request + Load hooks enabled (HOODLUM trampoline)\n":
        "ISOLATION: Request hook only; Load trampoline NOT installed\n";
    return true;
}
bool CheckIntegrity(bool recoverPristine, std::ostream& log){
    if(!installed || requestOwnershipLost)return false;
    MEMORY_BASIC_INFORMATION mem{};
    if(!QueryCommitted(kRequest,mem) ||
       !IsReadableProtection(mem.Protect) ||
       reinterpret_cast<uintptr_t>(mem.BaseAddress)+mem.RegionSize-kRequest <
           sizeof(kRequestEntry)){
        log<<"HOOK INTEGRITY: Request entry became unreadable; "
              "scheduler suspended\n";
        requestOwnershipLost=true;
        log.flush();
        return false;
    }
    unsigned char bytes[sizeof(kRequestEntry)]{};
    std::memcpy(bytes,reinterpret_cast<const void*>(kRequest),sizeof(bytes));
    const auto state=asc::hook_integrity::Classify(
        bytes,sizeof(bytes),kRequest,
        reinterpret_cast<uintptr_t>(&RequestDetour),
        kRequestEntry,sizeof(kRequestEntry));
    if(state==asc::hook_integrity::State::Owned) return true;
    if(state==asc::hook_integrity::State::Pristine && recoverPristine){
        // The original entry is restored. MinHook still owns the hook
        // bookkeeping, so cycle only this hook; do not touch other ASIs.
        const auto disable=MH_DisableHook(reinterpret_cast<void*>(kRequest));
        if(disable==MH_OK){
            const auto enable=MH_EnableHook(reinterpret_cast<void*>(kRequest));
            if(enable==MH_OK){
                unsigned char current[sizeof(kRequestEntry)]{};
                std::memcpy(current,reinterpret_cast<const void*>(kRequest),
                            sizeof(current));
                if(asc::hook_integrity::Classify(
                    current,sizeof(current),kRequest,
                    reinterpret_cast<uintptr_t>(&RequestDetour),
                    kRequestEntry,sizeof(kRequestEntry))==
                        asc::hook_integrity::State::Owned){
                    log<<"HOOK INTEGRITY: restored pristine Request entry; "
                          "Distant Streaming hook re-armed\n";
                    log.flush();
                    return true;
                }
            }
        }
    }
    requestOwnershipLost=true;
    log<<"HOOK INTEGRITY: Request hook replaced or lost; state="
       <<static_cast<int>(state)
       <<". Controller stopped. Not overwriting an unknown ASI detour.\n";
    log.flush();
    return false;
}
void Uninstall(){
    if(!installed)return;
    // Avoid MH_ALL_HOOKS: this ASI also uses MinHook for its D3D9 path.
    MH_DisableHook(reinterpret_cast<void*>(kRequest));
    MH_RemoveHook(reinterpret_cast<void*>(kRequest));
    if(origLoad){
        MH_DisableHook(reinterpret_cast<void*>(kLoad));
        MH_RemoveHook(reinterpret_cast<void*>(kLoad));
    }
    if(archiveActive){
        MH_DisableHook(reinterpret_cast<void*>(kChoose));
        MH_RemoveHook(reinterpret_cast<void*>(kChoose));
    }
    MH_Uninitialize();
    origRequest=nullptr;
    origLoad=nullptr;
    origChoose=nullptr;
    installed=false;
    archiveActive=false;
}
void EnterPluginRequest(){++pluginDepth;}
void ExitPluginRequest(){if(pluginDepth)--pluginDepth;}
bool IsInstalled(){return installed;}
bool IsArchiveProfilerActive(){return installed&&archiveActive;}
bool RequestedRecently(int id,uint32_t now,unsigned quietMs){return recentRequests.Recent(id,now,quietMs);}
RequestProfileBatch TakeRequestProfileBatch(){
    // Called by the once-per-second scheduler on the installing thread.
    // RequestDetour only fills this buffer on that same thread.
    RequestProfileBatch out=requestProfileBatch;
    requestProfileBatch={};
    return out;
}
Counters CollectCounters(){
    Counters out{};
    out.requestCalls=requests.exchange(0);
    out.nativeRequestCalls=nativeRequests.exchange(0);
    out.pluginRequestCalls=pluginRequests.exchange(0);
    out.nonMainNativeRequests=nonMainNative.exchange(0);
    out.requestCoalesced=coalesced.exchange(0,std::memory_order_relaxed);
    out.requestIgnored=ignored.exchange(0,std::memory_order_relaxed);
    out.loadCalls=loads.exchange(0);
    out.loadTotalUs=loadUs.exchange(0);
    out.loadPeakUs=peakLoadUs.exchange(0);
    out.archiveChoices=choices.exchange(0);
    out.archiveSamples=archiveSamples.exchange(0);
    out.archiveSelectionUs=chooseUs.exchange(0);
    out.archivePeakUs=archivePeak.exchange(0);
    out.archiveSlow=archiveSlow.exchange(0);
    out.archiveNoFile=archiveNoFile.exchange(0);
    for(unsigned i=0;i<8;++i)
        out.archiveBins[i]=archiveBins[i].exchange(0);
    return out;
}
void DrainVanillaRequests(void(*callback)(const RequestRecord&,void*),void*ctx){
    if(!callback)return;
    observations.Drain([&](const RequestRecord& rec){callback(rec,ctx);},1024);
}
}
