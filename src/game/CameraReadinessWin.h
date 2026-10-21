#pragma once
#include "asc/CameraReadiness.h"
#include "asc/MotionScheduling.h"
#include <ostream>

class CameraReadiness {
public:
    struct Counters {
        uint64_t requests=0,flushes=0,blockingUs=0,peakBlockingUs=0,instances=0;
    };
    void Configure(asc::camera::Settings,std::ostream&);
    bool Tick(const asc::Snapshot&,const asc::Config&,
              asc::motion::RequestBudget* budget=nullptr,unsigned fastIntervalMs=0);
    Counters TakeCounters(){auto out=counters_;counters_={};return out;}
private:
    asc::camera::Settings cfg_{};
    asc::camera::Cadence cadence_;
    asc::camera::Cadence instanceCadence_;
    std::vector<asc::camera::Target> targets_;
    std::vector<asc::Cell> cells_;
    Counters counters_{};
    bool canFlush_=false,canInstance_=false,inTick_=false;
    float cosine_=0;
    int64_t qpcFrequency_=0;
};
