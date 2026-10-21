#include "OlaBindingsWin.h"
#include "WorldListSafety.h"
#include "asc/OlaBindings.h"
#include <windows.h>
#include <CStreaming.h>
#include <CStreamingInfo.h>
#include <CModelInfo.h>
#include <cstdint>
#include <cstring>

namespace {
constexpr uintptr_t kEntityModelLookup = 0x546680; // GTA SA 1.0 US
constexpr unsigned kProbeMaxIds=512;

bool ProbeModels(CBaseModelInfo* const* table,int count) {
    if (!asc::ola::ValidModelCount(count) ||
        !asc::world::Readable(table,sizeof(*table)*kProbeMaxIds)) return false;
    unsigned plausible=0;
    // Do not dereference the candidate table until its entire probe span is
    // readable. Probe several real model objects and their vtables.
    for(unsigned id=0;id<kProbeMaxIds;++id) {
        const auto* info=table[id];
        if (!info || !asc::world::Readable(info,sizeof(void*))) continue;
        const void* vtable=nullptr;
        std::memcpy(&vtable,info,sizeof(vtable));
        if(asc::world::Readable(vtable,sizeof(void*))) {
            if (++plausible>=4) return true;
        }
    }
    return false;
}
}
namespace ola_bindings {
bool StillBound(){
    const int count=CModelInfo::ms_modelInfoCount;
    if(!asc::ola::ValidModelCount(count))return false;
    auto* live=CStreamingInfo::ms_pArrayBase;
    auto* model=CModelInfo::ms_modelInfoPtrs;
    if(!live || live!=CStreaming::ms_aInfoForModel ||
       !asc::world::Readable(live,sizeof(CStreamingInfo)) ||
       !model ||
       !asc::world::Readable(model,sizeof(*model)*kProbeMaxIds))
        return false;
    if(asc::world::Readable(reinterpret_cast<void*>(kEntityModelLookup),7)){
        uint8_t ins[7]{};
        std::memcpy(ins,reinterpret_cast<const void*>(kEntityModelLookup),7);
        const uintptr_t referenced=asc::ola::DecodeEntityModelTable(ins,7);
        if(referenced && referenced!=reinterpret_cast<uintptr_t>(model))
            return false;
    }
    return true;
}
bool IsOlaLoaded() noexcept {
    return GetModuleHandleA("III.VC.SA.LimitAdjuster.asi")!=nullptr ||
           GetModuleHandleA("OpenLimitAdjuster.asi")!=nullptr;
}
bool Bind(std::ostream&log) {
    auto* live=CStreamingInfo::ms_pArrayBase;
    auto* cached=CStreaming::ms_aInfoForModel;
    const int count=CModelInfo::ms_modelInfoCount;
    const bool streamOk=asc::world::Readable(live,sizeof(CStreamingInfo));
    auto* current=CModelInfo::ms_modelInfoPtrs;
    const bool oldModelOk=ProbeModels(current,count);

    // Look at a second, INDEPENDENT game instruction which indexes the
    // model table when CEntity asks for model info. This displacement is
    // maintained by limit adjusters that relocate the table. A JMP or
    // unrecognised opcode is NOT guessed or followed.
    uintptr_t referenced=0;
    if (asc::world::Readable(reinterpret_cast<void*>(kEntityModelLookup),7)) {
        uint8_t instruction[7]{};
        std::memcpy(instruction,reinterpret_cast<void*>(kEntityModelLookup),7);
        referenced=asc::ola::DecodeEntityModelTable(instruction,7);
    }
    auto* modelCandidate=reinterpret_cast<CBaseModelInfo**>(referenced);
    const bool indexedModelOk=referenced && ProbeModels(modelCandidate,count);

    // A readable cached model table is not authoritative if the game's
    // actual lookup uses another base; never restore a stale table.
    auto* resolved=indexedModelOk ? modelCandidate :
        (!referenced && oldModelOk ? current : nullptr);
    const bool modelOk=resolved!=nullptr;

    log<<"OLA bindings: ola_loaded="<<(IsOlaLoaded()?1:0)
       <<", stream_live="<<static_cast<const void*>(live)
       <<", stream_cached="<<static_cast<const void*>(cached)
       <<", stream_ok="<<streamOk
       <<", model_cached="<<static_cast<const void*>(current)
       <<", model_lookup_operand=0x"<<std::hex<<referenced<<std::dec
       <<", lookup_ok="<<indexedModelOk
       <<", model_ok="<<modelOk
       <<", model_count="<<count<<'\n';

    if(!streamOk || !modelOk){
        log<<"OLA bindings: FAIL CLOSED. No speculative model requests or "
              "world traversal; check lookup opcode, OLA version and "
              "executable variant. No unsafe pointer guesses.\n";
        log.flush();
        return false;
    }
    // These are Plugin-SDK DLL-local snapshot variables, NOT any GTA game
    // globals. We do not patch OLA, native executable code, or its tables.
    CStreaming::ms_aInfoForModel=live;
    CModelInfo::ms_modelInfoPtrs=resolved;
    log<<"OLA bindings: ACTIVE; runtime streaming and model arrays rebound "
          "for this ASI. Predictive-ID ceiling remains 20000.\n";
    log.flush();
    return true;
}
}
