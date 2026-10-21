#pragma once
// Pure bounded spatial policy for far-distance static IPL LOD lists.
// No raw CEntity pointers are cached: callers re-read GTA world lists each frame.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace asc::world_range {
constexpr int kLodGridX=30, kLodGridY=30;
constexpr float kWorldMin=-3000.f, kLodCellSize=200.f;
struct LodCell { uint8_t x{}, y{}; float distanceSquared{}; };
struct LodPlan { std::array<LodCell,kLodGridX*kLodGridY> cells{}; uint16_t count{}; };

inline LodPlan PlanLodCells(float camX,float camY,float forwardX,float forwardY,
                            float nearRange,float farRange) noexcept {
    LodPlan plan{};
    if(!std::isfinite(camX) || !std::isfinite(camY) ||
       !std::isfinite(forwardX) || !std::isfinite(forwardY) ||
       !std::isfinite(nearRange) || !std::isfinite(farRange) ||
       nearRange<0.f || farRange<=nearRange || farRange>10000.f)
        return plan;
    const float radius=141.422f; // maximum center-to-corner error in a 200-unit tile
    const float near2=std::max(0.f,nearRange-radius)*std::max(0.f,nearRange-radius);
    const float far2=(farRange+radius)*(farRange+radius);
    const float fMag=std::sqrt(forwardX*forwardX+forwardY*forwardY);
    const bool checkFacing=fMag>0.0001f && std::isfinite(fMag);
    if(checkFacing){ forwardX/=fMag;forwardY/=fMag; }
    for(int y=0;y<kLodGridY;++y)for(int x=0;x<kLodGridX;++x) {
        const float dx=kWorldMin+(x+.5f)*kLodCellSize-camX;
        const float dy=kWorldMin+(y+.5f)*kLodCellSize-camY;
        const float d2=dx*dx+dy*dy;
        if(d2<near2 || d2>far2)continue;
        // Conservative forward half-plane: include neighbouring tiles at the
        // edge of the frustum; actual entity visibility remains GTA's decision.
        if(checkFacing && dx*forwardX+dy*forwardY < -radius)continue;
        plan.cells[plan.count++]={uint8_t(x),uint8_t(y),d2};
    }
    std::sort(plan.cells.begin(),plan.cells.begin()+plan.count,
       [](const LodCell& a,const LodCell& b){
         if(a.distanceSquared!=b.distanceSquared)
             return a.distanceSquared<b.distanceSquared;
         return a.y==b.y?a.x<b.x:a.y<b.y;
       });
    return plan;
}
// Fairly visit a bounded number of LOD tiles from a sorted plan across frames.
// The plan is rebuilt from the current camera each frame; only a small numeric
// cursor is retained, never native GTA entity pointers or sector-list nodes.
// A zero-length plan must not evaluate a modulo operation.
constexpr uint16_t RotatingLodIndex(uint32_t cursor, uint32_t offset,
                                    uint16_t count) noexcept {
    return count ? uint16_t((cursor + offset) % count) : 0;
}
// Per-model request cooldown applies only to speculative DistantStreaming
// requests; original GTA model streaming remains unaffected.
inline bool CanRequest(uint32_t now,uint32_t lastAttempt,uint32_t cooldownMs) noexcept {
    return lastAttempt==0 || uint32_t(now-lastAttempt)>=cooldownMs;
}
inline bool AllowExtras(uint64_t availVirtualBytes,uint32_t minFreeMiB,
                        bool engineEmergency) noexcept {
    return !engineEmergency && availVirtualBytes>=uint64_t(minFreeMiB)*1048576u;
}
} // namespace asc::world_range
