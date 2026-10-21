#include "StreamingMemoryWin.h"
#include <CStreaming.h>
#include "OlaBindingsWin.h"
#include <array>
#include <algorithm>
#include <cstring>

namespace {
constexpr uintptr_t kBudget=0x8A5A80;
constexpr uintptr_t kStartup=0x5B8E64;
constexpr uintptr_t kMakeSpaceFor=0x40E120,kMakeSpaceForEnd=0x40E170;
// Only ASC updates this scalar. Other ASIs retain their original public address.
uint32_t ownedBudget=0;
struct BudgetRead {uintptr_t operand=0;uint32_t original=0,replacement=0;};
std::array<BudgetRead,6> budgetReads{};
unsigned budgetReadCount=0;
uintptr_t makeSpaceForResume=kMakeSpaceFor+5;
// The verified HOODLUM stub implements the displaced MOV EAX,[budget] before
// returning to +5. Preserve that contract without its obfuscated pointer read.
__declspec(naked) void OwnedBudgetEntry(){
    __asm {
        mov eax, dword ptr [ownedBudget]
        jmp dword ptr [makeSpaceForResume]
    }
}

bool HostCode(uintptr_t address,size_t size){
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if(base!=0x400000 || address<base)return false;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(address-base>=nt->OptionalHeader.SizeOfImage)return false;
    const auto* sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){
        const auto& section=sections[i];
        const auto span=std::max(section.Misc.VirtualSize,section.SizeOfRawData);
        const auto offset=address-base;
        if(offset>=section.VirtualAddress && offset-section.VirtualAddress<span)
            return size<=span-(offset-section.VirtualAddress) &&
                   (section.Characteristics&IMAGE_SCN_MEM_EXECUTE)!=0;
    }
    return false;
}

