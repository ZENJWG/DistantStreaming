#include "asc/CameraReadiness.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace asc;
using namespace asc::camera;
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(0)
int main(){
    try {
        Settings cfg;
        CHECK(!cfg.blocking);
        CHECK(cfg.maxScanCells==64 && cfg.maxScanNodes==3072);
        auto badScan=cfg;badScan.maxScanCells=1;CHECK(!Valid(badScan));
        badScan=cfg;badScan.maxScanUs=100;CHECK(!Valid(badScan));
        badScan=cfg;badScan.maxScanCells=16;badScan.maxScanNodes=768;
        badScan.maxScanUs=1500;CHECK(Valid(badScan));
        cfg.blocking=true;
        CHECK(Valid(cfg));
        cfg.maxModels=4;cfg.maxBatchSectors=8192; // exercise small-batch boundaries
        auto bad=cfg;bad.maxModels=9;CHECK(!Valid(bad));
        bad=cfg;bad.intervalMs=0;CHECK(!Valid(bad));
        bad=cfg;bad.range=std::numeric_limits<float>::quiet_NaN();CHECK(!Valid(bad));
        CHECK(InView({100,0,0},{0,0,0},{1,0,0},300,.5f));
        CHECK(InView({100,0,0},{0,0,0},{2,0,0},300,.5f));
        CHECK(!InView({-100,0,0},{0,0,0},{1,0,0},300,.5f));
        CHECK(!InView({10,100,0},{0,0,0},{1,0,0},300,.5f));
        CHECK(!InView({400,0,0},{0,0,0},{1,0,0},300,.5f));
        CHECK(!InView({10,0,0},{0,0,0},{0,0,0},300,.5f));
        CHECK(!InView({NAN,0,0},{0,0,0},{1,0,0},300,.5f));

        std::vector<Target> targets={{5,100,100,false,true}, // already priority: still needs flush
            {2,25,100,false,false},{2,25,100,false,false}, // dedupe
            {8,1,100,true,false}, // loaded: no I/O
            {9,50,100,false,false},{10,200,100,false,false},
            {11,300,100,false,false},{-1,0,100,false,false}};
        auto batch=Plan(targets,cfg);
        CHECK(batch.selected==4&&batch.requests==3&&batch.needsFlush);
        CHECK(batch.requestIds[0]==2&&batch.requestIds[1]==9&&batch.requestIds[2]==10);
        CHECK(batch.selectedIds[2]==5); // already-priority models still prepare TXDs
        std::vector<int> operations;
        Execute(batch,true,[&](int id){operations.push_back(id);},[&]{operations.push_back(-100);});
        CHECK(operations.size()==4&&operations.back()==-100);
        operations.clear();
        Execute(batch,false,[&](int id){operations.push_back(id);},[&]{throw std::runtime_error("unexpected flush");});
        CHECK(operations.size()==3);
        std::vector<int> textures;
        TextureDependencies(3,[](int slot){return slot==3?2:slot==2?1:-1;},
            [&](int slot){textures.push_back(slot);});
        CHECK((textures==std::vector<int>{3,2,1}));
        textures.clear();
        TextureDependencies(3,[](int){return 3;},[&](int slot){textures.push_back(slot);});
        CHECK(textures.size()==1); // corrupt parent cycle cannot spin
        textures.clear();
        TextureDependencies(4999,[](int){return 5000;},[&](int slot){textures.push_back(slot);});
        CHECK(textures.size()==1);
        textures.clear();
        TextureDependencies(1,[](int){return -2;},[&](int slot){textures.push_back(slot);});
        CHECK(textures.empty());
        targets={{5,10,100,false,true}};
        batch=Plan(targets,cfg);CHECK(batch.requests==0&&batch.needsFlush);
        unsigned flushes=0;
        Execute(batch,true,[](int){throw std::runtime_error("already priority requested twice");},[&]{++flushes;});
        CHECK(flushes==1);
        targets.clear();batch=Plan(targets,cfg);
        CHECK(!batch.needsFlush&&batch.selected==0);
        Execute(batch,true,[](int){},[&]{throw std::runtime_error("empty flush");});
        targets={{1,1,10000,false,false},{2,2,1,false,false}};
        batch=Plan(targets,cfg);CHECK(batch.selected==1&&batch.requestIds[0]==1);
        targets={{1,1,6000,false,false},{2,2,6000,false,false},{3,3,100,false,false}};
        batch=Plan(targets,cfg);CHECK(batch.selected==2&&batch.requestIds[1]==3);
        cfg.enabled=false;CHECK(!Plan(targets,cfg).needsFlush);cfg.enabled=true;

        Cadence cadence;
        CHECK(cadence.Due(0,250));cadence.Complete(0);
        CHECK(!cadence.Due(249,250)&&cadence.Due(250,250));
        cadence.Complete(UINT32_MAX-100);
        CHECK(!cadence.Due(50,250)&&cadence.Due(150,250));

        Config core;
        CHECK(MayRequest(false,false,47,48));
        CHECK(!MayRequest(false,false,48,48));
        CHECK(!MayRequest(false,false,49,48));
        CHECK(MayRequest(false,true,49,48)); // promotion consumes no slot
        CHECK(MayRequest(true,false,49,48)); // explicit blocking catch-up
        Snapshot s;
        s.gameplaySafe=s.playerValid=s.cameraValid=true;
        s.streamingBudget=256*1048576ULL;s.streamingUsed=50*1048576ULL;
        s.availablePhysicalBytes=1024*1048576ULL;
        CHECK(Allowed(s,core,cfg));
        s.gameRequests=500;CHECK(Allowed(s,core,cfg)); // queued work can be caught up
        cfg.blocking=false;CHECK(!Allowed(s,core,cfg));cfg.blocking=true;
        s.gameRequests=0;s.addressSpaceLow=true;CHECK(!Allowed(s,core,cfg));s.addressSpaceLow=false;
        s.streamingUsed=250*1048576ULL;CHECK(!Allowed(s,core,cfg));s.streamingUsed=50*1048576ULL;
        s.area=1;CHECK(!Allowed(s,core,cfg));s.area=0;
        s.gameplaySafe=false;CHECK(!Allowed(s,core,cfg));s.gameplaySafe=true;
        s.availablePhysicalBytes=64*1048576ULL;CHECK(!Allowed(s,core,cfg));
        std::cout<<"PASS: camera geometry, pending priority catch-up, bounded nearest batches, blocking sequencing, async switch, cooldown rollover and memory/gameplay guards\n";
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
