#include "asc/CatalogNativePrefetch.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <unordered_set>
#include <vector>
#include <string>

using namespace asc;
using namespace asc::catalog_prefetch;
struct FakeGame final: IGame {
    std::unordered_set<int> requested, loaded, eligible;
    std::vector<int> issued;
    std::vector<Candidate> Collect(const std::vector<Cell>&,Vec3,Vec3,Vec3,Vec3,bool,unsigned) override {
        return {};
    }
    bool CanRequest(int id) const override {return eligible.contains(id)&&!requested.contains(id);}
    bool IsLoaded(int id) const override {return loaded.contains(id);}
    bool IsStillRequested(int id) const override {return requested.contains(id);}
    void RequestDefault(int id) override {issued.push_back(id);requested.insert(id);}
};
static native::CatalogCellReply Reply(int x=1,int y=2) {
    native::CatalogCellReply r{};
    r.cellX=x;r.cellY=y;r.modelCount=4;
    r.modelIds[0]=100;r.modelIds[1]=101;r.modelIds[2]=102;r.modelIds[3]=103;
    std::strcpy(r.modelNames[0],"good100");
    std::strcpy(r.modelNames[1],"good101");
    std::strcpy(r.modelNames[2],"good102");
    std::strcpy(r.modelNames[3],"good103");
    return r;
}
static Snapshot SafeState(uint32_t now=10000) {
    Snapshot s{};
    s.nowMs=now;s.playerValid=true;s.gameplaySafe=true;
    s.streamingBudget=512*1024*1024ull;s.streamingUsed=100*1024*1024ull;
    s.availablePhysicalBytes=1024*1024*1024ull;s.recentFrameMs=15.0f;
    return s;
}
int main() {
    Settings conf{}; conf.enabled=true;
    assert(Valid(conf));
    char safe[24]{};
    assert(!CopySafeName("",safe));
    assert(CopySafeName("dash-name_23",safe));
    assert(std::strcmp(safe,"dash-name_23")==0);
    char bad[24]{}; std::memset(bad,'X',24);
    assert(!CopySafeName(bad,safe));
    std::strcpy(bad,"../oops");assert(!CopySafeName(bad,safe));
    auto verify=[](int id,const char* name) {
        return id>=100 && id<=103 &&
            std::strcmp(name,(std::string("good")+std::to_string(id)).c_str())==0;
    };
    auto r=Reply(); FakeGame game{};
    game.eligible={100,101,102,103};
    Scheduler scheduler{};scheduler.Configure(conf);
    auto s=SafeState();
    scheduler.Tick(s,&r,1,1,2,10,0,3,game,verify);
    assert(game.issued.size()==2 && game.issued[0]==100 && game.issued[1]==101);
    scheduler.Tick(s,&r,1,1,2,10,0,3,game,verify);
    assert(game.issued.size()==2); // second scheduler tick in SAME GTA frame
    s.nowMs+=20;
    scheduler.Tick(s,&r,1,1,2,11,0,3,game,verify);
    assert(game.issued.size()==4);
    // Native backlog always blocks new work without losing the page cursor.
    Scheduler blocked{};blocked.Configure(conf);
    FakeGame g2{};g2.eligible=game.eligible;
    s=SafeState();s.gameRequests=16;
    blocked.Tick(s,&r,2,1,2,s.nowMs,0,3,g2,verify);
    assert(g2.issued.empty());
    s.gameRequests=0;s.recentFrameMs=32;
    blocked.Tick(s,&r,2,1,2,s.nowMs,0,3,g2,verify);
    assert(g2.issued.empty());
    s.recentFrameMs=15;s.addressSpaceLow=true;
    blocked.Tick(s,&r,2,1,2,s.nowMs,0,3,g2,verify);
    assert(g2.issued.empty());
    s.addressSpaceLow=false;
    blocked.Tick(s,&r,2,0,0,s.nowMs,0,3,g2,verify);
    assert(g2.issued.empty());
    blocked.Tick(s,&r,2,1,2,s.nowMs,3,3,g2,verify);
    assert(g2.issued.empty());
    blocked.Tick(s,&r,2,1,2,s.nowMs,1,3,g2,verify);
    assert(g2.issued.size()==2);
    // Loaded models stop consuming our pending quota. Cooldown survives loads.
    for(int id: g2.issued) {g2.requested.erase(id);g2.loaded.insert(id);}
    auto later=Reply();later.modelCount=2;
    s.nowMs+=100;
    blocked.Tick(s,&later,3,1,2,s.nowMs,0,3,g2,verify);
    assert(g2.issued.size()==2);
    g2.loaded.clear();
    s.nowMs+=100;
    blocked.Tick(s,&later,4,1,2,s.nowMs,0,3,g2,verify);
    assert(g2.issued.size()==2);
    s.nowMs+=conf.cooldownMs+1;
    blocked.Tick(s,&later,5,1,2,s.nowMs,0,3,g2,verify);
    assert(g2.issued.size()==4);
    // Untrusted catalog identity cannot silently request the wrong model.
    auto spoof=Reply();spoof.modelCount=1;spoof.modelIds[0]=110;
    FakeGame g3{};g3.eligible={110};
    Scheduler rejected{};rejected.Configure(conf);
    rejected.Tick(SafeState(),&spoof,1,1,2,100,0,3,g3,verify);
    assert(g3.issued.empty() && rejected.GetCounters().wrongIdentity==1);
    // Names missing terminators must not reach the host's name hash.
    spoof.modelIds[0]=100;std::memset(spoof.modelNames[0],'x',24);g3.eligible.insert(100);
    rejected.Tick(SafeState(),&spoof,2,1,2,100,0,3,g3,verify);
    assert(g3.issued.empty() && rejected.GetCounters().badName==1);
    // Prediction follows velocity and rejects teleport-derived lookahead.
    Trajectory track{};
    auto p=track.Predict({100,100,0},1000,conf,120);
    assert(p.x==100);
    p=track.Predict({110,100,0},1100,conf,120);
    assert(p.x>110 && p.x<=110+conf.maxPredictionUnits);
    p=track.Predict({2000,2000,0},1200,conf,120);
    assert(p.x==2000 && p.y==2000);
    return 0;
}
