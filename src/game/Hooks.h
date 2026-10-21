#pragma once
#include <cstdint>
#include <array>
#include "asc/RequestCallProfile.h"
#include <ostream>
#include "asc/RequestObservations.h"
namespace hooks {
using RequestRecord=asc::RequestObservation;
struct ArchiveProfileOptions {
    bool enabled=false;
    unsigned sampleEvery=16;   // power of two; bounded observation cost
    unsigned slowThresholdUs=1000;
    unsigned hookMode=1; // 1=request only, 2=request+load
};
struct Counters {
    uint64_t requestCalls=0, requestLost=0;
    uint64_t nativeRequestCalls=0,pluginRequestCalls=0,nonMainNativeRequests=0;
    uint64_t requestCoalesced=0,requestIgnored=0;
    uint64_t loadCalls=0,loadTotalUs=0,loadPeakUs=0;
    uint64_t archiveChoices=0,archiveSamples=0,archiveSelectionUs=0;
    uint64_t archivePeakUs=0,archiveSlow=0,archiveNoFile=0;
    // Approximate log-scale latency histogram, sample-only.
    uint64_t archiveBins[8]{};
};
// Main-thread samples only. A fixed 1/128 sampling rate avoids expensive
// per-call stack inspection or engine-state reads in the hot hook.
struct RequestProfileBatch {
    std::array<asc::request_profile::Sample,1024> samples{};
    uint32_t count{},dropped{};
    uint64_t hookTicks{},originalTicks{},hookPeakTicks{},originalPeakTicks{},timed{};
};
RequestProfileBatch TakeRequestProfileBatch();
bool Install(ArchiveProfileOptions options);
void Uninstall();
void EnterPluginRequest();
void ExitPluginRequest();
bool IsInstalled();
// Verify the Request entry still points at our trampoline. If a third party
// restores pristine bytes, optionally re-arm; never overwrite foreign detours.
bool CheckIntegrity(bool recoverPristine, std::ostream& log);
const char* InstallReport();
bool IsArchiveProfilerActive();
Counters CollectCounters();
void DrainVanillaRequests(void(*callback)(const RequestRecord&,void*),void* ctx);
bool RequestedRecently(int id,uint32_t now,unsigned quietMs);
}
