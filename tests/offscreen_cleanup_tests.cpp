#include "asc/OffscreenCleanup.h"
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace asc::offscreen;
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(0)
int main(){
    try {
        Settings cfg;CHECK(Valid(cfg));
        auto bad=cfg;bad.maxScanPerFrame=0;CHECK(!Valid(bad));
        bad=cfg;bad.timeBudgetUs=10000;CHECK(!Valid(bad));
        bad=cfg;bad.maxPressureDeletes=1;CHECK(!Valid(bad));
        Protection p;CHECK(CanDelete(p));
        p.onScreen=true;CHECK(!CanDelete(p));p.onScreen=false;
        p.nearPlayer=true;CHECK(!CanDelete(p));p.nearPlayer=false;
        p.rendering=true;CHECK(!CanDelete(p));p.rendering=false;
        p.pinned=true;CHECK(!CanDelete(p));p.pinned=false;
        p.staticWorld=false;CHECK(!CanDelete(p));p.staticWorld=true;
        p.hasObject=false;CHECK(!CanDelete(p));p.hasObject=true;
        p.validModel=false;CHECK(!CanDelete(p));

        Stamp stamp;
        CHECK(!stamp.Observe(100,1,7,true,0,250));
        CHECK(!stamp.Observe(100,1,7,true,249,250));
        CHECK(stamp.Observe(100,1,7,true,250,250));
        CHECK(!stamp.Observe(100,1,7,false,300,250)); // seen again: grace restarts
        CHECK(!stamp.Observe(100,1,7,true,350,250));
        CHECK(stamp.Observe(100,1,7,true,600,250));
        CHECK(!stamp.Observe(101,1,7,true,650,250)); // new RW instance
        CHECK(!stamp.Observe(101,1,8,true,900,250)); // pool slot reused
        CHECK(!stamp.Observe(101,2,8,true,1150,250)); // model changed
        stamp={};
        CHECK(!stamp.Observe(1,1,0,true,UINT32_MAX-100,250));
        CHECK(stamp.Observe(1,1,0,true,150,250));
        CHECK(CanUnload(0,true,false,false,100,cfg));
        CHECK(!CanUnload(1,true,false,false,100,cfg)); // shared/on-screen reference
        CHECK(!CanUnload(0,true,true,false,100,cfg)); // mission/pinned/priority flags
        CHECK(!CanUnload(0,true,false,true,100,cfg)); // queued/recent demand
        CHECK(!CanUnload(0,false,false,false,100,cfg)); // loading/not loaded
        CHECK(!CanUnload(0,true,false,false,10000,cfg)); // costly free excluded

        RecentRequests<10> recent;
        CHECK(!recent.Recent(1,0,1000));
        recent.Record(1,0);CHECK(recent.Recent(1,999,1000));
        CHECK(!recent.Recent(1,1000,1000));
        recent.Record(1,UINT32_MAX-100);
        CHECK(recent.Recent(1,150,1000));
        recent.Record(-1,1);recent.Record(10,1);
        CHECK(!recent.Recent(-1,1,1000)&&!recent.Recent(10,1,1000));

        auto pending=std::make_unique<PendingModels>();
        for(int id=0;id<20000;++id){pending->Add(id);pending->Add(id);}
        CHECK(pending->Size()==20000);
        for(int id=0;id<20000;++id)CHECK(pending->Pop()==id);
        CHECK(pending->Pop()==-1);
        pending->Add(7);pending->Add(8);
        CHECK(pending->Pop()==7);pending->Add(7); // quiet-time retry stays queued
        CHECK(pending->Pop()==8&&pending->Pop()==7);
        pending->Add(9);pending->Clear();CHECK(pending->Size()==0);
        std::cout<<"PASS: offscreen/reference protections, grace and pool reuse, recent requests, rollover, deferred reclamation, bounds\n";
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
