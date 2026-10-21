#include "GameAdapter.h"
#include "Hooks.h"
#include "WorldListSafety.h"
#include "asc/CameraReadiness.h"
#include "asc/MotionScheduling.h"
#include <CWorld.h>
#include <CSector.h>
#include <CEntity.h>
#include <CStreaming.h>
#include <CStreamingInfo.h>
#include <CModelInfo.h>
#include <CClock.h>
#include <algorithm>
#include <bitset>
#include <cmath>
#include <vector>

namespace {
float sq(float f){return f*f;}
bool Better(const asc::Candidate& a,const asc::Candidate& b){
    if(a.urgent!=b.urgent)return a.urgent>b.urgent;
    return a.score==b.score?a.modelId<b.modelId:a.score>b.score;
}
bool validId(int id) {
    return id>=0&&id<static_cast<int>(asc::kVanillaModelInfoCountCap)
        && id<CModelInfo::ms_modelInfoCount
        && CModelInfo::ms_modelInfoPtrs
        && CModelInfo::ms_modelInfoPtrs[id]!=nullptr
        && CStreaming::ms_aInfoForModel;
}
bool visibleTime(const CBaseModelInfo* info) {
    // Virtual SDK GetTimeInfo is non-const.
    const auto* time=const_cast<CBaseModelInfo*>(info)->GetTimeInfo();
    if(!time)return true;
    const unsigned on=time->m_nTimeOn,off=time->m_nTimeOff;
    const unsigned hour=CClock::ms_nGameClockHours;
    if(on>=24||off>=24||on==off)return true;
    // Add +/- 1 hour safety so upcoming transitions aren't missed.
    const unsigned upcoming=(hour+1)%24;
    auto within=[=](unsigned h) {
        return on<off?(h>=on&&h<off):(h>=on||h<off);
    };
    return within(hour)||within(upcoming);
}
template<class Node>
void Traverse(Node* node,asc::Vec3 here,asc::Vec3 future,
              asc::Vec3 cam,asc::Vec3 fwd,bool cameraValid,
              float sectorScore,float priorityRadius,const asc::offscreen::Settings& cleanup,
              std::vector<asc::Candidate>& pool,
              std::array<int,asc::kVanillaModelInfoCountCap>& indices,
              unsigned& remainingNodes,bool lod=false,bool viewBoost=false) {
    unsigned localNodes=0;
    for(auto* n=node;n&&remainingNodes>0&&localNodes<512;) {
        // A mod may have left a stale node in a world-sector list.
        // Never dereference a plainly invalid link or entity pointer.
        if(!asc::world::Readable(n,sizeof(*n))) {
            ++asc::world::rejectedNodes;
            break;
        }
        auto* next=n->pNext;
        const auto* e=static_cast<CEntity*>(n->pItem);
        n=next;
        ++localNodes;
        --remainingNodes;
        if(!asc::world::Readable(e,sizeof(CEntity))) {
            ++asc::world::rejectedEntities;
            continue;
        }
        if(e->bDontStream||e->bRemoveFromWorld||e->m_nAreaCode!=0)
            continue;
        const int id=e->m_nModelIndex;
        if(!validId(id))continue;
        const auto& stream=CStreaming::ms_aInfoForModel[id];
        // Loaded assets must not fill the top-K list ahead of missing models.
        // Pending requests remain visible for demand refresh and accounting.
        if(stream.m_nLoadState==LOADSTATE_LOADED||stream.m_nCdSize==0)continue;
        if(stream.m_nFlags&(MISSION_REQUIRED|KEEP_IN_MEMORY|LOADING_SCENE))continue;
        if(stream.m_nLoadState==LOADSTATE_NOT_LOADED&&
           (stream.m_nFlags&GAME_REQUIRED))continue;
        const CBaseModelInfo* info=CModelInfo::ms_modelInfoPtrs[id];
        if(!visibleTime(info))continue;
        const CVector& pos=e->GetPosition();
        if(!std::isfinite(pos.x)||!std::isfinite(pos.y)||
           !std::isfinite(pos.z))continue;
        const float dHere=sq(pos.x-here.x)+sq(pos.y-here.y);
        const float dFuture=sq(pos.x-future.x)+sq(pos.y-future.y);
        // Don't immediately re-request rear scenery just reclaimed by cleanup.
        if(cleanup.enabled&&cameraValid&&dHere>sq(cleanup.keepNearUnits)
           &&!asc::camera::InView({pos.x,pos.y,pos.z},cam,fwd,100000,.08716f)
           &&!const_cast<CEntity*>(e)->GetIsOnScreen())continue;
        const float distanceHere=std::sqrt(dHere),distanceFuture=std::sqrt(dFuture);
        const float dist=std::min(distanceHere,distanceFuture);
        // Respect approximate draw distance without rejecting essential
        // nearby/current-world models due to atypical low LOD values.
        if(dist>std::max(80.0f,std::min(info->m_fDrawDistance,lod?800.0f:300.0f)+40.0f))
            continue;
        const float x=pos.x-cam.x,y=pos.y-cam.y,z=pos.z-cam.z;
        const float camDistance=std::sqrt(x*x+y*y+z*z);
        float view=0.5f;
        if(cameraValid&&camDistance>1.0f) {
            const float dot=(x*fwd.x+y*fwd.y+z*fwd.z)/camDistance;
            // Don't completely starve assets behind camera: turning matters.
            view=std::clamp((dot+0.4f)/1.4f,0.05f,1.0f);
        }
        const float urgency=1.0f/(1.0f+distanceFuture*0.014f);
        const float proximity=1.0f/(1.0f+distanceHere*0.012f);
        // IMG sector count is only a *disk cost hint*, not runtime RAM.
        const unsigned sectors=stream.m_nCdSize;
        const float cost=1.0f/(1.0f+float(std::min(sectors,1024u))/128.0f);
        const float score=(viewBoost?.52f:.32f)*view+
                         (viewBoost?.17f:.27f)*urgency+(viewBoost?.12f:.22f)*proximity
                         +.12f*sectorScore+.07f*cost;
        const bool urgent=dHere<=sq(priorityRadius);
        if(!std::isfinite(score))continue;
        const asc::Candidate candidate{id,score,sectors,false,urgent};
        auto& index=indices[unsigned(id)];
        if(index>=0){
            if(Better(candidate,pool[size_t(index)]))pool[size_t(index)]=candidate;
        }else{
            index=static_cast<int>(pool.size());
            pool.push_back(candidate);
        }
    }
}
}
std::vector<asc::Candidate> GameAdapter::Collect(
    const std::vector<asc::Cell>& cells,asc::Vec3 current,
    asc::Vec3 future,asc::Vec3 camera,asc::Vec3 forward,
    bool cameraValid,unsigned maxCandidates) {
    std::vector<asc::Candidate> out;
    CollectInto(cells,current,future,camera,forward,cameraValid,maxCandidates,out);
    return out;
}
bool GameAdapter::CollectInto(const std::vector<asc::Cell>& cells,
    asc::Vec3 current,asc::Vec3 future,asc::Vec3 camera,asc::Vec3 forward,
    bool cameraValid,unsigned maxCandidates,std::vector<asc::Candidate>& out) {
    // Exchange storage with the controller instead of copying the ranked list.
    scored_.swap(out);
    scored_.clear();
    if(!maxCandidates){scored_.swap(out);return true;}
    // Reuse successful region checks only during this read-only traversal.
    // A new cache each call observes pool remaps between scheduler ticks.
    asc::world::RegionValidationCache regions;
    asc::world::ScopedRegionCache validation(regions);
    // Inspect the whole bounded traversal before choosing candidates.
    // Never stop just because the first cells filled the output limit.
    auto& scored=scored_;
    scored.clear();
    scoredIndex_.fill(-1);
    unsigned remainingNodes=3072;
    auto scan=[&](const asc::Cell& cell){
        if(!CWorld::ms_aSectors||!remainingNodes||cell.x<0||cell.x>=120||cell.y<0||cell.y>=120)return;
        auto* s=CWorld::GetSector(cell.x,cell.y);
        Traverse(s->m_buildingList.GetNode(),current,future,camera,forward,
                 cameraValid,cell.score,priorityRadius_,cleanup_,scored,scoredIndex_,remainingNodes,false,fastCamera_);
        Traverse(s->m_dummyList.GetNode(),current,future,camera,forward,
                 cameraValid,cell.score,priorityRadius_,cleanup_,scored,scoredIndex_,remainingNodes,false,fastCamera_);
    };
    // Under a dense-sector traversal cap, don't repeatedly spend the entire
    // budget near the player and never reach the predicted endpoint.
    const int currentX=std::clamp(CWorld::GetSectorIndexX(current.x),0,119);
    const int currentY=std::clamp(CWorld::GetSectorIndexY(current.y),0,119);
    const int futureX=std::clamp(CWorld::GetSectorIndexX(future.x),0,119);
    const int futureY=std::clamp(CWorld::GetSectorIndexY(future.y),0,119);
    const auto currentCell=[&]{
        for(const auto& cell:cells)
            if(cell.x==currentX&&cell.y==currentY)scan(cell);
    };
    const auto futureCell=[&]{
        for(const auto& cell:cells)
            if((cell.x!=currentX||cell.y!=currentY)&&
               cell.x==futureX&&cell.y==futureY)scan(cell);
    };
    if(fastTravel_ && !fastCamera_){futureCell();currentCell();}
    else {currentCell();futureCell();}
    for(const auto& cell:cells){
        if(!remainingNodes)break;
        if((cell.x==currentX&&cell.y==currentY)||
           (cell.x==futureX&&cell.y==futureY))continue;
        scan(cell);
    }
    // Distant building placeholders live in the separate 30x30 LOD grid.
    // Reserve traversal work for them so missing LODs don't leave empty world.
    std::bitset<900> visitedLods;
    unsigned lodNodes=1024;
    if(CWorld::ms_aLodPtrLists)for(const auto& cell:cells){
        if(!lodNodes)break;
        if(cell.x<0||cell.x>=120||cell.y<0||cell.y>=120)continue;
        const unsigned lx=unsigned(cell.x)/4,ly=unsigned(cell.y)/4;
        const unsigned index=ly*30+lx;
        if(visitedLods.test(index))continue;
        visitedLods.set(index);
        Traverse(CWorld::GetLodPtrList(int(lx),int(ly))->GetNode(),
                 current,future,camera,forward,cameraValid,cell.score,
                 priorityRadius_,cleanup_,scored,scoredIndex_,lodNodes,true,fastCamera_);
    }
    // Each model already retains its best instance. Select top K before
    // sorting, avoiding a full sort of duplicate world placements.
    if(scored.size()>maxCandidates){
        std::nth_element(scored.begin(),scored.begin()+maxCandidates,scored.end(),Better);
        scored.resize(maxCandidates);
    }
    std::sort(scored.begin(),scored.end(),Better);
    scored_.swap(out);
    return true;
}
unsigned GameAdapter::RequestAllowance() const {
    return requestBudget_?requestBudget_->Available():~0u;
}
bool GameAdapter::CanRequest(int id) const {
    if(!RequestAllowance() || !validId(id))return false;
    const auto& info=CStreaming::ms_aInfoForModel[id];
    return info.m_nCdSize!=0
        && info.m_nLoadState==LOADSTATE_NOT_LOADED
        && (info.m_nFlags&(GAME_REQUIRED|MISSION_REQUIRED|KEEP_IN_MEMORY|LOADING_SCENE))==0;
}
bool GameAdapter::IsLoaded(int id) const {
    return validId(id)&&CStreaming::ms_aInfoForModel[id].m_nLoadState==
        LOADSTATE_LOADED;
}
bool GameAdapter::IsStillRequested(int id) const {
    if(!validId(id))return false;
    const auto state=CStreaming::ms_aInfoForModel[id].m_nLoadState;
    return state==LOADSTATE_Requested||state==LOADSTATE_Channeled||
           state==LOADSTATE_Finishing;
}
void GameAdapter::RequestDefault(int id) {
    Request(id,false);
}
void GameAdapter::RequestPriority(int id) {
    Request(id,true);
}
void GameAdapter::Request(int id,bool priority) {
    if(requestBudget_ && !requestBudget_->Take())return;
    struct Scope {
        Scope(){hooks::EnterPluginRequest();}
        ~Scope(){hooks::ExitPluginRequest();}
    } scope;
    CStreaming::RequestModel(id,priority?PRIORITY_REQUEST:DEFAULT);
}
