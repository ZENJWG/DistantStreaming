#include "asc/Core.h"
#include "asc/StatsWindow.h"
#include <iostream>
#include <stdexcept>
#include <unordered_set>
using namespace asc;
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(0)
struct World : IGame {
    std::vector<Candidate> candidates;
    std::unordered_set<int> loaded,pending;
    std::vector<int> requested,priority;
    std::vector<Candidate> Collect(const std::vector<Cell>&,Vec3,Vec3,Vec3,Vec3,bool,unsigned)override{
        return candidates;
    }
    bool CanRequest(int)const override{return true;}
    bool IsLoaded(int id)const override{return loaded.contains(id);}
    bool IsStillRequested(int id)const override{return pending.contains(id);}
    void RequestDefault(int id)override{requested.push_back(id);pending.insert(id);}
    void RequestPriority(int id)override{priority.push_back(id);RequestDefault(id);}
};
Snapshot state(uint32_t now){
    Snapshot s;
    s.nowMs=now;s.playerValid=s.gameplaySafe=true;
    s.streamingBudget=256*1048576ULL;s.streamingUsed=50*1048576ULL;
    s.availablePhysicalBytes=2000*1048576ULL;
    return s;
}
int main(){
    try {
        Config cfg;
        CHECK(Controller::Valid(cfg));
        cfg.maxPriorityPerTick=5;CHECK(!Controller::Valid(cfg));
        cfg.maxPriorityPerTick=2;
        World world;world.candidates={{10,1}};
        Controller reload(cfg);
        reload.Tick(state(100),world);
        world.pending.clear();world.loaded.insert(10);
        reload.Tick(state(200),world);
        world.loaded.clear();
        reload.Tick(state(300),world);
        CHECK(world.requested.size()==2); // no six-second cooldown on real unload

        Controller expired(cfg);
        World old;old.candidates={{11,1}};
        expired.Tick(state(100),old);
        old.pending.clear();old.loaded.insert(11);
        expired.Tick(state(200),old);
        old.candidates.clear();expired.Tick(state(4000),old);
        CHECK(!expired.IsTracked(11));
        old.loaded.clear();old.candidates={{11,1}};
        expired.Tick(state(4100),old);
        CHECK(old.requested.size()==2); // completed history expiry is not a failed request

        World near;
        near.candidates={{1,1},{2,.3f,0,false,true},
                         {3,.2f,0,false,true},{4,.1f,0,false,true}};
        Controller urgent(cfg);urgent.Tick(state(100),near);
        CHECK(near.requested.size()==4);
        CHECK(near.requested[0]==2&&near.requested[1]==3&&near.requested[2]==4);
        CHECK(near.priority.size()==2&&urgent.GetStats().priorityIssued==2);
        CHECK(urgent.GetStats().issued==4); // priorities share total request quota

        World busy;busy.candidates=near.candidates;
        Controller yield(cfg);
        auto queued=state(100);queued.gamePriorityRequests=1;
        yield.Tick(queued,busy);
        CHECK(busy.priority.empty()&&busy.requested.size()==4);

        cfg.maxPriorityPerTick=0;
        World off;off.candidates=near.candidates;
        Controller disabled(cfg);disabled.Tick(state(100),off);
        CHECK(off.priority.empty()&&off.requested.size()==4);
        cfg.maxPriorityPerTick=2;

        World moderate;moderate.candidates=near.candidates;
        Controller throttle(cfg);
        auto warm=state(100);warm.streamingUsed=200*1048576ULL;
        throttle.Tick(warm,moderate);
        CHECK(throttle.GetStats().state==Pressure::Throttled);
        CHECK(throttle.GetStats().adaptiveQuota==1);
        CHECK(moderate.requested.size()==1&&moderate.priority.empty());

        World blocked;blocked.candidates=near.candidates;
        Controller guard(cfg);
        auto low=state(100);low.addressSpaceLow=true;
        guard.Tick(low,blocked);
        CHECK(blocked.requested.empty());
        guard.Reset();low=state(100);low.recentFrameMs=35;
        guard.Tick(low,blocked);CHECK(blocked.requested.empty());
        guard.Reset();low=state(100);low.gameRequests=cfg.backlogLimit;
        guard.Tick(low,blocked);CHECK(blocked.requested.empty());

        StatsWindow window;
        Stats tick;tick.priorityIssued=2;window.Add(tick);
        tick.priorityIssued=1;window.Add(tick);
        CHECK(window.Take().priorityIssued==3);
        std::cout<<"PASS: immediate unload recovery, completed expiry, nearby ranking, bounded async priority, yielding, pressure and frame/backlog guards\n";
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
