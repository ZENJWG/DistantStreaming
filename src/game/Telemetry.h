#pragma once
#include "asc/Core.h"
#include "Hooks.h"
#include "Readiness.h"
#include "CameraReadinessWin.h"
#include "OffscreenCleanupWin.h"
#include <windows.h>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

class Telemetry {
public:
    bool Start(const std::string& directory);
    void Frame();
    float RecentFrameMs() const {return recentFrameMs_;}
    void Sample(uint32_t now,const asc::Snapshot&,const asc::Stats&,
                const hooks::Counters&,const readiness::Summary&,
                const CameraReadiness::Counters&,const OffscreenCleanup::Counters&);
    void Stop();
    ~Telemetry(){Stop();}
private:
    static DWORD WINAPI WorkerEntry(void*);
    void Worker();
    HANDLE thread_=nullptr,event_=nullptr,file_=INVALID_HANDLE_VALUE;
    std::mutex mutex_;
    std::deque<std::string> lines_;
    bool stopping_=false;
    uint64_t droppedLines_=0;
    int64_t prevQpc_=0,qpcFrequency_=0;
    uint32_t lastSample_=0;
    float recentFrameMs_=0;
    std::vector<double> frameTimes_;
};
