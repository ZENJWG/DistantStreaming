#pragma once
#include <cstdint>
#include <iosfwd>
#include <d3d9.h>
#include "asc/RenderCostProbe.h"

namespace gpu_residency {
struct Settings {
    bool enabled=true;
    bool drawCallTelemetry=false; // CPU timing 1/128 D3D9 draws, forwards untouched
    bool framePacingTelemetry=false; // CPU wall-time D3D9 Present/EndScene, no changes to pacing
    uint32_t targetMiB=8192;
    uint32_t headroomMiB=768;
    uint32_t sampleMs=500;
    uint32_t reportMs=10000;
    // Real D3D9 managed-resource residency hints; original allocation pool preserved.
    bool manageResources=true;
    bool preloadManaged=false; // synchronous driver hint; explicit opt-in only
    uint32_t managedPriority=16;
    // Rare, on-bind priority promotion of existing managed textures/buffers.
    // Never converts resource pools or drops CPU backing copies.
    bool prioritizeUsedManaged=true;
    uint32_t usedManagedPriority=64;
    uint32_t bindPrioritySampleRate=128;
    uint32_t preloadMinAgeMs=150;
    uint32_t preloadMaxResourceMiB=4;
    uint32_t preloadQueueMiB=96;
    uint32_t preloadJobsPerFrame=1;
    uint32_t preloadBudgetUs=250;
    uint32_t preloadMaxAgeMs=900;
    uint32_t preloadIntervalMs=33;
};
// Callback overhead is measured independently from total GTA frame work.
uint64_t BeginCallbackSample() noexcept;
void EndCallbackSample(bool drawing,uint64_t startedUs) noexcept;
// RenderFrame stage timings; no filesystem I/O or GPU queries in the callback.
void EndRenderStage(asc::render_cost::Stage stage,uint64_t startedUs) noexcept;
void Configure(Settings settings);
void Frame(IDirect3DDevice9* device, uint32_t nowMs,bool streamingPressure,std::ostream& log);
bool UnderPressure() noexcept;
void Stop();
}
