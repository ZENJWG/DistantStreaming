#pragma once
#include "asc/StreamingMemory.h"
#include <windows.h>
#include <ostream>

namespace streaming_budget {
// The budget actually consumed by GTA, even if another ASI changed the public field.
uint32_t Effective() noexcept;
}

class StreamingMemory {
public:
    // Called at RenderWare startup after target validation, outside loader lock.
    void Configure(asc::memory::Settings settings,std::ostream& log);
    // Keep the protected budget across loading/new-game transitions.
    void StreamingStopped(){haveUpdate_=false;}
    void Shutdown(std::ostream& log);
    void Update(uint32_t now,const MEMORYSTATUSEX* memory,std::ostream& log);
    // Reassert the last verified budget at a game frame boundary. No allocations,
    // Win32 memory queries, OLA patches, or game-model dereferences.
    void Reassert(uint32_t now,std::ostream& log,const char* phase);
    bool LargeAddressAware()const{return laa_;}
    bool LowAddressSpace()const{return lowAddressSpace_;}
    uint64_t LargestFreeVaBytes()const{return largestFreeVaBytes_;}
private:
    asc::memory::Settings settings_{};
    bool laa_=false,haveUpdate_=false,lowAddressSpace_=false,externalOwnerLogged_=false;
    bool vaProbeActive_=false,vaProbeAttempted_=false,haveVaProbe_=false;
    uint32_t lastUpdate_=0,lastOwnershipLog_=0,lastVaProbe_=0;
    uint64_t largestFreeVaBytes_=0;
    uint64_t vaProbeLargest_=0;
    uintptr_t vaProbeNext_=0,vaProbeMaximum_=0;
    void ProbeAddressSpace(uint32_t now,const MEMORYSTATUSEX* memory,std::ostream& log);
    uint32_t desiredBudgetBytes_=0;
    uint32_t lastForeignLog_=0;
    uint64_t budgetOverrides_=0;
    uint64_t externalBudgetChanges_=0;
    bool startupPatched_=false;
    uint32_t originalStartupBudget_=0,lastStartupBudget_=0;
    bool protectionFailureLogged_=false;
};
