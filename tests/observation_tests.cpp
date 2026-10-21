#include "asc/RequestObservations.h"
#include "asc/Core.h"
#include <array>
#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
using asc::ObservationResult;
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(0)
struct Traffic : asc::IGame {
    unsigned requests=0;
    bool pending=false;
    std::vector<asc::Candidate> Collect(const std::vector<asc::Cell>&,
        asc::Vec3,asc::Vec3,asc::Vec3,asc::Vec3,bool,unsigned)override{return {};}
    bool CanRequest(int id)const override{return id==600;}
    bool IsLoaded(int)const override{return false;}
    bool IsStillRequested(int)const override{return pending;}
    void RequestDefault(int)override{++requests;pending=true;}
};
int main(){
    try {
        asc::RequestObservations<64> small;
        CHECK(small.Observe(-1,0,0,true)==ObservationResult::Ignored);
        CHECK(small.Observe(64,0,0,true)==ObservationResult::Ignored);
        CHECK(small.Observe(1,0,0,true)==ObservationResult::Recorded);
        CHECK(small.Observe(1,4,50,true)==ObservationResult::Coalesced);
        CHECK(small.Observe(1,8,60,true)==ObservationResult::Coalesced);
        CHECK(small.Observe(1,16,80,false)==ObservationResult::Recorded);
        unsigned main=0,worker=0;
        CHECK(small.Drain([&](const asc::RequestObservation& r){
            CHECK(r.id==1);
            if(r.onMainThread){CHECK(r.requestedAt==0&&r.flags==12);++main;}
            else {CHECK(r.requestedAt==80&&r.flags==16);++worker;}
        })==2);
        CHECK(main==1&&worker==1);
        CHECK(small.Drain([](const auto&){})==0);
        CHECK(small.Observe(1,0,UINT32_MAX,true)==ObservationResult::Recorded);
        small.Observe(1,1,5,true);
        small.Drain([](const auto& r){CHECK(r.requestedAt==UINT32_MAX&&r.flags==1);});

        // Match the run's 14 million calls and its 110,721-call peak burst.
        auto flood=std::make_unique<asc::RequestObservations<20000>>();
        unsigned merged=0;
        for(unsigned i=0;i<14000000;++i){
            const auto result=flood->Observe(int(i%128),int(i&255),100,true);
            merged+=result==ObservationResult::Coalesced;
        }
        CHECK(merged==14000000-128);
        CHECK(flood->Drain([](const auto& r){CHECK(r.requestedAt==100);})==128);

        // All IDs in both lanes can be pending: no event-capacity loss.
        for(int id=0;id<20000;++id){
            CHECK(flood->Observe(id,1,200,false)==ObservationResult::Recorded);
            CHECK(flood->Observe(id,2,250,true)==ObservationResult::Recorded);
        }
        unsigned delivered=0;
        std::vector<unsigned char> seen(40000);
        for(;;){
            const auto n=flood->Drain([&](const auto& r){
                const unsigned index=unsigned(r.id)*2+unsigned(r.onMainThread);
                CHECK(!seen[index]);seen[index]=1;
                CHECK(r.flags==(r.onMainThread?2:1));
                ++delivered;
            },1024);
            CHECK(n<=1024);
            if(!n)break;
        }
        CHECK(delivered==40000);
        CHECK(flood->Drain([](const auto&){},0)==0);

        // Rotating scan remains fair when a low ID is constantly refilled.
        small.Reset();small.Observe(63,2,0,true);
        bool highSeen=false;
        for(unsigned i=0;i<8;++i){
            small.Observe(0,1,i,true);
            small.Drain([&](const auto& r){if(r.id==63)highSeen=true;},1);
        }
        CHECK(highSeen);

        // An observed-scale storm can feed confidence learning across ticks
        // without erasing controller history or repeatedly warming the model.
        asc::Config cfg;cfg.prefetch=false;cfg.dynamicWarmup=true;
        asc::Controller controller(cfg);
        Traffic traffic;
        for(uint32_t now=100;now<=700;now+=100){
            for(unsigned n=0;n<110721;++n)
                flood->Observe(600,0,now,true);
            CHECK(flood->Drain([&](const auto& r){
                controller.OnVanillaRequest(r.id,r.flags);
                controller.OnDynamicVanillaDemand(r.id,r.requestedAt,{0,0,0});
            })==1);
            asc::Snapshot state;
            state.nowMs=now;state.playerValid=state.gameplaySafe=true;
            state.streamingBudget=256*1048576ULL;
            controller.Tick(state,traffic);
        }
        CHECK(traffic.requests==1);

        // Concurrent flag merges and consume/republish races preserve demand.
        asc::RequestObservations<512> concurrent;
        std::array<std::thread,4> threads;
        std::atomic<unsigned> finished{0};
        std::array<unsigned,1024> observed{};
        for(unsigned p=0;p<threads.size();++p){
            threads[p]=std::thread([&,p]{
                for(unsigned i=0;i<200000;++i)
                    concurrent.Observe(int(i%512),1<<p,i,(p&1)!=0);
                finished.fetch_add(1,std::memory_order_release);
            });
        }
        auto consume=[&](const auto& r){
            observed[unsigned(r.id)*2+unsigned(r.onMainThread)]|=unsigned(r.flags);
        };
        while(finished.load(std::memory_order_acquire)!=4){
            concurrent.Drain(consume,73);
            std::this_thread::yield();
        }
        for(auto& thread:threads)thread.join();
        while(concurrent.Drain(consume,73)){}
        for(unsigned id=0;id<512;++id){
            CHECK(observed[id*2]==5);
            CHECK(observed[id*2+1]==10);
        }
        std::cout<<"PASS: 14M-repeat flood, full model domain, flags, provenance, timestamps, fair bounded drain, concurrency\n";
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
