#include "CameraReadinessWin.h"
#include "Hooks.h"
#include "WorldListSafety.h"
#include <windows.h>
#include <CWorld.h>
#include <CEntity.h>
#include <CStreaming.h>
#include <CStreamingInfo.h>
#include <CModelInfo.h>
#include <CClock.h>
#include <CTxdStore.h>

namespace {
bool executable(uintptr_t address){
    MEMORY_BASIC_INFORMATION m{};
    if(!VirtualQuery(reinterpret_cast<const void*>(address),&m,sizeof(m))
       ||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const DWORD p=m.Protect&255;
    return p==PAGE_EXECUTE||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
}
bool eligible(CEntity* e,const asc::Snapshot& s,const asc::camera::Settings& cfg,float cosine){
    if(!e||!e->bIsVisible||e->bDontStream||e->bRemoveFromWorld||e->m_nAreaCode!=0
       ||(e->m_nType!=ENTITY_TYPE_BUILDING&&e->m_nType!=ENTITY_TYPE_DUMMY))return false;
    const int id=e->m_nModelIndex;
    if(id<0||id>=int(asc::kVanillaModelInfoCountCap)||id>=CModelInfo::ms_modelInfoCount
       ||!CModelInfo::ms_modelInfoPtrs||!CModelInfo::ms_modelInfoPtrs[id])return false;
    auto* info=CModelInfo::ms_modelInfoPtrs[id];
    if(!info->m_pColModel||CModelInfo::IsVehicleModel(id)||CModelInfo::IsPedModel(id))return false;
    if(const auto* time=info->GetTimeInfo()){
        const unsigned on=time->m_nTimeOn,off=time->m_nTimeOff,h=CClock::ms_nGameClockHours;
        if(on<24&&off<24&&on!=off&&!(on<off?(h>=on&&h<off):(h>=on||h<off)))return false;
    }
    const auto& p=e->GetPosition();
    if(!asc::camera::InView({p.x,p.y,p.z},s.camera,s.cameraForward,cfg.range,cosine))return false;
    // Use the game's bounding-volume/frustum test as well as the forward cone.
    return e->GetIsOnScreen();
}
template<class Callback>
void scan(const std::vector<asc::Cell>& cells,unsigned maxNodes,
          unsigned maxUs,int64_t qpcFrequency,Callback&& callback){
    // This is a *supplemental* camera readiness scan, not the renderer.
    // Stop expensive world-list walking without changing visibility lists.
    LARGE_INTEGER started{};
    QueryPerformanceCounter(&started);
    unsigned examined=0;
    bool budgetExceeded=false;
    unsigned nodes=maxNodes;
    auto walk=[&](auto* node,unsigned& left){
        for(unsigned local=0;node&&left&&!budgetExceeded&&local<512;
            ++local,--left) {
            if((examined++ & 15u)==0 && qpcFrequency>0) {
                LARGE_INTEGER current{};
                QueryPerformanceCounter(&current);
                if(current.QuadPart>=started.QuadPart &&
                   uint64_t(current.QuadPart-started.QuadPart)*1000000ull/
                       uint64_t(qpcFrequency)>=maxUs) {
                    budgetExceeded=true;
                    break;
                }
            }
            if(!asc::world::Readable(node,sizeof(*node))){
                ++asc::world::rejectedNodes;
                break;
            }
            auto* next=node->pNext;
            auto* entity=static_cast<CEntity*>(node->pItem);
            node=next;
            if(!asc::world::Readable(entity,sizeof(CEntity))){
                ++asc::world::rejectedEntities;
                continue;
            }
            if(!callback(entity)){
                budgetExceeded=true;
                break;
            }
        }
    };
    if(CWorld::ms_aSectors)for(const auto& c:cells){
        if(!nodes||budgetExceeded)break;
        if(c.x<0||c.x>=120||c.y<0||c.y>=120)continue;
        auto* sector=CWorld::GetSector(c.x,c.y);
        walk(sector->m_buildingList.GetNode(),nodes);
        walk(sector->m_dummyList.GetNode(),nodes);
    }
    std::bitset<900> seen;
    unsigned lodNodes=std::min(1024u,std::max(64u,maxNodes/4u));
    if(CWorld::ms_aLodPtrLists)for(const auto& c:cells){
        if(!lodNodes||budgetExceeded)break;
        if(c.x<0||c.x>=120||c.y<0||c.y>=120)continue;
        const unsigned x=unsigned(c.x)/4,y=unsigned(c.y)/4,index=y*30+x;
        if(seen.test(index))continue;
        seen.set(index);
        walk(CWorld::GetLodPtrList(int(x),int(y))->GetNode(),lodNodes);
    }
}
struct PluginRequestScope {
    PluginRequestScope(){hooks::EnterPluginRequest();}
    ~PluginRequestScope(){hooks::ExitPluginRequest();}
};
}
void CameraReadiness::Configure(asc::camera::Settings cfg,std::ostream& log){
    cfg_=cfg;targets_.reserve(4096);
    cosine_=std::cos(cfg.halfAngleDegrees*3.14159265f/180.0f);
    canFlush_=executable(0x40EA10);
    canInstance_=executable(0x533D30);
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
    qpcFrequency_=frequency.QuadPart;
    log<<"Camera catch-up: enabled="<<cfg.enabled<<", blocking="<<cfg.blocking
       <<", loader available="<<canFlush_<<", instance available="<<canInstance_
       <<", models/batch="<<cfg.maxModels<<", interval Ms="<<cfg.intervalMs
       <<", scan cells/nodes/us="<<cfg.maxScanCells<<"/"
       <<cfg.maxScanNodes<<"/"<<cfg.maxScanUs<<'\n';
    if(cfg.enabled&&cfg.blocking)
        log<<"Camera blocking mode waits for native priority loading; disk/texture stalls are possible.\n";
}
bool CameraReadiness::Tick(const asc::Snapshot& s,const asc::Config& core,
    asc::motion::RequestBudget* budget,unsigned fastIntervalMs){
    const unsigned interval=fastIntervalMs && (!budget || budget->Available())?
        std::min(cfg_.intervalMs,fastIntervalMs):cfg_.intervalMs;
    if(inTick_||!asc::camera::Allowed(s,core,cfg_)||!CStreaming::ms_aInfoForModel
       ||!cadence_.Due(s.nowMs,interval))return false;
    // Leave room for the predictive route/controller in the same update.
    unsigned cameraAllowance=budget?std::min(s.fastTravel&&!s.fastCamera?2u:4u,
        budget->Available()):~0u;
    const auto takeRequest=[&]{
        if(!cameraAllowance || (budget && !budget->Take()))return false;
        --cameraAllowance;return true;
    };
    // Nonblocking mode shares the normal backlog admission cap. Blocking
    // catch-up must be able to promote visible work already stuck in a queue.
    inTick_=true;
    struct Exit {bool& active;~Exit(){active=false;}} exit{inTick_};
    asc::Vec3 end=s.camera;
    const float n=std::sqrt(s.cameraForward.x*s.cameraForward.x+s.cameraForward.y*s.cameraForward.y);
    if(n>0.1f){end.x+=s.cameraForward.x/n*cfg_.range*.7f;end.y+=s.cameraForward.y/n*cfg_.range*.7f;}
    auto& cells=cells_;
    asc::Controller::SelectCells(
        s.camera,end,cfg_.range*.55f,cfg_.maxScanCells,cells);
    // Spend the traversal budget on front-facing cells before the rear half
    // of the camera capsule. Keep the camera's own cell first for close walls.
    for(auto& cell:cells){
        const float x=-3000+(cell.x+.5f)*50-s.camera.x;
        const float y=-3000+(cell.y+.5f)*50-s.camera.y;
        const bool own=std::abs(x)<=25&&std::abs(y)<=25;
        cell.score+=(own?4.0f:(x*s.cameraForward.x+y*s.cameraForward.y>=0?2.0f:0.0f));
    }
    std::sort(cells.begin(),cells.end(),[](const asc::Cell& a,const asc::Cell& b){return a.score>b.score;});
    targets_.clear();
    {
    asc::world::RegionValidationCache regions;
    asc::world::ScopedRegionCache validation(regions);
    scan(cells,cfg_.maxScanNodes,cfg_.maxScanUs,qpcFrequency_,[&](CEntity* e){
        if(!e||e->m_pRwObject)return true;
        if(!eligible(e,s,cfg_,cosine_))return true;
        const auto& info=CStreaming::ms_aInfoForModel[e->m_nModelIndex];
        if(info.m_nLoadState==LOADSTATE_LOADED||!info.m_nCdSize)return true;
        const auto& p=e->GetPosition();
        const float x=p.x-s.camera.x,y=p.y-s.camera.y,z=p.z-s.camera.z;
        targets_.push_back({e->m_nModelIndex,x*x+y*y+z*z,info.m_nCdSize,false,
            (info.m_nFlags&PRIORITY_REQUEST)!=0});
        return true;
    });
    } // Discard region checks before native requests/loading/instancing.
    const auto batch=asc::camera::Plan(targets_,cfg_);
    {
        PluginRequestScope scope;
        // Promote texture dependencies even when the DFF was already queued.
        // Native RequestModel only recurses on an initially unloaded DFF.
        auto* pool=CTxdStore::ms_pTxdPool;
        std::bitset<5000> requestedTextures;
        if(pool&&pool->m_pObjects&&pool->m_byteMap)
            for(unsigned i=0;i<batch.selected;++i){
                const int id=batch.selectedIds[i];
                const int slot=CModelInfo::ms_modelInfoPtrs[id]->m_nTxdIndex;
                asc::camera::TextureDependencies(slot,[&](int txd){
                    if(txd>=pool->m_nSize)return -2;
                    auto* def=pool->GetAt(txd);
                    return def?int(def->m_wParentIndex):-2;
                },[&](int txd){
                    if(requestedTextures.test(unsigned(txd)))return;
                    requestedTextures.set(unsigned(txd));
                    if(!pool->GetAt(txd)->m_pRwDictionary){
                        const auto& stream=CStreaming::ms_aInfoForModel[
                            int(asc::kVanillaModelInfoCountCap)+txd];
                        if(stream.m_nLoadState==LOADSTATE_LOADED ||
                           (stream.m_nFlags&PRIORITY_REQUEST) ||
                           !asc::camera::MayRequest(cfg_.blocking,
                               stream.m_nLoadState!=LOADSTATE_NOT_LOADED,
                               CStreaming::ms_numModelsRequested,core.backlogLimit))return;
                        const int flags=stream.m_nFlags;
                        if(!takeRequest())return;
                        CStreaming::RequestTxdModel(txd,flags|PRIORITY_REQUEST);
                        ++counters_.requests;
                    }
                });
            }
    }
    asc::camera::Execute(batch,cfg_.blocking&&canFlush_,[&](int id){
        const auto& stream=CStreaming::ms_aInfoForModel[id];
        if(!asc::camera::MayRequest(cfg_.blocking,
            stream.m_nLoadState!=LOADSTATE_NOT_LOADED,
            CStreaming::ms_numModelsRequested,core.backlogLimit))return;
        PluginRequestScope scope;
        // ORs priority into existing queued world work without mission pins.
        const int flags=CStreaming::ms_aInfoForModel[id].m_nFlags;
        if(!takeRequest())return;
        CStreaming::RequestModel(id,flags|PRIORITY_REQUEST);
        ++counters_.requests;
    },[&]{
        LARGE_INTEGER a{},b{};
        QueryPerformanceCounter(&a);
        // User-requested synchronous camera catch-up. This native operation
        // flushes shared channels; its duration is NOT bounded by our batch.
        CStreaming::LoadAllRequestedModels(true);
        QueryPerformanceCounter(&b);
        const uint64_t us=qpcFrequency_>0?uint64_t(
            double(b.QuadPart-a.QuadPart)*1000000.0/double(qpcFrequency_)):0;
        ++counters_.flushes;counters_.blockingUs+=us;
        counters_.peakBlockingUs=std::max(counters_.peakBlockingUs,us);
    });
    // Re-scan instead of retaining entity pointers across the blocking loader.
    unsigned attempts=0;
    LARGE_INTEGER instanceStart{};QueryPerformanceCounter(&instanceStart);
    if(cfg_.instanceLoaded&&cfg_.maxInstances&&canInstance_&&CStreaming::ms_bIsInitialised
       &&!CStreaming::m_bLoadingScene && instanceCadence_.Due(s.nowMs,cfg_.intervalMs)){
       scan(cells,cfg_.maxScanNodes,cfg_.maxScanUs,qpcFrequency_,[&](CEntity* e){
        if(attempts>=cfg_.maxInstances)return false;
        if(!e||e->m_pRwObject)return true;
        if(!cfg_.blocking&&qpcFrequency_>0){
            LARGE_INTEGER now{};QueryPerformanceCounter(&now);
            if(double(now.QuadPart-instanceStart.QuadPart)*1000000.0/qpcFrequency_>=1000)return false;
        }
        if(!eligible(e,s,cfg_,cosine_))return true;
        if(CStreaming::ms_aInfoForModel[e->m_nModelIndex].m_nLoadState!=LOADSTATE_LOADED)return true;
        ++attempts;e->CreateRwObject();
        if(e->m_pRwObject)++counters_.instances;
        return attempts<cfg_.maxInstances;
    });
       instanceCadence_.Complete(GetTickCount());
    }
    cadence_.Complete(GetTickCount()); // space batches from completion, including stalls
    // Queuing an async request does not allocate the model yet. Only a real
    // flush or instancing attempt requires an immediate OS-memory resample.
    return (batch.needsFlush&&cfg_.blocking&&canFlush_)||attempts!=0;
}
