#include "Telemetry.h"
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <sstream>

namespace {
double mib(uint64_t bytes){return double(bytes)/1048576.0;}
double percentile(std::vector<double>& v,float fraction){
    if(v.empty())return 0.0;
    const size_t at=std::min(v.size()-1,
       size_t(std::floor((v.size()-1)*fraction)));
    std::nth_element(v.begin(),v.begin()+at,v.end());
    return v[at];
}
void writeAll(HANDLE file,const std::string&s){
    size_t offset=0;
    while(offset<s.size()){
        DWORD wrote=0;
        const DWORD n=DWORD(std::min<size_t>(s.size()-offset,65536));
        if(!WriteFile(file,s.data()+offset,n,&wrote,nullptr)||wrote==0)break;
        offset+=wrote;
    }
}
constexpr const char* kHeader=
    "ms,stream_mib,stream_budget_mib,private_mib,working_mib,"
    "physical_available_mib,vanilla_backlog,pressure,quota,issued,outstanding,"
    "sector_count,candidates,promotions,expired,denied,loaded,"
    "hook_requests,lost_events,load_calls,load_total_us,load_peak_us,"
    "archive_calls,archive_samples,archive_sample_total_us,"
    "archive_peak_us,archive_slow_samples,archive_no_file_samples,"
    "archive_lt25us,archive_lt50us,archive_lt100us,archive_lt250us,"
    "archive_lt500us,archive_lt1000us,archive_lt2500us,archive_ge2500us,"
    "dynamic_eligible,dynamic_issued,dynamic_deferred,"
    "ready,ready_ped,ready_vehicle,"
    "readiness_p50_ms,readiness_p95_ms,queue_p95_ms,ready_timeouts,"
    "frame_p50_ms,frame_p95_ms,frame_p99_ms,dropped_log_lines,"
    "exe_laa,virtual_total_mib,virtual_available_mib,address_space_guard,"
    "coalesced_requests,ignored_resource_requests,world_priority_issued,"
    "priority_backlog,readiness_pending,readiness_max_pending_ms,frame_guard,memory_guard,"
    "camera_requests,camera_flushes,camera_block_total_us,camera_block_peak_us,camera_instances,"
    "cleanup_scanned,cleanup_instances,cleanup_models,cleanup_textures,cleanup_stream_bytes,"
    "cleanup_total_us,cleanup_peak_us,cleanup_budget_stops\n";
}
bool Telemetry::Start(const std::string&dir){
    if(thread_)return true;
    const std::string path=dir+"\\DistantStreaming.csv";
    file_=CreateFileA(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,
                     nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file_==INVALID_HANDLE_VALUE)return false;
    event_=CreateEventA(nullptr,FALSE,FALSE,nullptr);
    if(!event_){CloseHandle(file_);file_=INVALID_HANDLE_VALUE;return false;}
    stopping_=false;
    writeAll(file_,kHeader);
    LARGE_INTEGER qpf{};
    QueryPerformanceFrequency(&qpf);
    qpcFrequency_=qpf.QuadPart;
    frameTimes_.reserve(512);
    thread_=CreateThread(nullptr,0,&Telemetry::WorkerEntry,this,0,nullptr);
    if(!thread_){
        CloseHandle(event_);event_=nullptr;
        CloseHandle(file_);file_=INVALID_HANDLE_VALUE;
        return false;
    }
    return true;
}
void Telemetry::Frame(){
    if(qpcFrequency_<=0){
        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);
        qpcFrequency_=freq.QuadPart;
        if(qpcFrequency_<=0)return;
    }
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    if(prevQpc_&&qpc.QuadPart>prevQpc_){
        const double ms=1000.0*double(qpc.QuadPart-prevQpc_)/qpcFrequency_;
        // No automatic dismissal of long frames; 250ms is retained for p99.
        if(std::isfinite(ms)&&ms>0){
            // A multi-second stall must also trigger admission backoff.
            // Bound its recovery weight so loading/pause transitions do not
            // suppress optional work for an excessive number of frames.
            const float guardMs=float(std::min(ms,2000.0));
            recentFrameMs_=recentFrameMs_==0?guardMs:
                0.2f*guardMs+0.8f*recentFrameMs_;
            if(thread_&&frameTimes_.size()<4096)frameTimes_.push_back(ms);
        }
    }
    prevQpc_=qpc.QuadPart;
}
void Telemetry::Sample(uint32_t now,const asc::Snapshot&s,
                       const asc::Stats&st,const hooks::Counters&c,
                       const readiness::Summary&r,const CameraReadiness::Counters& camera,
                       const OffscreenCleanup::Counters& cleanup){
    if(!thread_||uint32_t(now-lastSample_)<1000)return;
    lastSample_=now;
    std::ostringstream row;
    row<<std::fixed<<std::setprecision(2)
       <<now<<','<<mib(s.streamingUsed)<<','<<mib(s.streamingBudget)
       <<','<<mib(s.privateBytes)<<','<<mib(s.workingSetBytes)
       <<','<<mib(s.availablePhysicalBytes)
       <<','<<s.gameRequests<<','<<unsigned(st.state)
       <<','<<st.adaptiveQuota<<','<<st.issued
       <<','<<st.outstanding<<','<<st.cells<<','<<st.candidates
       <<','<<st.promotions<<','<<st.expired
       <<','<<st.denied<<','<<st.loaded<<','
       <<c.requestCalls<<','<<c.requestLost<<','<<c.loadCalls
       <<','<<c.loadTotalUs<<','<<c.loadPeakUs<<','
       <<c.archiveChoices<<','<<c.archiveSamples<<','
       <<c.archiveSelectionUs<<','<<c.archivePeakUs<<','
       <<c.archiveSlow<<','<<c.archiveNoFile<<','
       <<c.archiveBins[0]<<','<<c.archiveBins[1]<<','
       <<c.archiveBins[2]<<','<<c.archiveBins[3]<<','
       <<c.archiveBins[4]<<','<<c.archiveBins[5]<<','
       <<c.archiveBins[6]<<','<<c.archiveBins[7]<<','
       <<st.dynamicEligible<<','<<st.dynamicIssued<<','
       <<st.dynamicDeferred<<','
       <<r.loaded<<','<<r.pedReady<<','<<r.vehicleReady<<','
       <<r.p50Ms<<','<<r.p95Ms<<','<<r.queueP95Ms<<','
       <<r.timedOut<<','
       <<percentile(frameTimes_,.50f)<<','
       <<percentile(frameTimes_,.95f)<<','
       <<percentile(frameTimes_,.99f)<<','
       <<droppedLines_<<','<<unsigned(s.largeAddressAware)
       <<','<<mib(s.totalVirtualBytes)<<','<<mib(s.availableVirtualBytes)
       <<','<<unsigned(s.addressSpaceLow)<<','<<c.requestCoalesced
       <<','<<c.requestIgnored<<','<<st.priorityIssued<<','<<s.gamePriorityRequests
       <<','<<r.pending<<','<<r.maxPendingMs<<','<<unsigned(st.frameGuard)
       <<','<<unsigned(st.memoryGuard)<<','<<camera.requests<<','<<camera.flushes
       <<','<<camera.blockingUs<<','<<camera.peakBlockingUs<<','<<camera.instances
       <<','<<cleanup.scanned<<','<<cleanup.instances<<','<<cleanup.models<<','<<cleanup.textures
       <<','<<cleanup.streamBytes<<','<<cleanup.totalUs<<','<<cleanup.peakUs<<','<<cleanup.budgetStops<<'\n';
    frameTimes_.clear();
    // A single short try-lock per second, not file I/O on the game thread.
    std::unique_lock<std::mutex> guard(mutex_,std::try_to_lock);
    if(!guard.owns_lock()||lines_.size()>=128){++droppedLines_;return;}
    lines_.push_back(row.str());
    guard.unlock();
    SetEvent(event_);
}
DWORD WINAPI Telemetry::WorkerEntry(void*ctx){
    static_cast<Telemetry*>(ctx)->Worker();
    return 0;
}
void Telemetry::Worker(){
    for(;;){
        WaitForSingleObject(event_,2000);
        std::deque<std::string> pending;
        bool stop=false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending.swap(lines_);
            stop=stopping_;
        }
        for(const auto&line:pending)writeAll(file_,line);
        if(stop)break;
    }
    FlushFileBuffers(file_);
}
void Telemetry::Stop(){
    if(!thread_)return;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        stopping_=true;
    }
    SetEvent(event_);
    WaitForSingleObject(thread_,INFINITE); // ONLY from game shutdown, never DllMain.
    CloseHandle(thread_);thread_=nullptr;
    CloseHandle(event_);event_=nullptr;
    CloseHandle(file_);file_=INVALID_HANDLE_VALUE;
}
