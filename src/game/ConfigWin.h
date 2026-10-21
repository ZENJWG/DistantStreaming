#pragma once
#include "asc/Core.h"
#include "asc/FrameStabilityPolicy.h"
#include "asc/StreamingMemory.h"
#include "asc/CameraReadiness.h"
#include "asc/OffscreenCleanup.h"
#include "GpuResidencyWin.h"
#include "asc/ArchivePreload.h"
#include "AsyncAssetCacheWin.h"
#include "asc/CatalogNativePrefetch.h"
#include "asc/MotionScheduling.h"
#include <cstdint>
#include <string>
struct RuntimeConfig {
    asc::Config core{};
    asc::stability::Settings stability{};
    asc::memory::Settings streamingMemory{};
    asc::camera::Settings cameraReadiness{};
    asc::offscreen::Settings offscreenCleanup{};
    gpu_residency::Settings gpuResidency{};
    asc::archive_preload::Settings archivePreload{};
    asc::external_cache::Settings externalCache{};
    asc::catalog_prefetch::Settings catalogPrefetch{};
    asc::motion::Settings motion{};
    bool enabled=true,telemetry=true,archiveProfile=true;
    // 0=no hooks/scheduler, 1=Request only, 2=Request+Load (legacy).
    unsigned hookMode=0;
    bool hookIntegrityMonitor=true, recoverPristineHooks=true;
    unsigned hookCheckIntervalMs=1000;
    bool controllerEnabled=false;
    // Explicit opt-in for GTA streaming-state telemetry. Disabled by default.
    bool readinessTelemetry=false;
    unsigned stateSampleIntervalMs=100;
    unsigned archiveSampleEvery=16;
    unsigned archiveSlowThresholdUs=1000;
};
RuntimeConfig ReadConfig(const std::string& directory);