bool Readable(uintptr_t address,size_t size,bool executable=false){
    MEMORY_BASIC_INFORMATION mbi{};
    if(!VirtualQuery(reinterpret_cast<void*>(address),&mbi,sizeof(mbi)) ||
       mbi.State!=MEM_COMMIT || (mbi.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const auto end=reinterpret_cast<uintptr_t>(mbi.BaseAddress)+mbi.RegionSize;
    if(address>end || size>end-address)return false;
    const auto protection=mbi.Protect&0xFF;
    // HOODLUM executes original code from image pages marked PAGE_READWRITE.
    // Validate the PE section as well as each patch's instruction fingerprint.
    if(executable && protection==PAGE_READWRITE)
        return mbi.Type==MEM_IMAGE && HostCode(address,size);
    return executable ? (protection==PAGE_EXECUTE_READ ||
        protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY) :
        (protection==PAGE_READONLY || protection==PAGE_READWRITE ||
         protection==PAGE_WRITECOPY || protection==PAGE_EXECUTE_READ ||
         protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY);
}
uint32_t Read32(uintptr_t address){
    uint32_t value=0;
    std::memcpy(&value,reinterpret_cast<const void*>(address),sizeof(value));
    return value;
}
bool WriteCode32(uintptr_t address,uint32_t value){
    DWORD previous=0;
    if(!VirtualProtect(reinterpret_cast<void*>(address),sizeof(value),
                       PAGE_EXECUTE_READWRITE,&previous))return false;
    std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(value));
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),sizeof(value));
    DWORD ignored=0;
    VirtualProtect(reinterpret_cast<void*>(address),sizeof(value),previous,&ignored);
    return true;
}
bool StartupInstruction(){
    if(!Readable(kStartup,10,true))return false;
    const auto* code=reinterpret_cast<const unsigned char*>(kStartup);
    // mov dword ptr [CStreaming::ms_memoryAvailable], imm32. OLA changes imm32.
    return code[0]==0xC7 && code[1]==0x05 && Read32(kStartup+2)==kBudget;
}
bool ProtectBudgetReads(std::ostream& log){
    if(budgetReadCount)return true;
    if(!Readable(kMakeSpaceFor,kMakeSpaceForEnd-kMakeSpaceFor,true))return false;
    const auto* code=reinterpret_cast<const unsigned char*>(kMakeSpaceFor);
    const bool hoodlum=code[0]==0xE9 && Read32(kMakeSpaceFor+1)==0xFFFF56C6u;
    if(hoodlum){
        constexpr uintptr_t stub=0x4037EB;
        constexpr unsigned char expected[]={
            0x9C,0xA1,0x58,0x07,0x56,0x01,0x51,0x8B,0x0D,0x7C,0xF8,0x56,
            0x01,0xD3,0xC0,0x59,0x8B,0x00,0x9D,0xE9,0x22,0xA9,0x00,0x00};
        if(!Readable(stub,sizeof(expected),true) ||
           std::memcmp(reinterpret_cast<const void*>(stub),expected,sizeof(expected))!=0)
            return false;
    }
    // Only the exact original HOODLUM redirect is supported. Foreign detours
    // are neither followed nor overwritten.
    if((code[0]==0xE9&&!hoodlum) || code[0]==0xEB || code[0]==0xCC ||
       code[0]==0xC3 || (code[0]==0xFF && code[1]==0x25))return false;
    std::array<BudgetRead,6> candidates{};
    unsigned count=0;
    const auto target=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&ownedBudget));
    for(uintptr_t p=kMakeSpaceFor+1;p+4<=kMakeSpaceForEnd;++p){
        if(Read32(p)!=kBudget)continue;
        const auto* operand=reinterpret_cast<const unsigned char*>(p);
        // Accept only the original 32-bit absolute MOV read encodings:
        // A1 moffs32, or 8B /r with mod=00,r/m=101. Never redirect a store.
        const bool movEax=operand[-1]==0xA1;
        const bool movReg=p>=kMakeSpaceFor+2 && operand[-2]==0x8B &&
                          (operand[-1]&0xC7)==0x05;
        if((!movEax&&!movReg) || count==candidates.size())return false;
        candidates[count++]={p,static_cast<uint32_t>(kBudget),target};
        p+=3;
    }
    // Redirect every matching read, including the behind-camera fallback.
    // Unexpected references are rejected above before any code is changed.
    if(!count)return false;
    if(hoodlum){
        // Two direct MOV reads remain in the canonical body; the third is
        // displaced into the exact native stub verified above.
        if(count!=2)return false;
        const auto hook=reinterpret_cast<uintptr_t>(&OwnedBudgetEntry);
        candidates[count++]={kMakeSpaceFor+1,Read32(kMakeSpaceFor+1),
                             static_cast<uint32_t>(hook-(kMakeSpaceFor+5))};
    }
    // All operands are in this one small function. Obtain write access before
    // changing any of them, so a protection failure leaves no partial install.
    DWORD previous=0;
    if(!VirtualProtect(reinterpret_cast<void*>(kMakeSpaceFor),
                       kMakeSpaceForEnd-kMakeSpaceFor,PAGE_EXECUTE_READWRITE,&previous))
        return false;
    for(unsigned i=0;i<count;++i)
        std::memcpy(reinterpret_cast<void*>(candidates[i].operand),
                    &candidates[i].replacement,sizeof(uint32_t));
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(kMakeSpaceFor),
                          kMakeSpaceForEnd-kMakeSpaceFor);
    DWORD ignored=0;
    VirtualProtect(reinterpret_cast<void*>(kMakeSpaceFor),
                   kMakeSpaceForEnd-kMakeSpaceFor,previous,&ignored);
    budgetReads=candidates;
    budgetReadCount=count;
    log<<"ASC BUDGET PROTECTION: redirected "<<count
       <<" native MakeSpaceFor reads; layout="<<(hoodlum?"verified HOODLUM":"direct")
       <<"; writes to 0x8A5A80 cannot change GTA's effective budget.\n";
    return true;
}
}

namespace streaming_budget {
uint32_t Effective() noexcept {
    return budgetReadCount ? ownedBudget : CStreaming::ms_memoryAvailable;
}
}

