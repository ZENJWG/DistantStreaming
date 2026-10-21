#include "OffscreenCleanupWin.h"
#include "asc/CameraReadiness.h"
#include "Hooks.h"
#include "AsyncAssetCacheWin.h"
#include "asc/NativeResidencyPolicy.h"
#include <windows.h>
#include <CPools.h>
#include <CStreaming.h>
#include <CStreamingInfo.h>
#include <CModelInfo.h>
#include <CTxdStore.h>

namespace {
bool executable(uintptr_t address){
    MEMORY_BASIC_INFORMATION m{};
    if(!VirtualQuery(reinterpret_cast<void*>(address),&m,sizeof(m))||m.State!=MEM_COMMIT
       ||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const auto p=m.Protect&255;
    return p==PAGE_EXECUTE||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
}
bool valid(int id){
    return id>=0&&id<int(asc::kVanillaModelInfoCountCap)&&id<CModelInfo::ms_modelInfoCount
        &&CModelInfo::ms_modelInfoPtrs&&CModelInfo::ms_modelInfoPtrs[id];
}
constexpr unsigned protectedFlags=GAME_REQUIRED|MISSION_REQUIRED|KEEP_IN_MEMORY|LOADING_SCENE|PRIORITY_REQUEST;
template<class Pool> unsigned poolSize(Pool* pool){
    return pool&&pool->m_pObjects&&pool->m_byteMap&&pool->m_nSize>0&&pool->m_nSize<=65536?
        unsigned(pool->m_nSize):0;
}
}
void OffscreenCleanup::Configure(asc::offscreen::Settings cfg,std::ostream& log){
    cfg_=cfg;
    available_=executable(0x534030);removeAvailable_=executable(0x4089A0);
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);frequency_=frequency.QuadPart;
    log<<"Offscreen cleanup: enabled="<<cfg.enabled<<", instance delete="<<available_
       <<", model unload="<<(cfg.unloadModels&&removeAvailable_)
       <<", soft budget Us="<<cfg.timeBudgetUs<<", grace Ms="<<cfg.graceMs
       <<", helper-first native trim="<<cfg.helperFirstTrim
       <<", start/stop headroom MiB="<<cfg.trimBeginHeadroomMiB
       <<"/"<<cfg.trimStopHeadroomMiB
       <<", min helper source cache MiB="<<cfg.trimMinHelperCachedMiB
       <<", stable ms="<<cfg.trimStableMs
       <<" (native GTA RemoveModel only; no remote RW resources)\n";
}
void OffscreenCleanup::Reset(){
    for(auto& stamp:buildingStamps_)stamp.offscreen=false;
    for(auto& stamp:dummyStamps_)stamp.offscreen=false;
    cursor_=0;haveDirection_=turnHold_=false;
    pendingModels_.Clear();
    eligibleSince_.fill(0);
    trimCursor_=0;lastTrimAt_=0;trimActive_=false;
}
bool OffscreenCleanup::TryRemoveUnreferenced(int id,uint32_t now,bool trimOrigin){
    if(!cfg_.unloadModels||!removeAvailable_||!valid(id))return false;
    auto* info=CModelInfo::ms_modelInfoPtrs[id];
    if(CModelInfo::IsVehicleModel(id)||CModelInfo::IsPedModel(id))return false;
    const auto& stream=CStreaming::ms_aInfoForModel[id];
    if(!asc::native_residency::UnloadCandidate(info->m_nRefCount,
        stream.m_nLoadState==LOADSTATE_LOADED,stream.m_nFlags&protectedFlags,
        hooks::RequestedRecently(id,now,
            trimOrigin?std::max(cfg_.requestQuietMs,cfg_.trimStableMs):cfg_.requestQuietMs),
        stream.m_nCdSize,cfg_.maxUnloadSectors))
        return false;
    const int txd=info->m_nTxdIndex;
    const uint64_t before=CStreaming::ms_memoryUsed;
    CStreaming::RemoveModel(id);
    ++counters_.models;
    if(cfg_.unloadTextures&&txd>=0&&txd<5000){
        auto* pool=CTxdStore::ms_pTxdPool;
        // Retain native/mission references and every dictionary used by a
        // requested model. Releasing source bytes never releases live textures.
        if(pool&&txd<int(poolSize(pool))&&pool->GetAt(txd)&&
           pool->GetAt(txd)->m_wRefsCount==0){
            const int txdId=int(asc::kVanillaModelInfoCountCap)+txd;
            const auto& texture=CStreaming::ms_aInfoForModel[txdId];
            if(texture.m_nLoadState==LOADSTATE_LOADED &&
               !(texture.m_nFlags&protectedFlags) &&
               texture.m_nCdSize>0 &&
               texture.m_nCdSize<=cfg_.maxUnloadSectors &&
               !hooks::RequestedRecently(txdId,now,cfg_.requestQuietMs) &&
               !CStreaming::AreTexturesUsedByRequestedModels(txd)){
                CStreaming::RemoveModel(txdId);
                ++counters_.textures;
            }
        }
    }
    const uint64_t after=CStreaming::ms_memoryUsed;
    if(before>after){
        counters_.streamBytes+=before-after;
        if(trimOrigin)counters_.nativeTrimBytes+=before-after;
    }
    if(trimOrigin)++counters_.nativeTrimEvicted;
    return true;
}
void OffscreenCleanup::Frame(asc::Snapshot s,const asc::Config& core){
    if(!cfg_.enabled||!available_||frequency_<=0)return;
    if(!s.gameplaySafe||!s.playerValid||!s.cameraValid||s.area!=0||!CStreaming::ms_aInfoForModel){
        if(haveDirection_)Reset();
        return;
    }
    const float norm=std::sqrt(s.cameraForward.x*s.cameraForward.x+s.cameraForward.y*s.cameraForward.y+s.cameraForward.z*s.cameraForward.z);
    if(!std::isfinite(norm)||norm<.1f)return;
    const asc::Vec3 dir{s.cameraForward.x/norm,s.cameraForward.y/norm,s.cameraForward.z/norm};
    if(haveDirection_&&(dir.x*direction_.x+dir.y*direction_.y+dir.z*direction_.z)<.98f){
        turnAt_=s.nowMs;turnHold_=true;
    }
    direction_=dir;haveDirection_=true;
    if(turnHold_&&uint32_t(s.nowMs-turnAt_)<cfg_.graceMs)return;
    turnHold_=false;
    auto* buildings=CPools::ms_pBuildingPool;
    auto* dummies=CPools::ms_pDummyPool;
    const unsigned nb=poolSize(buildings),nd=poolSize(dummies),total=nb+nd;
    if(!total)return;
    if(buildingPool_!=reinterpret_cast<uintptr_t>(buildings)||buildingStamps_.size()!=nb){
        buildingStamps_.assign(nb,{});buildingPool_=reinterpret_cast<uintptr_t>(buildings);
    }
    if(dummyPool_!=reinterpret_cast<uintptr_t>(dummies)||dummyStamps_.size()!=nd){
        dummyStamps_.assign(nd,{});dummyPool_=reinterpret_cast<uintptr_t>(dummies);
    }
    cursor_%=total;
    LARGE_INTEGER start{},clock{};QueryPerformanceCounter(&start);
    auto elapsed=[&]{QueryPerformanceCounter(&clock);return uint64_t(
        double(clock.QuadPart-start.QuadPart)*1000000.0/double(frequency_));};
    const asc::native_residency::Limits limits{
        cfg_.trimBeginHeadroomMiB,cfg_.trimStopHeadroomMiB,
        cfg_.trimMinHelperCachedMiB,cfg_.trimMaxBacklog};
    const bool helperReady=cfg_.helperFirstTrim &&
        asc::external_cache::ReadyForNativeReclaim(cfg_.trimMinHelperCachedMiB);
    const bool wasTrimming=trimActive_;
    trimActive_=cfg_.helperFirstTrim &&
        asc::native_residency::Update(trimActive_,limits,helperReady,
            helperReady?uint64_t(cfg_.trimMinHelperCachedMiB)*asc::native_residency::MiB:0,
            s.streamingUsed,s.streamingBudget,s.gameRequests);
    if(wasTrimming&&!trimActive_)eligibleSince_.fill(0);
    if(trimActive_)++counters_.nativeTrimActiveFrames;
    const bool pressure=s.addressSpaceLow||trimActive_||
        (s.streamingBudget&&s.streamingUsed>=s.streamingBudget*core.memoryThrottlePercent/100);
    unsigned deleteLimit=pressure?cfg_.maxPressureDeletes:cfg_.maxDeletesPerFrame;
    if(s.recentFrameMs>=core.targetFrameMs)deleteLimit=std::min(deleteLimit,1u);
    unsigned deleted=0,unloaded=0;
    for(unsigned n=0;deleted<deleteLimit&&n<std::min(total,cfg_.maxScanPerFrame);++n){
        if((n%16)==0&&elapsed()>=cfg_.timeBudgetUs){++counters_.budgetStops;break;}
        const unsigned index=cursor_;cursor_=(cursor_+1)%total;++counters_.scanned;
        CEntity* e=index<nb?static_cast<CEntity*>(buildings->GetAt(int(index))):
            static_cast<CEntity*>(dummies->GetAt(int(index-nb)));
        auto& stamp=index<nb?buildingStamps_[index]:dummyStamps_[index-nb];
        if(!e||!e->m_pRwObject){stamp.offscreen=false;continue;}
        const int id=e->m_nModelIndex;
        if(!valid(id)){stamp.offscreen=false;continue;}
        auto* info=CModelInfo::ms_modelInfoPtrs[id];
        if(CModelInfo::IsVehicleModel(id)||CModelInfo::IsPedModel(id)){stamp.offscreen=false;continue;}
        const auto& stream=CStreaming::ms_aInfoForModel[id];
        const auto p=info->m_pColModel?e->GetBoundCentre():e->GetPosition();
        const float x=p.x-s.player.x,y=p.y-s.player.y,z=p.z-s.player.z;
        const float radius=info->m_pColModel?info->m_pColModel->m_boundSphere.m_fRadius:0;
        const float keep=cfg_.keepNearUnits+std::max(0.0f,radius);
        const bool nearPlayer=!std::isfinite(x*x+y*y+z*z)||!std::isfinite(radius)||x*x+y*y+z*z<=keep*keep;
        const bool pinned=e->bStreamingDontDelete||e->bDontStream||e->bRemoveFromWorld
            ||e->bIsTempBuilding||e->bIsProcObject||e->m_nAreaCode!=0||(stream.m_nFlags&protectedFlags);
        const bool onscreen=!info->m_pColModel||e->GetIsOnScreen()
            ||asc::camera::InView({p.x,p.y,p.z},s.camera,dir,100000,0.08716f); // 85-degree guard cone
        const asc::offscreen::Protection protection{onscreen,nearPlayer,e->bImBeingRendered,pinned,
            e->m_nType==ENTITY_TYPE_BUILDING||e->m_nType==ENTITY_TYPE_DUMMY,true,true};
        const unsigned generation=index<nb?buildings->m_byteMap[index].IntValue():dummies->m_byteMap[index-nb].IntValue();
        const bool ready=stamp.Observe(reinterpret_cast<uintptr_t>(e->m_pRwObject),id,generation,
            asc::offscreen::CanDelete(protection),s.nowMs,cfg_.graceMs);
        if(!ready)continue;
        if(elapsed()>=cfg_.timeBudgetUs){++counters_.budgetStops;break;}
        e->DeleteRwObject();++deleted;++counters_.instances;stamp.offscreen=false;
        if(cfg_.unloadModels)pendingModels_.Add(id);
        if(deleted>=deleteLimit)break;
    }
    // Keep deferred zero-reference candidates until the recent-request guard
    // expires; otherwise deleting the instance first would lose the candidate.
    const unsigned checks=std::min(pendingModels_.Size(),32u);
    for(unsigned n=0;n<checks&&unloaded<cfg_.maxUnloadsPerFrame;++n){
        if(!cfg_.unloadModels||!removeAvailable_||
           (cfg_.helperFirstTrim&&!trimActive_)||elapsed()>=cfg_.timeBudgetUs)break;
        const int id=pendingModels_.Pop();
        if(!valid(id))continue;
        // Earlier offscreen deletion is the ownership proof. Keep the engine's
        // own resource-removal and dependency rules, never free source buffers.
        if(hooks::RequestedRecently(id,s.nowMs,cfg_.requestQuietMs)){
            pendingModels_.Add(id);
            continue;
        }
        if(TryRemoveUnreferenced(id,s.nowMs,trimActive_))++unloaded;
    }
    // Secondary sweep: reclaim only unloaded-from-scene DFF resources that
    // are already unreferenced and have stayed idle through two separate passes.
    // This is the real x86 residency reduction. The x64 helper remains an
    // opportunistic source cache for future native re-requests, not a RW heap.
    if(trimActive_&&removeAvailable_&&unloaded<cfg_.maxUnloadsPerFrame &&
       uint32_t(s.nowMs-lastTrimAt_)>=cfg_.trimIntervalMs &&
       elapsed()<cfg_.timeBudgetUs){
        lastTrimAt_=s.nowMs;
        const unsigned nmax=std::min(cfg_.trimScanPerPass,
            unsigned(asc::kVanillaModelInfoCountCap));
        for(unsigned n=0;n<nmax;++n){
            if((n&15u)==0&&elapsed()>=cfg_.timeBudgetUs){
                ++counters_.budgetStops;break;
            }
            const unsigned id=trimCursor_;
            trimCursor_=(trimCursor_+1)%unsigned(eligibleSince_.size());
            ++counters_.nativeTrimScanned;
            if(!valid(int(id))){
                eligibleSince_[id]=0;continue;
            }
            const auto* info=CModelInfo::ms_modelInfoPtrs[id];
            const auto& stream=CStreaming::ms_aInfoForModel[id];
            const bool unused=!CModelInfo::IsVehicleModel(int(id)) &&
                !CModelInfo::IsPedModel(int(id)) &&
                asc::native_residency::UnloadCandidate(
                    info->m_nRefCount,stream.m_nLoadState==LOADSTATE_LOADED,
                    stream.m_nFlags&protectedFlags,
                    hooks::RequestedRecently(int(id),s.nowMs,
                        std::max(cfg_.requestQuietMs,cfg_.trimStableMs)),
                    stream.m_nCdSize,cfg_.maxUnloadSectors);
            if(!unused){eligibleSince_[id]=0;continue;}
            ++counters_.nativeTrimCandidates;
            if(!eligibleSince_[id]){
                eligibleSince_[id]=s.nowMs?s.nowMs:1;
                continue;
            }
            if(asc::native_residency::Stable(eligibleSince_[id],s.nowMs,
                                             cfg_.trimStableMs) &&
               unloaded<cfg_.maxUnloadsPerFrame && elapsed()<cfg_.timeBudgetUs &&
               TryRemoveUnreferenced(int(id),s.nowMs,true)){
                eligibleSince_[id]=0;
                ++unloaded;
            }
        }
    }
    const uint64_t us=elapsed();counters_.totalUs+=us;counters_.peakUs=std::max(counters_.peakUs,us);
}
