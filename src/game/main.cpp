#include <windows.h>
#include <psapi.h>
#include <plugin.h>
#include <CStreaming.h>
#include <CStreamingInfo.h>
#include <CWorld.h>
#include <CModelInfo.h>
#include <CPlayerPed.h>
#include <CTimer.h>
#include <CCutsceneMgr.h>
#include <CCamera.h>
#include "asc/Core.h"
#include "asc/CatalogNativePrefetch.h"
#include "asc/StreamingFlowProbe.h"
#include "asc/RequestCallProfile.h"
#include <algorithm>
#include "asc/StatsWindow.h"
#include "GameAdapter.h"
#include "WorldListSafety.h"
#include "OlaBindingsWin.h"
#include "Hooks.h"
#include "ConfigWin.h"
#include "TargetValidation.h"
#include "Telemetry.h"
#include "Readiness.h"
#include "StreamingMemoryWin.h"
#include "CameraReadinessWin.h"
#include "OffscreenCleanupWin.h"
#include "GpuResidencyWin.h"
#include "asc/ArchivePreload.h"
#include "AsyncAssetCacheWin.h"
#include "AsyncLog.h"
#include <memory>
#include <array>
#include <cstring>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <d3d9.h>

namespace {
// Module attribution runs only for the top sampled callers once per second.
// Never call loader/module APIs from the RequestModel detour.
std::string DescribeRequestCaller(uintptr_t address) {
    if(!address)return "unknown";
    HMODULE module=nullptr;
    if(GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(address),&module) && module) {
        char filename[MAX_PATH]{};
        const DWORD length=GetModuleFileNameA(module,filename,MAX_PATH);
        if(length && length<MAX_PATH) {
            const std::string full(filename,length);
            const auto separator=full.find_last_of("\\/");
            const std::string base=separator==std::string::npos
                ? full:full.substr(separator+1);
            std::ostringstream out;
            out<<base<<"+0x"<<std::hex
               <<(address-reinterpret_cast<uintptr_t>(module))<<std::dec;
            return out.str();
        }
    }
    MEMORY_BASIC_INFORMATION info{};
    if(VirtualQuery(reinterpret_cast<const void*>(address),&info,
                    sizeof(info))==sizeof(info)) {
        std::ostringstream out;
        out<<(info.State==MEM_COMMIT?"committed":"uncommitted")
           <<(info.Type==MEM_IMAGE?" image":
              info.Type==MEM_MAPPED?" mapped":
              info.Type==MEM_PRIVATE?" private":" unknown")
           <<" base=0x"<<std::hex
           <<reinterpret_cast<uintptr_t>(info.AllocationBase)<<std::dec
           <<" protect=0x"<<std::hex<<info.Protect<<std::dec;
        return out.str();
    }
    return "unmapped";
}
struct ScopedFrameCallback {
    bool drawing;
    uint64_t startedUs;
    explicit ScopedFrameCallback(bool isDrawing)
        :drawing(isDrawing),startedUs(gpu_residency::BeginCallbackSample()) {}
    ~ScopedFrameCallback() {gpu_residency::EndCallbackSample(drawing,startedUs);}
};
struct ScopedRenderStage {
    asc::render_cost::Stage stage;
    uint64_t startedUs;
    explicit ScopedRenderStage(asc::render_cost::Stage value)
        :stage(value),startedUs(gpu_residency::BeginCallbackSample()) {}
    ~ScopedRenderStage(){gpu_residency::EndRenderStage(stage,startedUs);}
};
struct Runtime {
    bool started=false,operational=false,telemetryEnabled=false;
    bool memoryPrepared=false,targetAccepted=false;
    bool logOpened=false;
    uint32_t lastTick=0,lastSample=0,lastHookCheck=0;
    uint32_t lastStateSample=0;
    bool stateSampleSeen=false;
    bool tickSeen=false, processCallbackSeen=false, waitingForStreaming=false;
    bool streamingReadySeen=false, sampleAttemptSeen=false;
    bool sdkPointersChecked=false;
    unsigned startupTraceTicks=0, startupRenderTraces=0;
    unsigned bindingAttempts=0;
    uint32_t lastBindingAttempt=0,lastBindingCheck=0;
    uint64_t latestAvgLoadUs=0;
    asc::stream_flow::StateSweep<asc::kVanillaModelInfoCountCap> stateSweep;
    asc::stream_flow::BacklogMeter backlogMeter;
    asc::request_profile::Profiler<> requestProfiler;
    asc::stability::ControllerGate controllerGate;
    asc::motion::Detector motionDetector;
    asc::motion::RequestBudget requestBudget;
    uint64_t lastCameraCostUs=0;
    uint32_t lastSlowCameraMs=0;
    bool slowCameraSeen=false;
    asc::stream_flow::StageCounter stateSamplingStage,cameraStage,controllerStage,
                                   archiveStage,cacheStage,requestProfileStage;
    LARGE_INTEGER flowFrequency{};
    uint32_t sampledSweepNumber=0;
    asc::catalog_prefetch::Trajectory catalogTrajectory;
    asc::catalog_prefetch::Scheduler catalogPrefetch;
    int catalogDesiredX=INT32_MAX,catalogDesiredY=INT32_MAX;
    uint64_t catalogReportedRequests=0,catalogReportedRejects=0;
    uint32_t catalogLastStatusMs=0;

    static LARGE_INTEGER Qpc() {
        LARGE_INTEGER stamp{};
        QueryPerformanceCounter(&stamp);
        return stamp;
    }
    uint64_t ElapsedUs(LARGE_INTEGER a,LARGE_INTEGER b) const {
        if(flowFrequency.QuadPart<=0 || b.QuadPart<a.QuadPart)return 0;
        return uint64_t(b.QuadPart-a.QuadPart)*1000000ull/
               uint64_t(flowFrequency.QuadPart);
    }
    void Stage(asc::stream_flow::StageCounter& counter,
               LARGE_INTEGER a,LARGE_INTEGER b) {
        counter.Add(ElapsedUs(a,b));
    }
    static asc::stream_flow::State ToFlowState(int engineState) {
        using S=asc::stream_flow::State;
        if(engineState==LOADSTATE_NOT_LOADED)return S::NotLoaded;
        if(engineState==LOADSTATE_Requested)return S::Requested;
        if(engineState==LOADSTATE_Channeled)return S::Channeled;
        if(engineState==LOADSTATE_Finishing)return S::Finishing;
        if(engineState==LOADSTATE_LOADED)return S::Loaded;
        return S::Other;
    }
    RuntimeConfig config{};
    std::unique_ptr<asc::Controller> controller;
    GameAdapter adapter;
    Telemetry telemetry;
    readiness::Tracker tracker;
    asc::StatsWindow statsWindow;
    StreamingMemory streamingMemory;
    CameraReadiness cameraReadiness;
    OffscreenCleanup offscreenCleanup;
    asc::Snapshot latestState{};
    AsyncLog log;
    std::ostringstream startupLog;
    std::string directory;