void StreamingMemory::Configure(asc::memory::Settings settings,std::ostream& log){
    settings_=settings;
    desiredBudgetBytes_=0;
    vaProbeActive_=vaProbeAttempted_=haveVaProbe_=false;
    largestFreeVaBytes_=0;
    const auto* base=reinterpret_cast<const unsigned char*>(GetModuleHandleA(nullptr));
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    laa_=(nt->FileHeader.Characteristics&IMAGE_FILE_LARGE_ADDRESS_AWARE)!=0;
    log<<"Streaming memory: "<<(settings.enabled?"managed":"vanilla")
       <<", EXE LAA="<<(laa_?"yes":"no")
       <<", configured MiB="<<settings.limitMiB<<" (0=no user cap)"
       <<", OLA budget override="<<(settings.overrideOla?"ASC":"OLA")
       <<", address reserve MiB="<<settings.reserveMiB<<'\n';
    // Startup must not wait for ms_bIsInitialised, a player, the scheduler,
    // model-table binding, or successful RequestModel hook installation.
    MEMORYSTATUSEX memory{};
    memory.dwLength=sizeof(memory);
    Update(GetTickCount(),GlobalMemoryStatusEx(&memory)?&memory:nullptr,log);
    log.flush();
}
void StreamingMemory::ProbeAddressSpace(uint32_t now,const MEMORYSTATUSEX* memory,std::ostream& log){
    // Aggregate free VA can hide fragmentation. Spread this diagnostic scan
    // across scheduler ticks instead of walking every region in one frame.
    if(!vaProbeActive_){
        if(vaProbeAttempted_&&uint32_t(now-lastVaProbe_)<10000u)return;
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        vaProbeNext_=reinterpret_cast<uintptr_t>(system.lpMinimumApplicationAddress);
        vaProbeMaximum_=reinterpret_cast<uintptr_t>(system.lpMaximumApplicationAddress);
        vaProbeLargest_=0;
        vaProbeActive_=vaProbeAttempted_=true;
    }
    static const int64_t frequency=[] {
        LARGE_INTEGER value{};
        return QueryPerformanceFrequency(&value)?value.QuadPart:int64_t(0);
    }();
    LARGE_INTEGER started{};
    QueryPerformanceCounter(&started);
    bool failed=false;
    for(unsigned regions=0;regions<256&&vaProbeNext_<vaProbeMaximum_;++regions){
        if(regions && (regions&15u)==0 && frequency>0){
            LARGE_INTEGER current{};
            QueryPerformanceCounter(&current);
            if(current.QuadPart>=started.QuadPart &&
               double(current.QuadPart-started.QuadPart)*1000000.0/
                   double(frequency)>=500.0)break;
        }
        MEMORY_BASIC_INFORMATION region{};
        if(VirtualQuery(reinterpret_cast<const void*>(vaProbeNext_),&region,
                        sizeof(region))!=sizeof(region)){
            failed=true;break;
        }
        const auto begin=reinterpret_cast<uintptr_t>(region.BaseAddress);
        if(!region.RegionSize || begin>UINTPTR_MAX-region.RegionSize ||
           begin+region.RegionSize<=vaProbeNext_){failed=true;break;}
        if(region.State==MEM_FREE)
            vaProbeLargest_=std::max<uint64_t>(vaProbeLargest_,region.RegionSize);
        vaProbeNext_=begin+region.RegionSize;
    }
    if(!failed&&vaProbeNext_<vaProbeMaximum_)return;
    vaProbeActive_=false;
    haveVaProbe_=!failed;
    largestFreeVaBytes_=failed?0:vaProbeLargest_;
    lastVaProbe_=now;
    log<<"ASC VA: largest contiguous free MiB="
       <<largestFreeVaBytes_/asc::memory::MiB
       <<", total free MiB="<<(memory?memory->ullAvailVirtual/asc::memory::MiB:0)
       <<", complete="<<(haveVaProbe_?1:0)<<"\n";
}

