#include "ConfigWin.h"
#include <windows.h>
#include <algorithm>
#include <string>
namespace {
int ini(const std::string&p,const char*g,const char*k,int fallback){
    return GetPrivateProfileIntA(g,k,fallback,p.c_str());
}
}
RuntimeConfig ReadConfig(const std::string&directory){
    RuntimeConfig cfg;
    const std::string p=directory+"\\DistantStreaming.ini";
    cfg.enabled=ini(p,"General","Enabled",1)!=0;
    cfg.hookMode=static_cast<unsigned>(std::clamp(ini(p,"Diagnostics","HookMode",0),0,2));
    cfg.controllerEnabled=ini(p,"Diagnostics","ControllerEnabled",0)!=0;
    cfg.hookIntegrityMonitor=ini(p,"Diagnostics","HookIntegrityMonitor",1)!=0;
    cfg.recoverPristineHooks=ini(p,"Diagnostics","RecoverPristineHooks",1)!=0;
    cfg.hookCheckIntervalMs=std::clamp(
        ini(p,"Diagnostics","HookCheckIntervalMs",1000),250,10000);
    cfg.readinessTelemetry=ini(p,"Diagnostics","ReadinessTelemetry",0)!=0;
    cfg.core.idleScanIntervalMs=std::clamp(
        ini(p,"Prefetch","IdleScanIntervalMs",250),0,1000);
    cfg.stateSampleIntervalMs=std::clamp(
        ini(p,"Diagnostics","StateSampleIntervalMs",100),20,1000);
    auto& motion=cfg.motion;
    motion.enabled=ini(p,"MotionScheduling","Enabled",1)!=0;
    motion.controllerMs=std::clamp(ini(p,"MotionScheduling","FastControllerIntervalMs",20),20,100);
    motion.cameraMs=std::clamp(ini(p,"MotionScheduling","FastCameraIntervalMs",40),40,100);
    motion.holdMs=std::clamp(ini(p,"MotionScheduling","BoostHoldMs",400),100,1000);
    motion.travelSpeed=float(std::clamp(ini(p,"MotionScheduling","FastTravelUnitsPerSecond",35),5,200));
    motion.cameraSpeed=float(std::clamp(ini(p,"MotionScheduling","FastCameraUnitsPerSecond",60),10,400));
    motion.turnDegreesPerSecond=float(std::clamp(ini(p,"MotionScheduling","FastTurnDegreesPerSecond",120),30,720));
    motion.requestsPerSecond=std::clamp(ini(p,"MotionScheduling","RequestsPerSecond",240),1,256);
    motion.requestBurst=std::clamp(ini(p,"MotionScheduling","RequestBurst",12),1,16);
    // All normal D3D9 resources are observed; the plugin does not force VRAM
    // allocation. TargetMiB is a soft residency/streaming-pressure target.
    cfg.gpuResidency.enabled=ini(p,"GPUResidency","Enabled",1)!=0;
    cfg.gpuResidency.framePacingTelemetry=
        ini(p,"GPUResidency","FramePacingTelemetry",0)!=0;
    cfg.gpuResidency.drawCallTelemetry=
        ini(p,"GPUResidency","DrawCallTelemetry",0)!=0;
    cfg.gpuResidency.targetMiB=std::clamp(
        ini(p,"GPUResidency","TargetMiB",8192),512,32768);
    cfg.gpuResidency.headroomMiB=std::clamp(
        ini(p,"GPUResidency","BudgetHeadroomMiB",768),128,4096);
    cfg.gpuResidency.sampleMs=std::clamp(
        ini(p,"GPUResidency","SampleBudgetMs",500),250,5000);
    cfg.gpuResidency.reportMs=std::clamp(
        ini(p,"GPUResidency","ReportIntervalMs",10000),1000,60000);
    cfg.gpuResidency.manageResources=
        ini(p,"GPUResidency","ManageResources",1)!=0;
    cfg.gpuResidency.preloadManaged=
        ini(p,"GPUResidency","PreloadManaged",0)!=0;
    cfg.gpuResidency.managedPriority=std::clamp(
        ini(p,"GPUResidency","ManagedPriority",16),0,256);
    cfg.gpuResidency.prioritizeUsedManaged=
        ini(p,"GPUResidency","PrioritizeUsedManaged",1)!=0;
    cfg.gpuResidency.usedManagedPriority=std::clamp(
        ini(p,"GPUResidency","UsedManagedPriority",64),0,256);
    cfg.gpuResidency.bindPrioritySampleRate=std::clamp(
        ini(p,"GPUResidency","BindPrioritySampleRate",128),32,1024);
    cfg.gpuResidency.preloadMinAgeMs=std::clamp(
        ini(p,"GPUResidency","PreloadMinAgeMs",150),50,2000);
    cfg.gpuResidency.preloadMaxResourceMiB=std::clamp(
        ini(p,"GPUResidency","PreloadMaxResourceMiB",4),1,64);
    cfg.gpuResidency.preloadQueueMiB=std::clamp(
        ini(p,"GPUResidency","PreloadQueueMiB",96),8,512);
    cfg.gpuResidency.preloadJobsPerFrame=std::clamp(
        ini(p,"GPUResidency","PreloadJobsPerFrame",1),1,8);
    cfg.gpuResidency.preloadBudgetUs=std::clamp(
        ini(p,"GPUResidency","PreloadBudgetUs",250),100,2500);
    cfg.gpuResidency.preloadMaxAgeMs=std::clamp(
        ini(p,"GPUResidency","PreloadMaxAgeMs",900),200,5000);
    cfg.gpuResidency.preloadIntervalMs=std::clamp(
        ini(p,"GPUResidency","PreloadIntervalMs",33),0,1000);
    cfg.gpuResidency.preloadMaxAgeMs=std::max(
        cfg.gpuResidency.preloadMaxAgeMs,
        cfg.gpuResidency.preloadMinAgeMs+100u);
    auto& ac=cfg.archivePreload;
    ac.binaryIpls=ini(p,"ArchivePreload","BinaryIPLs",0)!=0;
    ac.models=ini(p,"ArchivePreload","VanillaModelSources",0)!=0;
    ac.enabled=ini(p,"ArchivePreload","Enabled",0)!=0;
    ac.maxCacheMiB=std::clamp(
        ini(p,"ArchivePreload","CacheLimitMiB",192),16,1024);
    ac.maxEntryMiB=std::clamp(
        ini(p,"ArchivePreload","MaxEntryMiB",8),1,32);
    ac.sleepBetweenReadsMs=std::clamp(
        ini(p,"ArchivePreload","ReadPauseMs",5),1,100);
    ac.reportMs=std::clamp(
        ini(p,"ArchivePreload","ReportMs",10000),1000,60000);
    auto& ec=cfg.externalCache;
    ec.worldCatalogEnabled=ini(p,"WorldResidency","Enabled",0)!=0;
    auto& cp=cfg.catalogPrefetch;
    cp.enabled=ec.worldCatalogEnabled &&
        ini(p,"WorldResidency","PrefetchModels",0)!=0;
    cp.maxRequestsPerTick=std::clamp(
        ini(p,"WorldResidency","PrefetchMaxRequestsPerTick",2),1,2);
    cp.maxOutstanding=std::clamp(
        ini(p,"WorldResidency","PrefetchMaxOutstanding",8),1,16);
    cp.nativeBacklogLimit=std::clamp(
        ini(p,"WorldResidency","PrefetchNativeBacklogLimit",16),1,128);
    cp.maxChecksPerTick=std::clamp(
        ini(p,"WorldResidency","PrefetchMaxChecksPerTick",16),1,64);
    cp.maxMemoryPercent=std::clamp(
        ini(p,"WorldResidency","PrefetchMaxMemoryPercent",82),40,95);
    cp.cooldownMs=std::clamp(
        ini(p,"WorldResidency","PrefetchCooldownMs",8000),1000,120000);
    cp.lookAheadMs=std::clamp(
        ini(p,"WorldResidency","PrefetchLookAheadMs",1800),0,6000);
    cp.maxPredictionUnits=float(std::clamp(
        ini(p,"WorldResidency","PrefetchMaxPredictionUnits",320),0,600));
    ec.catalogQueryIntervalMs=static_cast<uint32_t>(std::clamp(
        ini(p,"WorldResidency","CatalogQueryIntervalMs",100),100,3000));
    ec.catalogRescanIntervalMs=static_cast<uint32_t>(std::clamp(
        ini(p,"WorldResidency","RescanIntervalMs",1000),200,15000));
    ec.enabled=ini(p,"ExternalAssetCache64","Enabled",1)!=0;
    ec.adaptiveSourceWarming=ini(p,"ExternalAssetCache64","AdaptiveSourceWarming",1)!=0;
    ec.interceptProcessRead=
        ini(p,"ExternalAssetCache64","InterceptCdStreamReadFile",0)!=0;
    ec.interceptModLoaderReads=
        ini(p,"ExternalAssetCache64","InterceptModLoaderLooseReads",1)!=0;
    ec.serveModLoaderReadyHits=
        ini(p,"ExternalAssetCache64","ServeModLoaderReadyHits",1)!=0;
    ec.maxModLoaderReadMiB=std::clamp(
        ini(p,"ExternalAssetCache64","MaxModLoaderReadMiB",8),1,16);
    ec.interceptCdStream=
        ini(p,"ExternalAssetCache64","InterceptCdStreamRead",1)!=0;
    ec.serveCdStreamHits=
        ini(p,"ExternalAssetCache64","ServeReadyCdStreamHits",0)!=0;
    ec.discoverModLoaderArchives=
        ini(p,"ExternalAssetCache64","DiscoverModLoaderArchives",1)!=0;
    ec.warmObservedModLoaderSources=ec.worldCatalogEnabled &&
        ini(p,"WorldResidency","WarmObservedModLoaderSources",0)!=0;
    ec.warmCatalogDependencies=ec.warmObservedModLoaderSources &&
        ini(p,"WorldResidency","WarmCatalogDependencies",0)!=0;
    ec.warmVerifiedImgMembers=ec.warmCatalogDependencies &&
        ini(p,"WorldResidency","WarmVerifiedImgMembers",0)!=0;
    ec.observedWarmMaxSourceMiB=std::clamp(
        ini(p,"WorldResidency","ObservedWarmMaxSourceMiB",512),16,2048);
    ec.observedWarmMaxTotalMiB=std::clamp(
        ini(p,"WorldResidency","ObservedWarmMaxTotalMiB",2048),64,8192);
    ec.observedWarmMaxSources=std::clamp(
        ini(p,"WorldResidency","ObservedWarmMaxSources",64),1,512);
    ec.backgroundArchiveWarming=
        ini(p,"ExternalAssetCache64","BackgroundArchiveWarming",0)!=0;
    ec.maxCdStreamCopyKiB=std::clamp(
        ini(p,"ExternalAssetCache64","MaxCdStreamCopyKiB",512),32,1024);
    ec.interceptSynchronousRead=
        ini(p,"ExternalAssetCache64","ReadThroughSynchronousIMG",1)!=0;
    ec.helperBudgetMiB=std::clamp(
        ini(p,"ExternalAssetCache64","CacheLimitMiB",8192),256,32768);
    ec.localCacheMiB=std::clamp(
        ini(p,"ExternalAssetCache64","GameLocalCacheMiB",48),8,128);
    ec.inProcessArchiveCacheMiB=std::clamp(
        ini(p,"ExternalAssetCache64","LimitInGameArchiveCacheMiB",32),0,256);
    ec.maxPending=std::clamp(
        ini(p,"ExternalAssetCache64","MaxQueuedReads",64),4,128);
    ec.reportMs=std::clamp(
        ini(p,"ExternalAssetCache64","ReportMs",10000),1000,60000);
    cfg.telemetry=ini(p,"General","Telemetry",1)!=0;
    cfg.archiveProfile=ini(p,"Profiling","ArchiveSelection",1)!=0;
    cfg.archiveSampleEvery=ini(p,"Profiling","SampleEveryNCalls",16);
    cfg.archiveSlowThresholdUs=ini(p,"Profiling","SlowThresholdUs",1000);
    cfg.streamingMemory.enabled=ini(p,"Memory","ManageStreamingMemory",0)!=0;
    cfg.streamingMemory.overrideOla=
        ini(p,"Memory","DistantStreamingOwnsNativeBudget",
            ini(p,"Memory","AdaptiveStreamingOwnsNativeBudget",1))!=0;
    cfg.streamingMemory.limitMiB=ini(p,"Memory","StreamingMemoryLimitMiB",512);
    cfg.streamingMemory.reserveMiB=ini(p,"Memory","AddressSpaceReserveMiB",512);
    auto& stable=cfg.stability;
    stable.enabled=ini(p,"FrameStability","Enabled",1)!=0;
    stable.allowBlockingCameraLoads=
        ini(p,"FrameStability","AllowBlockingCameraLoads",0)!=0;
    stable.controllerIntervalMs=ini(p,"FrameStability","ControllerIntervalMs",120);
    stable.slowControllerUs=ini(p,"FrameStability","SlowControllerUs",4000);
    stable.slowRetryMs=ini(p,"FrameStability","SlowControllerRetryMs",350);
    stable.maxPredictiveCells=ini(p,"FrameStability","MaxPredictiveCells",24);
    stable.maxPredictiveCandidates=ini(p,"FrameStability","MaxPredictiveCandidates",96);
    auto& camera=cfg.cameraReadiness;
    camera.enabled=ini(p,"CameraReadiness","Enabled",0)!=0;
    camera.blocking=ini(p,"CameraReadiness","BlockingLoad",0)!=0;
    camera.instanceLoaded=ini(p,"CameraReadiness","InstanceLoaded",1)!=0;
    camera.intervalMs=ini(p,"CameraReadiness","IntervalMs",100);
    camera.maxModels=ini(p,"CameraReadiness","MaxModelsPerBatch",8);
    camera.maxInstances=ini(p,"CameraReadiness","MaxInstancesPerBatch",32);
    camera.maxBatchSectors=ini(p,"CameraReadiness","MaxBatchSectors",16384);
    camera.maxScanCells=ini(p,"CameraReadiness","MaxScanCells",64);
    camera.maxScanNodes=ini(p,"CameraReadiness","MaxScanNodes",3072);
    camera.maxScanUs=ini(p,"CameraReadiness","MaxScanUs",3000);
    camera.range=float(ini(p,"CameraReadiness","RangeUnits",300));
    camera.halfAngleDegrees=float(ini(p,"CameraReadiness","HalfAngleDegrees",70));
    auto& cleanup=cfg.offscreenCleanup;
    cleanup.enabled=ini(p,"OffscreenCleanup","Enabled",0)!=0;
    cleanup.disableBlockingLoads=ini(p,"OffscreenCleanup","DisableBlockingLoads",1)!=0;
    cleanup.unloadModels=ini(p,"OffscreenCleanup","UnloadUnusedModels",1)!=0;
    cleanup.unloadTextures=ini(p,"OffscreenCleanup","UnloadUnusedTextures",1)!=0;
    cleanup.maxScanPerFrame=ini(p,"OffscreenCleanup","MaxScanPerFrame",512);
    cleanup.maxDeletesPerFrame=ini(p,"OffscreenCleanup","MaxDeletesPerFrame",4);
    cleanup.maxPressureDeletes=ini(p,"OffscreenCleanup","MaxPressureDeletesPerFrame",8);
    cleanup.maxUnloadsPerFrame=ini(p,"OffscreenCleanup","MaxModelUnloadsPerFrame",1);
    cleanup.timeBudgetUs=ini(p,"OffscreenCleanup","TimeBudgetUs",1000);
    cleanup.graceMs=ini(p,"OffscreenCleanup","OffscreenGraceMs",250);
    cleanup.requestQuietMs=ini(p,"OffscreenCleanup","RequestQuietMs",1000);
    cleanup.maxUnloadSectors=ini(p,"OffscreenCleanup","MaxUnloadSectors",4096);
    cleanup.keepNearUnits=float(ini(p,"OffscreenCleanup","KeepNearPlayerUnits",80));
    cleanup.helperFirstTrim=ini(p,"OffscreenCleanup","HelperFirstNativeTrim",0)!=0;
    cleanup.trimBeginHeadroomMiB=ini(p,"OffscreenCleanup","TrimStartHeadroomMiB",256);
    cleanup.trimStopHeadroomMiB=ini(p,"OffscreenCleanup","TrimStopHeadroomMiB",512);
    cleanup.trimMinHelperCachedMiB=ini(p,"OffscreenCleanup","TrimMinHelperCacheMiB",64);
    cleanup.trimMaxBacklog=ini(p,"OffscreenCleanup","TrimMaxNativeBacklog",24);
    cleanup.trimScanPerPass=ini(p,"OffscreenCleanup","TrimScanPerPass",256);
    cleanup.trimStableMs=ini(p,"OffscreenCleanup","TrimStableMs",5000);
    cleanup.trimIntervalMs=ini(p,"OffscreenCleanup","TrimIntervalMs",200);
    if(cleanup.enabled&&cleanup.disableBlockingLoads){
        camera.blocking=false;camera.maxInstances=std::min(camera.maxInstances,4u);
    }
    // A legacy user INI may explicitly request synchronous camera loads.
    // Stabilization is opt-out and overrides that dangerous path by default.
    if(stable.enabled){
        if(!stable.allowBlockingCameraLoads)camera.blocking=false;
        camera.maxInstances=std::min(camera.maxInstances,4u);
        camera.maxScanCells=std::min(camera.maxScanCells,16u);
        camera.maxScanNodes=std::min(camera.maxScanNodes,768u);
        camera.maxScanUs=std::min(camera.maxScanUs,1500u);
    }
    auto&c=cfg.core;
    c.prefetch=ini(p,"Prefetch","Enabled",0)!=0;
    c.tickMs=ini(p,"General","UpdateIntervalMs",50);
    c.memoryThrottlePercent=ini(p,"Memory","ThrottlePercent",85);
    c.memoryHighPercent=ini(p,"Memory","HighPercent",92);
    c.memoryResumePercent=ini(p,"Memory","ResumePercent",80);
    c.workingSetLimitMiB=ini(p,"Memory","MaxWorkingSetMiB",0);
    c.privateLimitMiB=ini(p,"Memory","MaxPrivateMiB",0);
    c.minAvailablePhysicalMiB=ini(p,"Memory","MinPhysicalAvailableMiB",256);
    c.maxRequestsPerTick=ini(p,"Prefetch","MaxRequestsPerTick",8);
    c.maxAdaptiveRequests=ini(p,"Prefetch","MaxAdaptiveRequests",int(std::min(8u,c.maxRequestsPerTick)));
    c.maxPriorityPerTick=ini(p,"Prefetch","MaxPriorityRequestsPerTick",2);
    c.priorityRadiusUnits=float(ini(p,"Prefetch","PriorityRadiusUnits",100));
    c.maxOutstanding=ini(p,"Prefetch","MaxOutstanding",24);
    c.backlogLimit=ini(p,"Prefetch","BacklogLimit",24);
    c.maxCells=ini(p,"Prefetch","MaxSectorScans",64);
    c.maxCandidates=ini(p,"Prefetch","MaxCandidates",256);
    c.expiryMs=ini(p,"Prefetch","ExpiryMs",3500);
    c.cooldownMs=ini(p,"Prefetch","CooldownMs",6000);
    c.lookAheadSeconds=ini(p,"Prefetch","LookAheadMs",3000)/1000.0f;
    c.predictClampUnits=float(ini(p,"Prefetch","MaximumPredictionUnits",300));
    c.scanRadiusUnits=float(ini(p,"Prefetch","SectorRadiusUnits",150));
    c.maxTeleportUnits=float(ini(p,"Prefetch","TeleportThresholdUnits",120));
    c.targetFrameMs=ini(p,"Performance","SlowFrameMs",25);
    c.frameRecoveryMs=ini(p,"Performance","CautionFrameMs",18);
    c.slowLoadUs=ini(p,"Performance","SlowLoadingUs",12000);
    c.dynamicWarmup=ini(p,"DynamicReadiness","Enabled",0)!=0;
    c.dynamicSlotsPerTick=ini(p,"DynamicReadiness","MaxRequestsPerTick",1);
    c.dynamicHistoryMs=ini(p,"DynamicReadiness","HistoryMs",10000);
    c.dynamicMinObservations=ini(p,"DynamicReadiness","MinObservations",2);
    c.dynamicMinRepeatGapMs=ini(p,"DynamicReadiness","MinRepeatGapMs",350);
    c.dynamicRewarmCooldownMs=ini(p,"DynamicReadiness","RewarmCooldownMs",15000);
    c.dynamicLocalRadiusUnits=float(
        ini(p,"DynamicReadiness","LocalRadiusUnits",120));
    c.dynamicMaxHistory=ini(p,"DynamicReadiness","MaxHistoryModels",32);
    const unsigned n=cfg.archiveSampleEvery;
    if(n<1||n>1024||(n&(n-1))!=0 ||
       cfg.archiveSlowThresholdUs<10||cfg.archiveSlowThresholdUs>100000)
        cfg.enabled=false;
    if(!asc::Controller::Valid(c))cfg.enabled=false;
    if(!asc::catalog_prefetch::Valid(cp))cfg.enabled=false;
    // The catalog bridge needs a verified game-side scheduler and a running x64
    // helper. An explicitly disabled path stays disabled, never half-active.
    cp.enabled=cp.enabled && ec.enabled && cfg.controllerEnabled && c.prefetch;
    if(!asc::stability::Valid(stable))cfg.enabled=false;
    if(!asc::memory::Valid(cfg.streamingMemory))cfg.enabled=false;
    if(!asc::camera::Valid(camera))cfg.enabled=false;
    if(!asc::offscreen::Valid(cleanup))cfg.enabled=false;
    return cfg;
}
