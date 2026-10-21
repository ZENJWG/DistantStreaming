#include "asc/Core.h"
#include <iostream>
#include <stdexcept>
#include <unordered_set>
using namespace asc;
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(0)

struct FakeGame : IGame {
    std::unordered_set<int> valid,loaded,pending;
    std::vector<int> requests;
    std::vector<Candidate> Collect(const std::vector<Cell>&,Vec3,Vec3,
         Vec3,Vec3,bool,unsigned) override{return {};}
    bool CanRequest(int id)const override{return valid.contains(id);}
    bool IsLoaded(int id)const override{return loaded.contains(id);}
    bool IsStillRequested(int id)const override{return pending.contains(id);}
    void RequestDefault(int id) override {
        requests.push_back(id);
        pending.insert(id);
    }
};
static Snapshot sample(uint32_t now,Vec3 pos={0,0,5}) {
    Snapshot s{};
    s.nowMs=now;s.player=pos;s.playerValid=true;s.gameplaySafe=true;
    s.streamingUsed=10*1048576ULL;s.streamingBudget=256*1048576ULL;
    s.availablePhysicalBytes=4000*1048576ULL;
    s.privateBytes=400*1048576ULL;s.workingSetBytes=450*1048576ULL;
    return s;
}
int main(){
    try{
        Config c{};
        c.prefetch=false;  // DynamicReadiness works independently.
        c.dynamicWarmup=true;
        c.dynamicMinObservations=2;
        c.dynamicSlotsPerTick=1;
        c.maxRequestsPerTick=3;c.maxAdaptiveRequests=3;
        c.dynamicHistoryMs=10000;
        c.dynamicRewarmCooldownMs=15000;
        CHECK(Controller::Valid(c));
        Controller a(c);
        FakeGame g;
        g.valid={600,601,602,603};

        a.OnDynamicVanillaDemand(600,1000,{0,0,5});
        a.Tick(sample(1100),g);
        CHECK(g.requests.empty()); // single observation cannot trigger

        a.OnDynamicVanillaDemand(600,1500,{0,0,5});
        a.OnDynamicVanillaDemand(601,1200,{2,0,5});
        a.OnDynamicVanillaDemand(601,1600,{2,0,5});
        a.Tick(sample(1700,{1,0,5}),g);
        CHECK(g.requests.size()==1); // max one dynamic per tick
        CHECK(a.GetStats().dynamicEligible==2);
        CHECK(a.GetStats().dynamicIssued==1);
        a.Tick(sample(1800,{2,0,5}),g);
        CHECK(g.requests.size()==2);
        CHECK(g.requests[0]!=g.requests[1]);

        // A completed local warmup must not be reissued repeatedly.
        g.pending.clear();g.loaded.clear();
        for(uint32_t now=1900;now<=2300;now+=100){
            a.Tick(sample(now,{2,0,5}),g);
        }
        CHECK(g.requests.size()==2);

        // Newly observed distant demand must qualify separately.
        a.OnDynamicVanillaDemand(602,2400,{500,500,5});
        a.OnDynamicVanillaDemand(602,2800,{510,510,5});
        a.Tick(sample(3000,{3,0,5}),g);
        CHECK(g.requests.size()==2);

        // Mission/flag filtering occurs in game adapter; controller only
        // admits classifications passed through OnDynamicVanillaDemand.
        // Old local traffic demand ages out after HistoryMs.
        a.OnDynamicVanillaDemand(603,3300,{0,0,5});
        a.Tick(sample(14500,{3,0,5}),g);
        CHECK(g.requests.size()==2);

        // Streaming memory pressure always stops speculative traffic.
        a.OnDynamicVanillaDemand(603,15500,{0,0,5});
        a.OnDynamicVanillaDemand(603,15900,{0,0,5});
        auto pressured=sample(16100,{3,0,5});
        pressured.streamingUsed=240*1048576ULL;
        a.Tick(pressured,g);
        CHECK(g.requests.size()==2);
        CHECK(a.GetStats().adaptiveQuota==0);

        // Backpressure cleared; model becomes eligible.
        a.Tick(sample(16200,{3,0,5}),g);
        CHECK(g.requests.size()==3&&g.requests.back()==603);

        // Rewarming respects a longer per-model guard than the generic
        // request-ledger expiry/cooldown, even after the model unloads.
        Config guarded=c;
        guarded.expiryMs=200;
        guarded.cooldownMs=200;
        guarded.dynamicRewarmCooldownMs=15000;
        Controller b(guarded);
        FakeGame q;
        q.valid.insert(610);
        b.OnDynamicVanillaDemand(610,1000,{0,0,5});
        b.OnDynamicVanillaDemand(610,1500,{0,0,5});
        b.Tick(sample(1600),q);
        CHECK(q.requests.size()==1);
        q.pending.clear();
        b.Tick(sample(1900),q);
        b.OnDynamicVanillaDemand(610,2200,{0,0,5});
        b.Tick(sample(2300),q);
        CHECK(q.requests.size()==1);
        // After 15 seconds, fresh repeated demand makes rewarming legal.
        b.OnDynamicVanillaDemand(610,17000,{0,0,5});
        b.OnDynamicVanillaDemand(610,17400,{0,0,5});
        b.Tick(sample(17500),q);
        CHECK(q.requests.size()==2);

        // Expiring learning confidence must not erase the rewarm cooldown.
        guarded.dynamicHistoryMs=1000;
        Controller shortHistory(guarded);
        FakeGame shortGame;
        shortGame.valid.insert(611);
        shortHistory.OnDynamicVanillaDemand(611,1000,{0,0,5});
        shortHistory.OnDynamicVanillaDemand(611,1400,{0,0,5});
        shortHistory.Tick(sample(1500),shortGame);
        CHECK(shortGame.requests.size()==1);
        shortGame.pending.clear();
        shortHistory.Tick(sample(1800),shortGame);
        shortHistory.OnDynamicVanillaDemand(611,3000,{0,0,5});
        shortHistory.OnDynamicVanillaDemand(611,3400,{0,0,5});
        shortHistory.Tick(sample(3500),shortGame);
        CHECK(shortGame.requests.size()==1);

        // Out-of-range input cannot enter dynamic learning table.
        a.OnDynamicVanillaDemand(-1,16300,{0,0,5});
        a.OnDynamicVanillaDemand(22000,16300,{0,0,5});
        std::cout<<"PASS: qualified local warming, independent scheduling, "
                    "hard cap, cooldown, location and pressure\n";
    } catch(const std::exception& e){
        std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;
    }
}