void StreamingMemory::Update(uint32_t now,const MEMORYSTATUSEX* memory,std::ostream& log){
    // OLA controls table relocation and limits. The user may explicitly
    // elect ASC to own only CStreaming::ms_memoryAvailable. OLA usually
    // patches the initialization immediate. ASC applies that immediate before
    // streaming starts, and keeps an independent scalar for native budget reads.
    ProbeAddressSpace(now,memory,log);
    // No complete initial sample (or a failed query) cannot authorize
    // speculation. Keep the previous complete result during later sweeps.
    const bool contiguousCritical=!haveVaProbe_ ||
        largestFreeVaBytes_<256ull*asc::memory::MiB;
    const bool ola=ola_bindings::IsOlaLoaded();
    if(ola && !settings_.overrideOla){
        if(settings_.enabled && !externalOwnerLogged_){
            log<<"OLA native budget ownership selected; ASC budget writes disabled.\n";
            log.flush();
            externalOwnerLogged_=true;
        }
        if(memory) lowAddressSpace_=contiguousCritical || memory->ullAvailVirtual<=
            uint64_t(settings_.reserveMiB)*asc::memory::MiB;
        return;
    }
    if(!settings_.enabled)return;
    // Fail closed for our speculation; leave the native budget untouched if
    // Windows cannot provide a valid address-space measurement.
    if(!memory||!memory->ullTotalVirtual){lowAddressSpace_=true;return;}
    lowAddressSpace_=contiguousCritical || memory->ullAvailVirtual<=
        uint64_t(settings_.reserveMiB)*asc::memory::MiB;
    // Recompute the safe budget once per second. The fast frame-boundary
    // Reassert() method enforces this cached value between computations.
    const uint32_t interval=1000u;
    if(haveUpdate_&&uint32_t(now-lastUpdate_)<interval)return;
    const auto result=asc::memory::Calculate(settings_,laa_,memory->ullTotalVirtual,
        memory->ullAvailVirtual,CStreaming::ms_memoryUsed);
    if(!result.budgetBytes){lowAddressSpace_=true;return;}
    const bool first=!haveUpdate_;
    haveUpdate_=true;lastUpdate_=now;
    desiredBudgetBytes_=result.budgetBytes;
    ownedBudget=desiredBudgetBytes_;
    if(settings_.overrideOla && !ProtectBudgetReads(log) && !protectionFailureLogged_){
        protectionFailureLogged_=true;
        log<<"ASC BUDGET PROTECTION UNAVAILABLE: unsupported/modified MakeSpaceFor; "
              "only startup and frame-boundary enforcement available.\n";
        log.flush();
    }
    // Match OLA's startup application point, before CStreaming::Init runs.
    // Recheck the opcode before each write; never overwrite a foreign detour.
    if(StartupInstruction()){
        if(!startupPatched_)originalStartupBudget_=Read32(kStartup+6);
        if(Read32(kStartup+6)==desiredBudgetBytes_ ||
           WriteCode32(kStartup+6,desiredBudgetBytes_)){
            startupPatched_=true;
            lastStartupBudget_=desiredBudgetBytes_;
        }
    }
    // This is a budget, not a preallocation or bulk load. Vanilla streaming
    // retains responsibility for loading and ordinary eviction.
    const bool replaced=CStreaming::ms_memoryAvailable!=result.budgetBytes;
    if(replaced) {
        CStreaming::ms_memoryAvailable=result.budgetBytes;
        ++budgetOverrides_;
    }
    if(ola && (first || (replaced && now-lastOwnershipLog_>=15000))){
        lastOwnershipLog_=now;
        log<<"ASC BUDGET OWNER: OLA loaded; enforced native budget MiB="
           <<result.budgetBytes/asc::memory::MiB
           <<", writes="<<budgetOverrides_<<'\n';
        log.flush();
    }
    if(first){
        log<<"Streaming memory applied (startup/loading/runtime): process VA MiB="
           <<result.processLimitBytes/asc::memory::MiB
           <<", free VA MiB="<<memory->ullAvailVirtual/asc::memory::MiB
           <<", effective stream MiB="<<result.budgetBytes/asc::memory::MiB
           <<", startup immediate="<<(startupPatched_?"applied":"unavailable")<<'\n';
        log.flush();
    }
}

void StreamingMemory::Reassert(uint32_t now,std::ostream& log,const char* phase){
    if(!settings_.enabled || !desiredBudgetBytes_) return;
    // Respect the explicit ownership choice, even when OLA loaded late.
    if(ola_bindings::IsOlaLoaded() && !settings_.overrideOla) return;
    // This field is a native GTA budget; OLA owns its table/pool expansion.
    // The public value is a compatibility mirror. Protected native reads use
    // ownedBudget even if another ASI writes again later in this same frame.
    const uint32_t observed=CStreaming::ms_memoryAvailable;
    if(observed==desiredBudgetBytes_) return;
    CStreaming::ms_memoryAvailable=desiredBudgetBytes_;
    ++budgetOverrides_;
    ++externalBudgetChanges_;
    // Rate-limit diagnostics; a repeated overwrite may be from the game or
    // another ASI. A mismatched field alone does not identify its writer.
    if(externalBudgetChanges_==1 || uint32_t(now-lastForeignLog_)>=10000u){
        lastForeignLog_=now;
        log<<"ASC BUDGET REASSERT: phase="<<phase
           <<", observed MiB="<<observed/asc::memory::MiB
           <<", target MiB="<<desiredBudgetBytes_/asc::memory::MiB
           <<", external changes="<<externalBudgetChanges_
           <<", total writes="<<budgetOverrides_<<'\n';
        log.flush();
    }
}

void StreamingMemory::Shutdown(std::ostream& log){
    bool remaining=false;
    for(unsigned i=0;i<budgetReadCount;++i){
        const auto operand=budgetReads[i].operand;
        if(Read32(operand)==budgetReads[i].replacement &&
           !WriteCode32(operand,budgetReads[i].original))remaining=true;
    }
    if(!remaining)budgetReadCount=0;
    if(startupPatched_ && StartupInstruction() &&
       Read32(kStartup+6)==lastStartupBudget_)
        WriteCode32(kStartup+6,originalStartupBudget_);
    startupPatched_=false;
    settings_.enabled=false;
    log<<"ASC budget ownership stopped"<<(remaining?"; native read restore failed":"")<<".\n";
}