    static std::string Dir(){
        HMODULE m=nullptr;
        if(!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&Runtime::Dir),&m))
            return ".";
        char path[MAX_PATH]{};
        if(!GetModuleFileNameA(m,path,MAX_PATH))return ".";
        std::string p(path);
        auto end=p.find_last_of("\\/");
        return end==std::string::npos?".":p.substr(0,end);
    }
    void PrepareMemory(bool openLog=true){
        if(!memoryPrepared){
            memoryPrepared=true;
            directory=Dir();
            config=ReadConfig(directory);
            startupLog<<"Distant Streaming: predictive native streaming and x64 asset cache, "
              "nonblocking GTA archive read-through, D3D9 GPU residency unchanged. "
              "GTA remains a 32-bit process.\n";
            if(config.enabled && config.hookMode!=0){
                const auto v=ValidateGameTarget(false);
                startupLog<<"ASI-load target check: "<<v.reason<<"\n";
                targetAccepted=v.ok;
                if(targetAccepted){
                    startupLog<<"ASC limiter initialization: ASI module load, before game callbacks; "
                                "streaming initialized="<<unsigned(CStreaming::ms_bIsInitialised!=0)<<"\n";
                    streamingMemory.Configure(config.streamingMemory,startupLog);
                }
            }
        }
        // The ASI-load path creates no workers, waits for no threads, and
        // buffers diagnostics in memory. Start file logging outside loader lock.
        if(openLog && !logOpened){
            logOpened=true;
            log.Open(directory+"\\DistantStreaming.log");
            log<<startupLog.str();startupLog.str({});
            log.flush();
        }
    }
    void Begin(){
        if(started)return;
        started=true;
        QueryPerformanceFrequency(&flowFrequency);
        PrepareMemory();
        if(!config.enabled){log<<"Disabled or invalid config; no hooks.\n";log.flush();return;}
        log<<"Diagnostics HookMode="<<config.hookMode<<" (0=passive, 1=Request, 2=Request+Load)"
           <<", ControllerEnabled="<<(config.controllerEnabled?1:0)
           <<", ReadinessTelemetry="<<(config.readinessTelemetry?1:0)
           <<", DynamicReadiness="<<(config.core.dynamicWarmup?1:0)
           <<", HookIntegrityMonitor="<<(config.hookIntegrityMonitor?1:0)
           <<", RecoverPristineHooks="<<(config.recoverPristineHooks?1:0)
           <<"\n";
        if(config.hookMode==0){
            log<<"PASSIVE: no game hooks, no scheduler, no HD client, no renderer.\n";
            log.flush();return;
        }
        log.flush();
        if(!targetAccepted)return;
        gpu_residency::Configure(config.gpuResidency);
        // The bounded binary IPL/index cache remains available. When the x64
        // service is enabled, optionally reduce its duplicate x86 RAM usage.
        if(config.externalCache.enabled&&config.archivePreload.enabled &&
           config.externalCache.inProcessArchiveCacheMiB>0 &&
           config.archivePreload.maxCacheMiB>
               config.externalCache.inProcessArchiveCacheMiB){
            config.archivePreload.maxCacheMiB=
                config.externalCache.inProcessArchiveCacheMiB;
            log<<"AssetCache64: legacy in-process archive cache limited to "
               <<config.archivePreload.maxCacheMiB
               <<" MiB; IPL validation remains enabled\n";
        }
        // Compute root from host game executable, not from scripts/ASI path.
        char exePath[MAX_PATH]{};
        if(GetModuleFileNameA(nullptr,exePath,MAX_PATH)){
            std::string exe(exePath);
            const auto last=exe.find_last_of("\\/");
            if(last!=std::string::npos)
                asc::archive_preload::Start(
                    exe.substr(0,last),config.archivePreload);
        }
        log<<"Frame stability: enabled="<<config.stability.enabled
           <<", blocking camera loads="<<config.cameraReadiness.blocking
           <<", controller interval/slow retry Ms="
           <<config.stability.controllerIntervalMs<<"/"
           <<config.stability.slowRetryMs
           <<", max predictive cells/candidates="
           <<config.stability.maxPredictiveCells<<"/"
           <<config.stability.maxPredictiveCandidates
           <<" (GTA native requests, world visibility, and LOD remain unchanged)\n";
        adapter.Configure(config.core,config.offscreenCleanup);
        adapter.SetRequestBudget(&requestBudget);
        log<<"Motion scheduling: enabled="<<config.motion.enabled
           <<", fast controller/camera ms="<<config.motion.controllerMs<<"/"<<config.motion.cameraMs
           <<", hold ms="<<config.motion.holdMs
           <<", shared requests/sec="<<config.motion.requestsPerSecond
           <<", burst="<<config.motion.requestBurst<<"\n";
        catalogPrefetch.Configure(config.catalogPrefetch);
        log<<"Catalog native prefetch: enabled="<<config.catalogPrefetch.enabled
           <<", limit/tick="<<config.catalogPrefetch.maxRequestsPerTick
           <<", pending cap="<<config.catalogPrefetch.maxOutstanding
           <<", backlog limit="<<config.catalogPrefetch.nativeBacklogLimit
           <<", predictive horizon Ms="<<config.catalogPrefetch.lookAheadMs
           <<", live model name+ID check required; never pins models\n";
        cameraReadiness.Configure(config.cameraReadiness,log);
        offscreenCleanup.Configure(config.offscreenCleanup,log);
        if(config.offscreenCleanup.enabled&&config.offscreenCleanup.disableBlockingLoads)
            log<<"Offscreen smooth mode: blocking camera loads disabled; camera instancing capped at four.\n";
        log<<"World readiness: tick Ms="<<config.core.tickMs
           <<", lookahead Ms="<<unsigned(config.core.lookAheadSeconds*1000)
           <<", requests/tick="<<config.core.maxAdaptiveRequests
           <<", priority/tick="<<config.core.maxPriorityPerTick
           <<", LOD scan active\n";
        controller=std::make_unique<asc::Controller>(config.core);
        telemetryEnabled=config.telemetry&&telemetry.Start(directory);
        log<<"Telemetry: config="<<(config.telemetry?"enabled":"disabled")
           <<", file="<<(telemetryEnabled?"opened":"not active")<<"\n";
        const bool archiveRequested=config.archiveProfile;
        const bool archiveAllowed=archiveRequested&&IsArchiveHookAvailable();
        if(archiveRequested&&!archiveAllowed)
            log<<"Archive hook address unavailable: profiling disabled; "
                  "core streaming remains available.\n";
        operational=hooks::Install({
            archiveAllowed,
            config.archiveSampleEvery,
            config.archiveSlowThresholdUs,
            config.hookMode
        });
        log<<hooks::InstallReport();
        if(!operational) {
            log<<"Could not install detours; no speculative requests.\n";
            controller.reset();
            telemetry.Stop();
            telemetryEnabled=false;
        }else {
            asc::external_cache::Start(directory,config.externalCache,log);
            log<<"Active independent ASI.\n";
            if(!config.controllerEnabled)
                log<<"ISOLATION: predictive controller disabled; GPU diagnostics "
                      "and native memory management remain independently configured.\n";
        }
        log.flush();
    }
    void RenderFrame(){
        if(!started)Begin();
        ScopedFrameCallback timing(true);
        // Do not install game renderer or visibility callbacks.
        if(!operational)return;
        {
            ScopedRenderStage sampled(asc::render_cost::Stage::GpuFrame);
            GpuFrame();
        }
        if(CStreaming::ms_bIsInitialised) {
            ScopedRenderStage sampled(asc::render_cost::Stage::MemoryReassert);
            streamingMemory.Reassert(GetTickCount(),log,"drawingEvent");
        }
        if(config.controllerEnabled){
            {
                ScopedRenderStage sampled(asc::render_cost::Stage::TelemetryFrame);
                telemetry.Frame();
            }
            // Tick() already has an interval gate; callbacks cannot double-tick.
            {
                ScopedRenderStage sampled(asc::render_cost::Stage::ControllerTick);
                Tick("drawingEvent");
            }
        }
        if(!config.controllerEnabled)return;
        // Never inspect RenderWare TXD/model tables until the OLA resolver
        // confirms the active streaming/model pointers.
        if(!sdkPointersChecked)return;
        if(operational){
            asc::Snapshot snapshot{};
            {
                ScopedRenderStage sampled(asc::render_cost::Stage::StateCapture);
                snapshot=latestState; snapshot.nowMs=GetTickCount();
                snapshot.gameplaySafe=CStreaming::ms_bIsInitialised&&!CStreaming::ms_disableStreaming
                    &&!CStreaming::m_bLoadingScene&&!CTimer::m_CodePause&&!CTimer::m_UserPause
                    &&!CCutsceneMgr::ms_running&&!CCutsceneMgr::ms_cutsceneProcessing
                    &&!CWorld::bProcessCutsceneOnly;
                snapshot.playerValid=false;
                if(snapshot.gameplaySafe)if(auto* ped=FindPlayerPed()){
                    const auto& p=ped->GetPosition();
                    snapshot.player={p.x,p.y,p.z};
                    snapshot.playerValid=true;snapshot.area=ped->m_nAreaCode;
                }
                const auto& p=TheCamera.m_vecGameCamPos;
                const auto& forward=TheCamera.m_mCameraMatrix.GetForward();
                snapshot.camera={p.x,p.y,p.z};
                snapshot.cameraForward={forward.x,forward.y,forward.z};
                const float norm=forward.x*forward.x+
                    forward.y*forward.y+forward.z*forward.z;
                snapshot.cameraValid=norm>.25f&&norm<4;
                snapshot.gpuBudgetCritical=gpu_residency::UnderPressure();
                snapshot.streamingUsed=CStreaming::ms_memoryUsed;
                snapshot.streamingBudget=streaming_budget::Effective();
                snapshot.recentFrameMs=telemetry.RecentFrameMs();
            }
            {
                ScopedRenderStage sampled(asc::render_cost::Stage::Cleanup);
                offscreenCleanup.Frame(snapshot,config.core);
            }
        }
    }
    void ProcessFrame(){
        if(!started)Begin();
        ScopedFrameCallback timing(false);
        if(!processCallbackSeen){
            processCallbackSeen=true;
            log<<"gameProcessEvent fired\n";
            log.flush();
        }
        if(operational)GpuFrame();
        Tick("gameProcessEvent");
        if(operational && CStreaming::ms_bIsInitialised)
            streamingMemory.Reassert(GetTickCount(),log,"gameProcessEvent");
    }
    // Original D3D9 Create* functions still allocate. We only observe those
    // calls and their COM lifetimes, never proxy resources or block on an EXE.
    void GpuFrame(){
        constexpr uintptr_t devicePointer=0xC97C28; // GTA SA 1.0 US
        if(!asc::world::Readable(reinterpret_cast<void*>(devicePointer),sizeof(void*)))return;
        auto* device=*reinterpret_cast<IDirect3DDevice9**>(devicePointer);
        if(!device||!asc::world::Readable(device,sizeof(void*)))return;
        const uint64_t budget=streaming_budget::Effective();
        const bool pressure=!budget || streamingMemory.LowAddressSpace() ||
            uint64_t(CStreaming::ms_memoryUsed)*100>=budget*config.core.memoryThrottlePercent ||
            telemetry.RecentFrameMs()>=config.core.frameRecoveryMs;
        gpu_residency::Frame(device,GetTickCount(),pressure,log);
    }
    void Tick(const char* source){
        if(!started)Begin();
        if(!operational)return;
        const uint32_t now=GetTickCount();
        if(uint32_t(now-lastTick)<config.core.tickMs)return;
        lastTick=now;
        if(!config.controllerEnabled){
            // The native memory budget does not depend on world/model-table
            // pointer resolution. Keep ASC budget ownership active after a
            // speculative-controller safety stop.
            if(config.streamingMemory.enabled && CStreaming::ms_bIsInitialised){
                MEMORYSTATUSEX mem{};
                mem.dwLength=sizeof(mem);
                streamingMemory.Update(GetTickCount(),
                    GlobalMemoryStatusEx(&mem)?&mem:nullptr,log);
            }
            return;
        }
        const uint32_t integrityNow=GetTickCount();
        if(config.hookIntegrityMonitor &&
           uint32_t(integrityNow-lastHookCheck)>=config.hookCheckIntervalMs){
            lastHookCheck=integrityNow;
            if(!hooks::CheckIntegrity(config.recoverPristineHooks,log)){
                log<<"SCHEDULER STOP: request hook ownership lost. "
                      "Do not overwrite foreign detours.\n";
                log.flush();
                config.controllerEnabled=false;
                return;
            }
        }
        if(!tickSeen){
            tickSeen=true;
            log<<"Scheduler reached from "<<source<<"\n";
            log.flush();
        }
        if(!CStreaming::ms_bIsInitialised){
            if(!waitingForStreaming){
                waitingForStreaming=true;
                log<<"Scheduler waiting for GTA streaming initialization\n";
                log.flush();
            }
            controller->Reset();
            motionDetector.Reset();requestBudget.Reset();slowCameraSeen=false;
            catalogTrajectory.Reset();
            catalogPrefetch.Reset();
            tracker.Reset();
            streamingMemory.StreamingStopped();
            return;
        }
        if(!streamingReadySeen){
            streamingReadySeen=true;
            log<<"Scheduler streaming ready; ticking controller\n";
            log.flush();
        }
        // Native memory ownership is independent of the model-table resolver.
        // Even if OLA relocates model metadata in a way we cannot yet decode,
        // ASC can still enforce its configured native budget without traversing
        // speculative pointers or issuing extra model requests.
        MEMORYSTATUSEX mem{};
        mem.dwLength=sizeof(mem);
        const bool memoryValid=GlobalMemoryStatusEx(&mem)!=FALSE;
        streamingMemory.Update(now,memoryValid?&mem:nullptr,log);
        // OLA can patch the instruction immediates captured by Plugin-SDK
        // at DLL load time. Resolve the live arrays only after streaming
        // initializes, without patching the game or altering OLA's ownership.
        if(!sdkPointersChecked) {
            // Some ASI loaders initialise the adjuster after our first
            // callback. Retry for a bounded interval, without touching
            // model tables or issuing any speculative RequestModel calls.
            if(bindingAttempts>0 && uint32_t(now-lastBindingAttempt)<500)
                return;
            lastBindingAttempt=now;
            ++bindingAttempts;
            if(ola_bindings::Bind(log)) {
                sdkPointersChecked=true;
                lastBindingCheck=now;
                log<<"ASC START: live OLA bindings verified after "
                   <<bindingAttempts<<" attempt(s)\n";
                log.flush();
            } else {
                if(bindingAttempts>=12) {
                    log<<"SAFETY STOP: runtime array binding unavailable "
                          "after 12 attempts; controller disabled.\n";
                    log.flush();
                    config.controllerEnabled=false;
                    config.core.dynamicWarmup=false;
                    catalogPrefetch.Reset();
                    }
                return;
            }
        }
        if(uint32_t(now-lastBindingCheck)>=2000){
            lastBindingCheck=now;
            if(!ola_bindings::StillBound()){
                log<<"OLA BINDINGS: relocated table changed; rebinding\n";
                log.flush();
                if(!ola_bindings::Bind(log)){
                    log<<"SAFETY STOP: live OLA binding lost; speculative "
                          "scheduler and renderer disabled, budget remains active.\n";
                    log.flush();
                    config.controllerEnabled=false;
                    config.core.dynamicWarmup=false;
                    catalogPrefetch.Reset();
                        return;
                }
            }
        }
        {
            // This block only observes requests/game state. Discard its region
            // checks before any later native requests, loading or instancing.
            asc::world::RegionValidationCache regions;
            asc::world::ScopedRegionCache validation(regions);
            struct CallbackCtx {
                Runtime*self;
                uint32_t now;
                asc::Vec3 player{};
                bool checkedPlayer=false;
                bool havePlayer=false;
            };
            CallbackCtx context{this,now};
            // Do not inspect game-owned model arrays just to record requests.
            // The previous callback called CModelInfo helpers even when optional
            // readiness features were disabled and OnRequest dereferenced streaming
            // tables; a corrupted/incompatible pointer triggered +0x53212.
            // Drain the coalescing mailbox without touching game objects when
            // all request consumers are disabled.
            if(config.core.prefetch || config.core.dynamicWarmup || config.readinessTelemetry){
                hooks::DrainVanillaRequests([](const hooks::RequestRecord& rec,void*ptr){
                    auto&ctx=*static_cast<CallbackCtx*>(ptr);
                    auto&self=*ctx.self;
                    const int id=rec.id,flags=rec.flags;
                    if(id<0||id>=int(asc::kVanillaModelInfoCountCap))return;
                    const uint32_t requestedAt=
                        uint32_t(ctx.now-rec.requestedAt)<0x80000000u?
                        rec.requestedAt:ctx.now;

                    self.controller->OnVanillaRequest(id,flags);
                    // The passive tracker cannot inspect game-owned state during
                    // observation; it only records the bounded id/timestamp.
                    if(self.config.readinessTelemetry)
                        self.tracker.OnRequest(id,requestedAt,readiness::ModelClass::Other);

                    if(!self.config.core.dynamicWarmup || !rec.onMainThread ||
                       (flags&(MISSION_REQUIRED|KEEP_IN_MEMORY|LOADING_SCENE))!=0)
                        return;
                    // Dynamic traffic classification is explicitly opt-in.
                    // Only this optional path touches GTA's model table.
                    if(id>=CModelInfo::ms_modelInfoCount ||
                       !CModelInfo::ms_modelInfoPtrs ||
                       !asc::world::Readable(
                            &CModelInfo::ms_modelInfoPtrs[id],
                            sizeof(CModelInfo::ms_modelInfoPtrs[id])))
                        return;
                    auto* model=CModelInfo::ms_modelInfoPtrs[id];
                    if(!model || !asc::world::Readable(model,sizeof(void*)))return;
                    const auto type=model->GetModelType();
                    if(type!=MODEL_INFO_VEHICLE && type!=MODEL_INFO_PED)
                        return;
                    // Resolve the player only for actual eligible traffic, and
                    // reuse that position for this read-only drain batch.
                    if(!ctx.checkedPlayer){
                        ctx.checkedPlayer=true;
                        if(auto* ped=FindPlayerPed()){
                            const auto& p=ped->GetPosition();
                            ctx.player={p.x,p.y,p.z};
                            ctx.havePlayer=true;
                        }
                    }
                    if(ctx.havePlayer)
                        self.controller->OnDynamicVanillaDemand(id,requestedAt,ctx.player);
                },&context);
            }else {
                hooks::DrainVanillaRequests(
                    [](const hooks::RequestRecord&,void*){},nullptr);
            }
        }
        if(config.readinessTelemetry)
            tracker.Sample(now);

        asc::Snapshot s{};
        s.nowMs=now;
        s.gpuBudgetCritical=gpu_residency::UnderPressure();
        s.streamingUsed=CStreaming::ms_memoryUsed;
        s.gameRequests=CStreaming::ms_numModelsRequested;
        s.gamePriorityRequests=CStreaming::ms_numPriorityRequests;
        s.recentFrameMs=telemetry.RecentFrameMs();
        s.recentLoadAvgUs=latestAvgLoadUs;

        PROCESS_MEMORY_COUNTERS_EX pm{};
        if(GetProcessMemoryInfo(GetCurrentProcess(),
           reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm),sizeof(pm))){
            s.privateBytes=pm.PrivateUsage;
            s.workingSetBytes=pm.WorkingSetSize;
        }
        if(memoryValid){
            s.availablePhysicalBytes=mem.ullAvailPhys;
            s.totalVirtualBytes=mem.ullTotalVirtual;
            s.availableVirtualBytes=mem.ullAvailVirtual;
        }
        const bool traceStartup=startupTraceTicks<3;
        if(traceStartup){
            log<<"TRACE scheduler #"<<startupTraceTicks
               <<": memory update finished; before camera readiness\n";
            log.flush();
        }
        s.streamingBudget=streaming_budget::Effective();
        s.largeAddressAware=streamingMemory.LargeAddressAware();
        s.addressSpaceLow=!memoryValid||streamingMemory.LowAddressSpace();

        s.gameplaySafe=CStreaming::ms_bIsInitialised &&
            !CStreaming::ms_disableStreaming &&
            !CStreaming::m_bLoadingScene &&
            !CTimer::m_CodePause && !CTimer::m_UserPause &&
            !CCutsceneMgr::ms_running &&
            !CCutsceneMgr::ms_cutsceneProcessing &&
            !CWorld::bProcessCutsceneOnly;
        if(s.gameplaySafe){
            CPlayerPed*ped=FindPlayerPed();
            if(ped){
                const CVector&pos=ped->GetPosition();
                s.player={pos.x,pos.y,pos.z};
                s.playerValid=true;
                // Publishes only two atomics; all catalog IPC stays on x64 cache worker.
                if(config.catalogPrefetch.enabled){
                    const auto predicted=catalogTrajectory.Predict(
                        s.player,now,config.catalogPrefetch,
                        config.core.maxTeleportUnits);
                    // The x64 worker owns all catalog queries; these are only
                    // atomic coordinates, even when the helper is unavailable.
                    asc::external_cache::WorldPositionHint(predicted.x,predicted.y);
                    catalogDesiredX=int(std::floor(double(predicted.x)/256.0));
                    catalogDesiredY=int(std::floor(double(predicted.y)/256.0));
                }else {
                    asc::external_cache::WorldPositionHint(pos.x,pos.y);
                }
                s.area=ped->m_nAreaCode;
                const CVector&cam=TheCamera.m_vecGameCamPos;
                const CVector&fwd=TheCamera.m_mCameraMatrix.GetForward();
                s.camera={cam.x,cam.y,cam.z};
                s.cameraForward={fwd.x,fwd.y,fwd.z};
                const float norm2=fwd.x*fwd.x+fwd.y*fwd.y+fwd.z*fwd.z;
                s.cameraValid=norm2>0.25f&&norm2<4.0f;
            }
        }
        // Sample only a bounded slice of the verified OLA streaming table.
        // A full sweep spans multiple ticks, so reported transitions are
        // observations, NOT exact native completion or cancellation events.
        if(!stateSampleSeen ||
           uint32_t(now-lastStateSample)>=config.stateSampleIntervalMs){
            const auto beforeStateSample=Qpc();
            lastStateSample=now;stateSampleSeen=true;
            asc::world::RegionValidationCache regions;
            asc::world::ScopedRegionCache validation(regions);
            // Both passes are read-only and adjacent. Check live binding once,
            // and reuse readable regions only until this block ends.
            const bool stateTablesBound=ola_bindings::StillBound() &&
                CStreaming::ms_aInfoForModel;
            if(stateTablesBound) {
                constexpr std::size_t kSlice=512;
                const std::size_t cursor=stateSweep.Cursor();
                const std::size_t count=std::min(kSlice,
                    std::size_t(asc::kVanillaModelInfoCountCap)-cursor);
                const auto* begin=CStreaming::ms_aInfoForModel+cursor;
                if(asc::world::Readable(begin,count*sizeof(CStreamingInfo)))
                    stateSweep.Sample(asc::kVanillaModelInfoCountCap,kSlice,
                        [&](std::size_t id) {
                            return ToFlowState(int(
                                CStreaming::ms_aInfoForModel[id].m_nLoadState));
                        });
            }
            // Track only IDs sampled in the RequestModel hook. The bound model
            // table is read here on GTA's main thread, never inside the detour.
            if(stateTablesBound) {
                requestProfiler.Poll(now,[&](int id) {
                    if(!asc::request_profile::StateIndexCandidate(id) ||
                       !asc::world::Readable(&CStreaming::ms_aInfoForModel[id],
                                              sizeof(CStreamingInfo)))
                        return asc::stream_flow::State::Other;
                    return ToFlowState(int(
                        CStreaming::ms_aInfoForModel[id].m_nLoadState));
                });
            }
            Stage(stateSamplingStage,beforeStateSample,Qpc());
        }
        backlogMeter.Add(now,uint32_t(s.gameRequests),
                         uint32_t(s.gamePriorityRequests));
        if(!s.gameplaySafe || !s.playerValid || s.area!=0)requestBudget.Reset();
        requestBudget.Begin(now,config.motion.requestsPerSecond,config.motion.requestBurst);
        motionDetector.Update(s,config.core,config.motion,
            controllerGate.LastCostUs()>=config.stability.slowControllerUs ||
            (slowCameraSeen && uint32_t(now-lastSlowCameraMs)<config.stability.slowRetryMs));
        const auto beforeCamera=Qpc();
        const bool cameraChanged=cameraReadiness.Tick(s,config.core,&requestBudget,
            s.fastCamera?config.motion.cameraMs:0);
        lastCameraCostUs=ElapsedUs(beforeCamera,Qpc());
        cameraStage.Add(lastCameraCostUs);
        if(lastCameraCostUs>=config.stability.slowControllerUs){
            lastSlowCameraMs=now;slowCameraSeen=true;
        }
        if(traceStartup){
            log<<"TRACE scheduler: camera readiness returned; changed="<<cameraChanged<<"\n";
            log.flush();
        }
        if(cameraChanged){
            // A synchronous batch can consume substantial address space.
            // Refresh guards before admitting additional predictive work.
            MEMORYSTATUSEX after{};after.dwLength=sizeof(after);
            if(GlobalMemoryStatusEx(&after)){
                s.availablePhysicalBytes=after.ullAvailPhys;
                s.totalVirtualBytes=after.ullTotalVirtual;
                s.availableVirtualBytes=after.ullAvailVirtual;
                streamingMemory.Update(GetTickCount(),&after,log);
                s.addressSpaceLow=streamingMemory.LowAddressSpace();
            }else {
                s.addressSpaceLow=true;
                s.totalVirtualBytes=s.availableVirtualBytes=s.availablePhysicalBytes=0;
            }
            PROCESS_MEMORY_COUNTERS_EX used{};
            if(GetProcessMemoryInfo(GetCurrentProcess(),
               reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&used),sizeof(used))){
                s.privateBytes=used.PrivateUsage;s.workingSetBytes=used.WorkingSetSize;
            }
        }
        // Catch-up can finish native loads and change the backlog/budget.
        s.gpuBudgetCritical=gpu_residency::UnderPressure();
        s.streamingUsed=CStreaming::ms_memoryUsed;
        s.streamingBudget=streaming_budget::Effective();
        s.gameRequests=CStreaming::ms_numModelsRequested;
        s.gamePriorityRequests=CStreaming::ms_numPriorityRequests;
        // Camera requests/instancing may have changed live pressure. Drop a
        // boost immediately before the controller when those guards now trip.
        motionDetector.Update(s,config.core,config.motion,
            controllerGate.LastCostUs()>=config.stability.slowControllerUs ||
            (slowCameraSeen && uint32_t(now-lastSlowCameraMs)<config.stability.slowRetryMs));
        adapter.SetMotion(s.fastTravel,s.fastCamera);
        const unsigned fastInterval=s.fastTravel||s.fastCamera?config.motion.controllerMs:0;
        if(traceStartup){
            log<<"TRACE scheduler: before controller tick\n";
            log.flush();
        }
        bool didControllerTick=false;
        if(controllerGate.Due(now,config.stability,fastInterval)) {
            const auto beforeController=Qpc();
            controller->Tick(s,adapter,
                config.stability.enabled?config.stability.maxPredictiveCells:0,
                config.stability.enabled?config.stability.maxPredictiveCandidates:0,
                fastInterval);
            const auto elapsed=ElapsedUs(beforeController,Qpc());
            controllerStage.Add(elapsed);
            controllerGate.Record(now,elapsed,config.stability);
            didControllerTick=true;
        }
        // Consume the latest x64 hint ONLY on GTA's thread. No IPC, network,
        // object pinning or synchronous loads. Extra requests share the
        // controller's max-requests-per-tick quota and real native backlog.
        if(config.catalogPrefetch.enabled && s.gameplaySafe &&
           ola_bindings::StillBound() && CModelInfo::ms_modelInfoPtrs &&
           CStreaming::ms_aInfoForModel) {
            asc::native::CatalogCellReply hints{};
            uint64_t serial=0;
            const bool available=asc::external_cache::TryCatalogHints(hints,serial);
            const unsigned controllerIssued=didControllerTick?
                controller->GetStats().issued:0;
            catalogPrefetch.Tick(
                s,available?&hints:nullptr,serial,
                catalogDesiredX,catalogDesiredY,
                CTimer::m_FrameCounter,controllerIssued,
                config.core.maxRequestsPerTick,adapter,
                [](int id,const char* name) {
                    if(id<0 || id>=int(asc::kVanillaModelInfoCountCap) ||
                       id>=CModelInfo::ms_modelInfoCount ||
                       !CModelInfo::ms_modelInfoPtrs ||
                       !CStreaming::ms_aInfoForModel ||
                       !asc::world::Readable(
                           &CModelInfo::ms_modelInfoPtrs[id],sizeof(void*)) ||
                       !asc::world::Readable(
                           &CStreaming::ms_aInfoForModel[id],
                           sizeof(CStreamingInfo)))return false;
                    CBaseModelInfo* model=CModelInfo::ms_modelInfoPtrs[id];
                    if(!model || !asc::world::Readable(model,sizeof(CBaseModelInfo)))
                        return false;
                    int liveId=-1;
                    CBaseModelInfo* resolved=CModelInfo::GetModelInfo(name,&liveId);
                    return liveId==id && resolved==model;
                });
        }
        const auto beforeArchive=Qpc();
        asc::archive_preload::Report(now,log);
        Stage(archiveStage,beforeArchive,Qpc());
        const auto beforeCache=Qpc();
        asc::external_cache::Report(now,log);
        Stage(cacheStage,beforeCache,Qpc());

        if(traceStartup){
            log<<"TRACE scheduler: controller tick returned\n";
            log.flush();
            ++startupTraceTicks;
        }
        latestState=s;
        if(traceStartup){log<<"TRACE scheduler: before stats update\n";log.flush();}
        if(didControllerTick)statsWindow.Add(controller->GetStats());
        if(traceStartup){log<<"TRACE scheduler: stats updated\n";log.flush();}
        if(uint32_t(now-lastSample)>=1000){
            if(config.catalogPrefetch.enabled){
                const auto& c=catalogPrefetch.GetCounters();
                if(c.requests!=catalogReportedRequests ||
                   c.wrongIdentity+c.badName!=catalogReportedRejects ||
                   uint32_t(now-catalogLastStatusMs)>=10000){
                    catalogLastStatusMs=now;
                    catalogReportedRequests=c.requests;
                    catalogReportedRejects=c.wrongIdentity+c.badName;
                    log<<"Catalog prefetch: requests="<<c.requests
                       <<", pending="<<c.outstanding
                       <<", nameReject="<<c.badName
                       <<", identityReject="<<c.wrongIdentity
                       <<", stateSkip="<<c.notEligible
                       <<", duplicate="<<c.duplicate
                       <<", staleCellTicks="<<c.staleCell
                       <<", pressureTicks="<<c.pressureSkips
                       <<", completed="<<c.completed<<"\n";
                }
            }
            if(traceStartup){log<<"TRACE scheduler: entering one-second sample\n";log.flush();}
            lastSample=now;
            const auto c=hooks::CollectCounters();
            const auto beforeRequestProfile=Qpc();
            const auto requestBatch=hooks::TakeRequestProfileBatch();
            const bool profileBound=ola_bindings::StillBound() &&
                CStreaming::ms_aInfoForModel &&
                asc::world::Readable(CStreaming::ms_aInfoForModel,
                                     sizeof(CStreamingInfo));
            for(uint32_t i=0;i<requestBatch.count;++i){
                const auto& sample=requestBatch.samples[i];
                // Diagnostic IDs include TXD/COL/IPL/DAT/IFP/RRR/SCM, not
                // just the 20k DFF IDs. Verify the individual entry before
                // reading its state; OLA may relocate/extend this allocation.
                const bool valid=profileBound &&
                    asc::request_profile::StateIndexCandidate(sample.id) &&
                    asc::world::Readable(&CStreaming::ms_aInfoForModel[sample.id],
                                         sizeof(CStreamingInfo));
                const auto state=valid?ToFlowState(int(
                    CStreaming::ms_aInfoForModel[sample.id].m_nLoadState)):
                    asc::stream_flow::State::Other;
                requestProfiler.Record(sample,state,valid);
            }
            const auto requestSummary=requestProfiler.Take();
            Stage(requestProfileStage,beforeRequestProfile,Qpc());
            const auto backlog=backlogMeter.Take();
            log<<"Streaming flow: native RequestModel calls="<<c.nativeRequestCalls
               <<", DistantStreaming calls="<<c.pluginRequestCalls
               <<", native off-main-thread="<<c.nonMainNativeRequests
               <<", observed coalesced="<<c.requestCoalesced
               <<", engine backlog last/low/peak="<<backlog.last<<"/"
               <<backlog.low<<"/"<<backlog.peak
               <<", engine priority="<<backlog.priorityLast
               <<", backlog rising/falling/steady="<<backlog.increases<<"/"
               <<backlog.decreases<<"/"<<backlog.unchanged
               <<", samples="<<backlog.samples<<"\n";
            // One-second memory-pressure correlation; no extra VA walk here.
            constexpr uint64_t kMiB=1024ull*1024ull;
            const auto budgetHeadroom=s.streamingBudget>s.streamingUsed
                ? s.streamingBudget-s.streamingUsed:0u;
            log<<"Streaming memory correlation: freeVA MiB="
               <<(s.availableVirtualBytes/kMiB)
               <<", largestFreeVA MiB (cached)="
               <<(streamingMemory.LargestFreeVaBytes()/kMiB)
               <<", private MiB="<<(s.privateBytes/kMiB)
               <<", streaming used/budget/headroom MiB="
               <<(uint64_t(s.streamingUsed)/kMiB)<<"/"
               <<(uint64_t(s.streamingBudget)/kMiB)<<"/"
               <<(uint64_t(budgetHeadroom)/kMiB)
               <<", native backlog="<<s.gameRequests
               <<" (observational; not a request throttle)\n";
            log<<"Request caller samples (1/128 main-thread native, delayed state): sampled="
               <<requestBatch.count<<", dropped="<<requestBatch.dropped
               <<", negative IDs="<<requestSummary.invalidIds
               <<", state unavailable="<<requestSummary.stateUnavailable
               <<", states notLoaded/requested/channeled/finishing/loaded/other=";
            for(unsigned i=0;i<6;++i){
                if(i)log<<"/";
                log<<requestSummary.states[i];
            }
            const auto hz=flowFrequency.QuadPart>0?
                uint64_t(flowFrequency.QuadPart):0;
            using asc::request_profile::AverageNanoseconds;
            log<<", hook-only avg/peak ns="
               <<AverageNanoseconds(requestBatch.hookTicks,hz,requestBatch.timed)
               <<"/"<<AverageNanoseconds(requestBatch.hookPeakTicks,hz)
               <<", original avg/peak ns="
               <<AverageNanoseconds(requestBatch.originalTicks,hz,requestBatch.timed)
               <<"/"<<AverageNanoseconds(requestBatch.originalPeakTicks,hz)
               <<", measured="<<requestBatch.timed
               <<" (sampled; instrumentation included)\n";
            log<<"Request resource families: ";
            for(unsigned f=0;f<asc::request_profile::kResourceFamilyCount;++f){
                if(f)log<<", ";
                log<<asc::request_profile::FamilyName(
                    static_cast<asc::request_profile::ResourceFamily>(f))
                   <<"="<<requestSummary.families[f];
            }
            log<<" (standard GTA SA 1.0 boundaries; extensions unclassified)\n";
            log<<"Request top callers (address:estimated samples +/- error): ";
            unsigned shown=0;
            for(const auto& caller:requestSummary.callers){
                if(!caller.address || shown>=6)break;
                if(shown++)log<<", ";
                log<<"0x"<<std::hex<<caller.address<<std::dec<<":"
                   <<caller.count<<"+/-"<<caller.error
                   <<" ["<<DescribeRequestCaller(caller.address)<<"]";
            }
            if(!shown)log<<"none";
            log<<"\n";
            log<<"Request top models (id:samples +/- error, loaded/pending/notLoaded): ";
            shown=0;
            for(const auto& model:requestSummary.models){
                if(model.id<0 || shown>=6)break;
                if(shown++)log<<", ";
                log<<model.id<<":"<<model.count<<"+/-"<<model.error
                   <<"["<<model.states[4]<<"/"
                   <<(model.states[1]+model.states[2]+model.states[3])
                   <<"/"<<model.states[0]<<"]";
            }
            if(!shown)log<<"none";
            log<<"\n";
            log<<"Request sampled load latency: completed="<<requestSummary.completed
               <<", cancelled/returned-to-notLoaded="<<requestSummary.cancelled
               <<", timedOut="<<requestSummary.timedOut
               <<", tracking="<<requestSummary.active
               <<", capacityMisses="<<requestSummary.trackCapacityMisses
               <<", observed avg/peak ms="
               <<(requestSummary.completed?
                   requestSummary.elapsedTotalMs/requestSummary.completed:0)
               <<"/"<<requestSummary.elapsedPeakMs
               <<" (first sampled request to first observed loaded; not native completion)\n";
            if(stateSweep.HasCompleted() &&
               stateSweep.Completed().number!=sampledSweepNumber) {
                const auto& flow=stateSweep.Completed();
                sampledSweepNumber=uint32_t(flow.number);
                log<<"Streaming state sweep: sweep="<<flow.number
                   <<", inspected="<<flow.inspected
                   <<", notLoaded="<<flow.notLoaded
                   <<", requested="<<flow.requested
                   <<", channeled="<<flow.channeled
                   <<", finishing="<<flow.finishing
                   <<", loaded="<<flow.loaded
                   <<", other="<<flow.other
                   <<", observed pending-to-loaded="<<flow.observedCompleted
                   <<", loaded-to-notLoaded="<<flow.observedEvicted
                   <<", pending-to-notLoaded="<<flow.observedCancelled
                   <<", sampled over multiple ticks (not instantaneous)\n";
            }
            log<<"Streaming scheduler CPU us (avg/peak/count): stateSample="
               <<stateSamplingStage.Average()<<"/"<<stateSamplingStage.peakUs<<"/"<<stateSamplingStage.calls
               <<", camera="<<cameraStage.Average()<<"/"<<cameraStage.peakUs<<"/"<<cameraStage.calls
               <<", controller="<<controllerStage.Average()<<"/"<<controllerStage.peakUs<<"/"<<controllerStage.calls
               <<", archiveReport="<<archiveStage.Average()<<"/"<<archiveStage.peakUs<<"/"<<archiveStage.calls
               <<", cacheReport="<<cacheStage.Average()<<"/"<<cacheStage.peakUs<<"/"<<cacheStage.calls
               <<", requestProfile="<<requestProfileStage.Average()<<"/"
               <<requestProfileStage.peakUs<<"/"<<requestProfileStage.calls<<"\n";
            stateSamplingStage={};cameraStage={};controllerStage={};
            archiveStage={};cacheStage={};requestProfileStage={};
            log<<"Motion scheduling: fastTravel="<<s.fastTravel
               <<", fastCamera="<<s.fastCamera
               <<", shared request credit="<<requestBudget.Available()
               <<", issued total="<<requestBudget.Issued()
               <<", rate-denied total="<<requestBudget.Denied()<<"\n";
            if(config.stability.enabled) {
                log<<"Frame stability controller: runs="<<controllerGate.TakeRuns()
                   <<", skipped="<<controllerGate.TakeSkips()
                   <<", slowRuns="<<controllerGate.TakeSlowRuns()
                   <<", lastCostUs="<<controllerGate.LastCostUs()
                   <<", native camera blocking="<<config.cameraReadiness.blocking
                   <<" (only optional predictive work is paced)\n";
            }
            latestAvgLoadUs=c.loadCalls?c.loadTotalUs/c.loadCalls:0;
            const auto r=config.readinessTelemetry ? tracker.TakeSummary() : readiness::Summary{};
            const auto camera= cameraReadiness.TakeCounters();
            const auto cleanup=offscreenCleanup.TakeCounters();
            if(config.offscreenCleanup.helperFirstTrim) {
                log<<"Helper-first native reclaim: activeFrames="<<cleanup.nativeTrimActiveFrames
                   <<", scanned="<<cleanup.nativeTrimScanned
                   <<", qualified="<<cleanup.nativeTrimCandidates
                   <<", nativeModelsRemoved="<<cleanup.nativeTrimEvicted
                   <<", nativeBytesReclaimedMiB="<<cleanup.nativeTrimBytes/1048576ull
                   <<", offscreenInstancesRemoved="<<cleanup.instances
                   <<", totalModelsRemoved="<<cleanup.models
                   <<", totalTextureDictionariesRemoved="<<cleanup.textures
                   <<", cleanup total CPU us="<<cleanup.totalUs
                   <<" (source bytes in helper; decoded render objects stay in GTA until GTA unloads)\n";
            }
            const auto stats=statsWindow.Take();
            if(startupTraceTicks>=3 && config.core.prefetch){
                log<<"ASC activity: cells="<<stats.cells
                   <<", candidates="<<stats.candidates
                   <<", issued="<<stats.issued
                   <<", priority="<<stats.priorityIssued
                   <<", outstanding="<<stats.outstanding
                   <<", denied="<<stats.denied
                   <<", pressure="<<int(stats.state)
                   <<", gameplay_safe="<<s.gameplaySafe<<'\n';
                log.flush();
            }
            const auto rejectedNodes=asc::world::rejectedNodes.exchange(0);
            const auto rejectedEntities=asc::world::rejectedEntities.exchange(0);
            if(rejectedNodes||rejectedEntities){
                log<<"World-list guard: rejected nodes="<<rejectedNodes
                   <<", entity pointers="<<rejectedEntities<<"\n";
                log.flush();
            }
            if(telemetryEnabled){
                telemetry.Sample(now,s,stats,c,r,camera,cleanup);
                if(!sampleAttemptSeen){
                    sampleAttemptSeen=true;
                    log<<"Telemetry: first one-second sample attempted\n";
                    log.flush();
                }
            }
        }
        if(traceStartup){log<<"TRACE scheduler: finished tick housekeeping\n";log.flush();}
        // Publish partial diagnostic chunks without game-thread file I/O.
        log.flush();
    }
    void Shutdown(){
        if(!memoryPrepared)return;
        // The game shutdown event is outside DllMain loader lock.
        operational=false;
        streamingMemory.Shutdown(log);
        gpu_residency::Stop();
        asc::archive_preload::Stop();
        asc::external_cache::Stop();

        hooks::Uninstall();
        telemetry.Stop();
        controller.reset();
        tracker.Reset();
        if(log){log<<"Shutdown.\n";log.flush();}
        log.Stop();
    }
};
// Deliberately process-lifetime allocated. Never join telemetry worker or
// unhook executable code from global destructor under DLL loader lock.
Runtime*runtime=new Runtime();
struct Bootstrap{
    Bootstrap(){
        // Like OLA's initializer, apply the scalar/guarded code patches while
        // this ASI loads, rather than waiting for RW or gameplay callbacks.
        runtime->PrepareMemory(false);
        // Only start logging here. No second limiter initialization.
        plugin::Events::initRwEvent.before += [] {runtime->PrepareMemory();};
        plugin::Events::gameProcessEvent += [] {runtime->ProcessFrame();};
        plugin::Events::drawingEvent += [] {runtime->RenderFrame();};
        plugin::Events::shutdownRwEvent += [] {runtime->Shutdown();};
    }
} bootstrap;
}
