#pragma once
#include "asc/Core.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace asc::motion {
struct Settings {
    bool enabled=true;
    unsigned controllerMs=20,cameraMs=40,holdMs=400;
    float travelSpeed=35.0f,cameraSpeed=60.0f,turnDegreesPerSecond=120.0f;
    unsigned requestsPerSecond=240,requestBurst=12;
};
// Main-thread only. Positions/directions are values; no game pointers survive.
class Detector {
public:
    void Reset(){have_=travelHeld_=cameraHeld_=false;}
    void Update(Snapshot& s,const Config& core,const Settings& cfg,bool slowWork){
        s.fastTravel=s.fastCamera=false;
        const auto finite=[](Vec3 v){
            return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
        };
        if(!cfg.enabled || !s.gameplaySafe || !s.playerValid || s.area!=0 ||
           !finite(s.player) || (s.cameraValid &&
               (!finite(s.camera)||!finite(s.cameraForward)))){Reset();return;}
        const auto distance=[](Vec3 a,Vec3 b){
            const double x=double(a.x)-b.x,y=double(a.y)-b.y,z=double(a.z)-b.z;
            return std::sqrt(x*x+y*y+z*z);
        };
        const uint32_t dt=uint32_t(s.nowMs-previous_.nowMs);
        const double travel=have_?distance(s.player,previous_.player):0;
        // Save/reload, pauses, teleports and invalid samples cannot create boosts.
        if(have_ && (s.area!=previous_.area || dt>500 ||
                     travel>core.maxTeleportUnits))Reset();
        const bool pressure=slowWork || s.addressSpaceLow || s.gpuBudgetCritical ||
            !s.streamingBudget ||
            s.streamingUsed>=s.streamingBudget*core.memoryThrottlePercent/100 ||
            s.gameRequests>=std::max(1u,core.backlogLimit/2) ||
            s.recentFrameMs>=core.targetFrameMs ||
            s.recentLoadAvgUs>=core.slowLoadUs ||
            s.availablePhysicalBytes<uint64_t(core.minAvailablePhysicalMiB)*1048576 ||
            (core.privateLimitMiB && s.privateBytes>uint64_t(core.privateLimitMiB)*1048576) ||
            (core.workingSetLimitMiB && s.workingSetBytes>uint64_t(core.workingSetLimitMiB)*1048576);
        if(pressure){travelHeld_=cameraHeld_=false;}
        else if(have_ && dt>=16){
            if(travel*1000.0/dt>=cfg.travelSpeed){
                travelHeld_=true;lastTravel_=s.nowMs;
            }
            if(s.cameraValid && previous_.cameraValid){
                const auto a=s.cameraForward,b=previous_.cameraForward;
                const double dot=double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z;
                const double norm=std::sqrt((double(a.x)*a.x+double(a.y)*a.y+double(a.z)*a.z)*
                    (double(b.x)*b.x+double(b.y)*b.y+double(b.z)*b.z));
                const double degrees=norm>0.01?
                    std::acos(std::clamp(dot/norm,-1.0,1.0))*57.295779513:0;
                // Following a fast player is travel, not a separate camera
                // change. Measure translation relative to the player instead.
                const double x=double(s.camera.x)-previous_.camera.x-
                    (double(s.player.x)-previous_.player.x);
                const double y=double(s.camera.y)-previous_.camera.y-
                    (double(s.player.y)-previous_.player.y);
                const double z=double(s.camera.z)-previous_.camera.z-
                    (double(s.player.z)-previous_.player.z);
                const double cameraTravel=std::sqrt(x*x+y*y+z*z);
                // A camera cut gets one bounded view boost, not travel prediction.
                if(degrees*1000.0/dt>=cfg.turnDegreesPerSecond ||
                   cameraTravel*1000.0/dt>=cfg.cameraSpeed){
                    cameraHeld_=true;lastCamera_=s.nowMs;
                }
            }
        }
        s.fastTravel=travelHeld_ && uint32_t(s.nowMs-lastTravel_)<cfg.holdMs;
        s.fastCamera=cameraHeld_ && s.cameraValid &&
            uint32_t(s.nowMs-lastCamera_)<cfg.holdMs;
        previous_=s;have_=true;
    }
private:
    Snapshot previous_{};
    uint32_t lastTravel_=0,lastCamera_=0;
    bool have_=false,travelHeld_=false,cameraHeld_=false;
};

// Shared by camera DFF/TXD, predictive and catalog calls. A token bucket smooths
// bursts; the fixed timestamp ring also enforces the one-second ceiling.
// GTA's recursive dependency requests and other mods are outside this limiter.
class RequestBudget {
public:
    void Reset(){have_=false;head_=count_=credits_=0;}
    void Begin(uint32_t now,unsigned rate,unsigned burst){
        rate_=std::clamp(rate,1u,256u);
        const unsigned capacity=std::clamp(burst,1u,16u)*1000;
        if(!have_){credits_=capacity;have_=true;}
        else credits_=unsigned(std::min<uint64_t>(capacity,
            uint64_t(credits_)+uint64_t(uint32_t(now-now_))*rate_));
        now_=now;
        while(count_ && uint32_t(now-times_[head_])>=1000){
            head_=(head_+1)%256u;--count_;
        }
    }
    unsigned Available() const {
        return have_?std::min(credits_/1000,rate_>count_?rate_-count_:0u):0;
    }
    bool Take(){
        if(!Available()){++denied_;return false;}
        times_[(head_+count_)%times_.size()]=now_;++count_;credits_-=1000;
        ++issued_;return true;
    }
    uint64_t Issued()const{return issued_;}
    uint64_t Denied()const{return denied_;}
private:
    std::array<uint32_t,256> times_{};
    unsigned head_=0,count_=0,credits_=0,rate_=240;
    uint32_t now_=0;
    bool have_=false;
    uint64_t issued_=0,denied_=0;
};
}
