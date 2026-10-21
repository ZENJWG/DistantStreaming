#include "asc/Core.h"
#include "asc/StatsWindow.h"
#include <limits>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include <algorithm>
#include <cmath>
using namespace asc;
#define CHECK(x) do {if(!(x)) throw std::runtime_error( #x );} while(0)
struct Fake : IGame {
    mutable unsigned admissionChecks=0;
    std::vector<Candidate> candidates;
    std::unordered_set<int> loaded,pending,valid;
    std::vector<int> requests;
    std::vector<Candidate> Collect(const std::vector<Cell>&cells,Vec3,Vec3,
                 Vec3,Vec3,bool,unsigned cap) override {
        CHECK(!cells.empty()&&cells.size()<=128);
        auto out=candidates;
        if(out.size()>cap)out.resize(cap);
        return out;
    }
    bool CanRequest(int id) const override {++admissionChecks;return valid.count(id)!=0;}
    bool IsLoaded(int id) const override {return loaded.count(id)!=0;}
    bool IsStillRequested(int id) const override {return pending.count(id)!=0;}
    void RequestDefault(int id) override{requests.push_back(id);pending.insert(id);}
};
static Snapshot snap(uint32_t now,Vec3 pos={0,0,5}) {
    Snapshot s;
    s.nowMs=now;s.player=pos;s.playerValid=true;s.gameplaySafe=true;
    s.streamingUsed=50*1048576ULL;
    s.streamingBudget=256*1048576ULL;
    s.privateBytes=650*1048576ULL;
    s.workingSetBytes=700*1048576ULL;
    s.availablePhysicalBytes=2000*1048576ULL;
    s.camera={0,0,10};s.cameraForward={1,0,0};s.cameraValid=true;
    return s;
}
int main() {
    try{
        Config c;
        CHECK(Controller::Valid(c));
        c.frameRecoveryMs=c.targetFrameMs;
        CHECK(!Controller::Valid(c));
        c=Config{};
        c.maxTeleportUnits=std::numeric_limits<float>::infinity();
        CHECK(!Controller::Valid(c));
        c=Config{};
        c.maxAdaptiveRequests=99;
        CHECK(!Controller::Valid(c));
        c=Config{};
        // A long fast-travel corridor must include the present sector and
        // future sectors; no endpoint-only scan.
        auto cells=Controller::SelectCells({0,0,0},{300,0,0},60,64);
        CHECK(!cells.empty()&&cells.size()<=64);
        bool present=false,atFuture=false,inMiddle=false;
        for(auto&v:cells){
            CHECK(v.x>=0&&v.x<120&&v.y>=0&&v.y<120);
            present |= v.x==60&&v.y==60;
            atFuture |= v.x==66&&v.y==60;
            inMiddle |= v.x==63&&v.y==60;
        }
        CHECK(present&&atFuture&&inMiddle);
        cells=Controller::SelectCells({0,0,0},{300,0,0},60,2);
        CHECK(cells.size()==2);
        CHECK(std::any_of(cells.begin(),cells.end(),[](const Cell&c){
            return c.x==60&&c.y==60;
        }));
        CHECK(std::any_of(cells.begin(),cells.end(),[](const Cell&c){
            return c.x==66&&c.y==60;
        }));
        cells=Controller::SelectCells({-2998,-2998,0},{-3050,-3050,0},80,20);
        CHECK(!cells.empty());
        CHECK(Controller::SelectCells({NAN,0,0},{0,0,0},60,20).empty());
        CHECK(Controller::SelectCells({0,0,0},{0,0,0},-1,20).empty());
        // Endpoint insertion must never overwrite the other endpoint.
        for(int dx=-12;dx<=12;++dx)for(int dy=-12;dy<=12;++dy) {
            const auto pair=Controller::SelectCells(
                {1,1,0},{float(dx*50+1),float(dy*50+1),0},25,2);
            CHECK(std::any_of(pair.begin(),pair.end(),[](const Cell& c){
                return c.x==60&&c.y==60;
            }));
            CHECK(std::any_of(pair.begin(),pair.end(),[&](const Cell& c){
                return c.x==60+dx&&c.y==60+dy;
            }));
        }
        // Huge finite coordinates must be clamped before float-to-int casts.
        const float huge=std::numeric_limits<float>::max();
        cells=Controller::SelectCells({huge,huge,0},{huge,huge,0},25,2);
        for(const auto& cell:cells)CHECK(cell.x>=0&&cell.x<120);

        StatsWindow window;
        Stats tick{};
        tick.issued=2;tick.promotions=1;tick.dynamicIssued=1;
        tick.dynamicDeferred=3;tick.outstanding=2;
        window.Add(tick);
        tick.issued=1;tick.promotions=0;tick.dynamicIssued=0;
        tick.dynamicDeferred=2;tick.outstanding=3;
        window.Add(tick);
        auto interval=window.Take();
        CHECK(interval.issued==3&&interval.promotions==1);
        CHECK(interval.dynamicIssued==1&&interval.dynamicDeferred==5);
        CHECK(interval.outstanding==3&&window.Take().issued==0);

        // Invalid scores must be removed before the sorting comparator runs.
        Fake malformed;
        malformed.candidates={{1,NAN},{2,INFINITY},{3,-INFINITY},{4,.5f}};
        malformed.valid={1,2,3,4};
        Controller sanitized;
        sanitized.Tick(snap(100),malformed);
        CHECK(malformed.requests==std::vector<int>{4});
        CHECK(sanitized.GetStats().candidates==1);

        // A rejected static candidate is checked only once per tick, even
        // when unused dynamic quota is returned to the static path.
        Fake rejected;
        rejected.candidates={{10,.8f},{11,.7f}};
        Controller singlePass;
        singlePass.Tick(snap(100),rejected);
        CHECK(rejected.admissionChecks==2);
        CHECK(singlePass.GetStats().denied==2);

        // Returning an unused dynamic slot continues static admission at
        // the saved cursor and still fills the total quota.
        Config reservedConfig;
        reservedConfig.dynamicWarmup=true;
        Controller reserved(reservedConfig);
        Fake redistribution;
        redistribution.candidates={{10,1.0f},{11,.9f},{12,.8f},{13,.7f},{14,.6f}};
        redistribution.valid={11,12,13,14,700};
        redistribution.loaded={700};
        reserved.OnDynamicVanillaDemand(700,1000,{0,0,5});
        reserved.OnDynamicVanillaDemand(700,1400,{0,0,5});
        reserved.Tick(snap(1500),redistribution);
        CHECK(redistribution.requests==std::vector<int>({11,12,13,14}));
        CHECK(redistribution.admissionChecks==6);
        CHECK(reserved.GetStats().denied==2);

        // Memory counters are uint64_t: percentage arithmetic must not wrap
        // a near-full or over-budget sample back into normal admission.
        for(const uint64_t used : {UINT64_MAX-1,UINT64_MAX}) {
            Fake memoryGame;
            memoryGame.candidates={{1,1.0f}};memoryGame.valid={1};
            Controller memoryController;
            auto state=snap(100);
            state.streamingBudget=UINT64_MAX;
            state.streamingUsed=used;
            memoryController.Tick(state,memoryGame);
            CHECK(memoryController.GetStats().state==Pressure::Critical);
            CHECK(memoryGame.requests.empty());
        }
        {
            Fake memoryGame;
            Controller memoryController;
            auto state=snap(100);
            state.streamingBudget=1;state.streamingUsed=UINT64_MAX;
            memoryController.Tick(state,memoryGame);
            CHECK(memoryController.GetStats().state==Pressure::Critical);
        }

        // Preserve the existing integer-percent hysteresis boundaries.
        for(const uint64_t used : {7099u,7100u}) {
            Controller hysteresis;
            Fake memoryGame;
            auto state=snap(100);
            state.streamingBudget=10000;state.streamingUsed=8500;
            hysteresis.Tick(state,memoryGame);
            CHECK(hysteresis.GetStats().state==Pressure::Critical);
            state.nowMs=200;state.streamingUsed=used;
            hysteresis.Tick(state,memoryGame);
            CHECK(hysteresis.GetStats().state==
                  (used==7099?Pressure::Normal:Pressure::Critical));
        }

        Fake g;
        g.candidates={{5,.7f},{1,.9f},{5,.5f},{2,.2f},{3,.1f}};
        g.valid={1,2,3,5};
        g.pending.insert(2);
        Controller ctl(c);
        ctl.Tick(snap(100),g);
        CHECK(g.requests.size()==3);
        CHECK(g.requests[0]==1&&g.requests[1]==5&&g.requests[2]==3);
        CHECK(!ctl.IsTracked(2)&&ctl.IsTracked(1));
        ctl.OnVanillaRequest(1,4);
        ctl.Tick(snap(200,{1,0,5}),g);
        CHECK(!ctl.IsTracked(1));
        CHECK(ctl.GetStats().promotions==1);

        auto heavy=snap(300);
        heavy.streamingUsed=230*1048576ULL;
        ctl.Tick(heavy,g);
        CHECK(ctl.GetStats().promotions==0);
        CHECK(ctl.GetStats().state==Pressure::Critical);
        const auto before=g.requests.size();
        auto mid=snap(400);mid.streamingUsed=204*1048576ULL;
        ctl.Tick(mid,g);
        CHECK(ctl.GetStats().state==Pressure::Critical);
        CHECK(before==g.requests.size());
        ctl.Tick(snap(500),g);
        CHECK(ctl.GetStats().state==Pressure::Normal);

        // Teleports suppress the current tick, invalidate provenance.
        ctl.Tick(snap(600,{700,700,5}),g);
        CHECK(ctl.GetStats().teleported);
        CHECK(!ctl.IsTracked(5));

        // Passive memory guards; no eviction or budget changes.
        ctl.Reset();g.pending.clear();g.requests.clear();
        auto low=snap(1000);low.privateBytes=1700*1048576ULL;
        ctl.Tick(low,g);
        CHECK(ctl.GetStats().memoryGuard&&g.requests.empty());
        ctl.Reset();

        // Slow frames suspend speculative requests; moderate frames permit one.
        auto slow=snap(1500);slow.recentFrameMs=35;
        ctl.Tick(slow,g);
        CHECK(ctl.GetStats().adaptiveQuota==0);
        auto caution=snap(1700,{1,0,5});caution.recentFrameMs=19;
        ctl.Tick(caution,g);
        CHECK(ctl.GetStats().adaptiveQuota==1);
        CHECK(g.requests.size()==1);

        // Refresh of an already tracked model MUST happen even if quota=0.
        ctl.Reset();g.requests.clear();g.pending.clear();
        g.candidates={{11,.9f},{12,.8f},{13,.7f}};
        g.valid.insert(11);g.valid.insert(12);g.valid.insert(13);
        ctl.Tick(snap(2000),g);
        CHECK(ctl.IsTracked(11)); // before refresh loop
        // Every tick, candidate 11 is still wanted; backpressure blocks new.
        for(uint32_t t=2100;t<=6200;t+=100){
            auto s=snap(t);s.recentFrameMs=35;
            ctl.Tick(s,g);
        }
        CHECK(ctl.IsTracked(11));
        CHECK(ctl.GetStats().expired==0);

        // Stale entries eventually expire, with cooldown preventing churn.
        g.candidates.clear();
        ctl.Tick(snap(6300),g);
        CHECK(ctl.IsTracked(11));
        g.pending.clear(); // engine has finished or cancelled the old requests
        ctl.Tick(snap(9900),g);
        CHECK(!ctl.IsTracked(11));
        CHECK(ctl.GetStats().expired>=1);

        // Stale but still queued speculation continues to consume the cap.
        Config bounded;
        bounded.maxOutstanding=4;
        bounded.expiryMs=100;
        Controller pending(bounded);
        Fake backlog;
        backlog.candidates={{20,1},{21,.9f},{22,.8f},{23,.7f}};
        backlog.valid={20,21,22,23,24};
        pending.Tick(snap(100),backlog);
        CHECK(backlog.requests.size()==4);
        backlog.candidates={{24,1}};
        pending.Tick(snap(400),backlog);
        CHECK(backlog.requests.size()==4&&pending.GetStats().outstanding==4);
        backlog.pending.erase(20);
        pending.Tick(snap(500),backlog);
        CHECK(backlog.requests.size()==5&&backlog.requests.back()==24);

        // No prefetch in interiors/cutscenes/unsafe moments.
        ctl.Reset();g.requests.clear();g.pending.clear();
        auto inside=snap(12000);inside.area=1;
        ctl.Tick(inside,g);
        CHECK(g.requests.empty());
        auto unsafe=snap(12100);unsafe.gameplaySafe=false;
        ctl.Tick(unsafe,g);
        CHECK(g.requests.empty());

        // Unsigned tick rollover remains safe for cadence and expiry.
        ctl.Reset();g.candidates={{9,1.0f}};g.valid.insert(9);
        ctl.Tick(snap(UINT32_MAX-50),g);
        ctl.Tick(snap(80,{1,0,5}),g);
        CHECK(ctl.GetStats().cells!=0);

        // Experimental local dynamic readiness is opt-in and bounded.
        Config dynCfg{};
        dynCfg.dynamicWarmup=true;
        dynCfg.maxAdaptiveRequests=1;
        Controller dyn(dynCfg);
        Fake traffic;
        traffic.valid.insert(700);
        dyn.OnDynamicVanillaDemand(700,1000,{0,0,5});
        dyn.OnDynamicVanillaDemand(700,1450,{1,0,5});
        dyn.Tick(snap(1600),traffic);
        CHECK(traffic.requests.size()==1&&traffic.requests[0]==700);
        dyn.Reset();
        traffic.requests.clear();traffic.pending.clear();
        dyn.OnDynamicVanillaDemand(700,1200,{900,900,5});
        dyn.Tick(snap(1300),traffic);
        CHECK(traffic.requests.empty()); // no cross-city warming

        std::cout<<"PASS: sector capsule, admission, promotion, pressure,"
                   " expiry, rollover, frame control, safety guards\n";
        return 0;
    }catch(const std::exception&e){
        std::cerr<<"FAIL: "<<e.what()<<'\n';
        return 1;
    }
}
