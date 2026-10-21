#include "asc/StreamingMemory.h"
#include "asc/Core.h"
#include <iostream>
#include <stdexcept>
using namespace asc::memory;
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(0)
struct Game : asc::IGame {
    unsigned requests=0;
    std::vector<asc::Candidate> Collect(const std::vector<asc::Cell>&,
        asc::Vec3,asc::Vec3,asc::Vec3,asc::Vec3,bool,unsigned) override {
        return {{1,1}};
    }
    bool CanRequest(int)const override{return true;}
    bool IsLoaded(int)const override{return false;}
    bool IsStillRequested(int)const override{return false;}
    void RequestDefault(int)override{++requests;}
};
int main(){
    try {
        Settings s;
        CHECK(Valid(s));
        CHECK(!Valid({true,unsigned(-1),256}));
        CHECK(!Valid({true,4097,256}));
        CHECK(!Valid({true,16,256}));
        CHECK(!Valid({true,0,0}));
        auto r=Calculate(s,true,4096*MiB,3500*MiB,100*MiB);
        CHECK(r.processLimitBytes==4096*MiB);
        CHECK(r.budgetBytes==512*MiB&&!r.lowAddressSpace);
        // Non-LAA and 32-bit OS limits override any configured/unlimited cap.
        r=Calculate(s,false,4096*MiB,4096*MiB,0);
        CHECK(r.processLimitBytes==2048*MiB&&r.budgetBytes==512*MiB);
        r=Calculate(s,true,3072*MiB,3072*MiB,0);
        CHECK(r.processLimitBytes==3072*MiB&&r.budgetBytes==512*MiB);
        r=Calculate(s,true,2048*MiB,2048*MiB,0);
        CHECK(r.budgetBytes==512*MiB);
        s.limitMiB=512;
        CHECK(Calculate(s,true,4096*MiB,3000*MiB,0).budgetBytes==512*MiB);
        s.limitMiB=4096;
        r=Calculate(s,true,4096*MiB,4096*MiB,4096*MiB);
        CHECK(r.budgetBytes==3584*MiB); // VA headroom, no fixed 1024 MiB cap
        s.limitMiB=2000;
        s.reserveMiB=384;
        r=Calculate(s,true,4096*MiB,2841*MiB,0);
        CHECK(r.budgetBytes==2000*MiB); // reproduces supplied game log scenario
        s.limitMiB=0;
        r=Calculate(s,true,4096*MiB,4096*MiB,0);
        CHECK(r.budgetBytes==3712*MiB); // no user cap; reserve-only bound
        s.reserveMiB=512;
        r=Calculate(s,true,4096*MiB,128*MiB,512*MiB);
        CHECK(r.lowAddressSpace&&r.budgetBytes==128*MiB);
        r=Calculate(s,true,4096*MiB,0,0);
        CHECK(r.lowAddressSpace&&r.budgetBytes==MinimumBudget);
        CHECK(Calculate(s,true,0,0,0).budgetBytes==0);
        CHECK(Calculate(s,true,1,0,0).budgetBytes==0);
        // Broad range: every budget is nonzero, aligned, within OS/LAA/user caps.
        for(bool laa:{false,true})for(unsigned cap:{0u,64u,1024u,4096u})
            for(uint64_t total:{2048*MiB,3072*MiB,4096*MiB})
                for(uint64_t free=0;free<=total;free+=37*MiB){
                    r=Calculate({true,cap,256},laa,total,free,500*MiB);
                    CHECK(r.budgetBytes>0&&(r.budgetBytes&65535)==0);
                    CHECK(r.budgetBytes<=r.processLimitBytes-256*MiB);
                    CHECK(r.budgetBytes<=MaxRepresentableBudget);
                    if(cap)CHECK(r.budgetBytes<=uint64_t(cap)*MiB);
                }
        // Removing old fixed process guards permits LAA-sized memory use;
        // low VA still denies speculation even with a generous stream budget.
        asc::Config cfg;
        cfg.privateLimitMiB=cfg.workingSetLimitMiB=0;
        CHECK(asc::Controller::Valid(cfg));
        asc::Controller ctl(cfg);
        Game game;
        asc::Snapshot snap;
        snap.nowMs=100;snap.playerValid=snap.gameplaySafe=true;
        snap.streamingBudget=3000*MiB;snap.streamingUsed=1000*MiB;
        snap.privateBytes=snap.workingSetBytes=2500*MiB;
        snap.availablePhysicalBytes=1024*MiB;snap.addressSpaceLow=true;
        ctl.Tick(snap,game);
        CHECK(ctl.GetStats().memoryGuard&&game.requests==0);
        snap.nowMs=200;snap.addressSpaceLow=false;
        ctl.Tick(snap,game);
        CHECK(!ctl.GetStats().memoryGuard&&game.requests==1);
        std::cout<<"PASS: uncapped native streaming budget, uint32 ceiling, LAA/OS headroom, admission\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
