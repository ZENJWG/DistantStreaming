#pragma once
#include <cstdint>
#include <ostream>
#include <string>
#include "asc/NativeMemoryProtocol.h"
namespace asc::external_cache {
struct Settings {
    bool enabled=false;
    bool worldCatalogEnabled=false;
    uint32_t catalogQueryIntervalMs=100;
    uint32_t catalogRescanIntervalMs=1000;
    bool interceptSynchronousRead=true;
    bool interceptProcessRead=false; // Legacy opt-in; Windows API route proved ineffective
    bool interceptModLoaderReads=true; // Per-module std.stream.dll ReadFile IAT: actual loose-source provenance
    bool serveModLoaderReadyHits=true; // Exact complete reads only, from already resident 32-bit L1
    uint32_t maxModLoaderReadMiB=8; // Bound worker memcpy/local L1 footprint
    bool interceptCdStream=true; // GTA SA CdStreamRead(0x406A20), model source boundary
    bool serveCdStreamHits=false; // Complete idle GTA stream channels from ready local cache
    bool adaptiveSourceWarming=true; // Back off low-reuse sources; native reads always proceed
    uint32_t maxCdStreamCopyKiB=512; // Bound main-thread memcpy latency
    bool discoverModLoaderArchives=true;
    // Promote ONLY sources actually selected by the live Mod Loader ReadFile
    // hook to bounded x64 background warming. Never scan override priority.
    bool warmObservedModLoaderSources=false;
    // V5: further restrict warming to observed loose DFF/TXD sources
    // referenced by current catalog-cell hints. No IMG speculation.
    bool warmCatalogDependencies=false;
    // V6 opt-in: allow only actually observed IMG members selected by Mod
    // Loader; verified on the x64 helper. Never warm an entire IMG by hint.
    bool warmVerifiedImgMembers=false;
    uint32_t observedWarmMaxSourceMiB=512;
    uint32_t observedWarmMaxTotalMiB=2048;
    uint32_t observedWarmMaxSources=64;
    bool backgroundArchiveWarming=false; // Demand-only by default; full IMG warming consumed >12 GiB with zero eligible reads
    uint32_t helperBudgetMiB=8192;
    uint32_t localCacheMiB=48;
    uint32_t inProcessArchiveCacheMiB=32; // 0 keeps legacy INI cache size
    uint32_t maxPending=64;
    uint32_t reportMs=10000;
};
void Start(const std::string& asiDirectory,Settings settings,std::ostream& log);
void Report(uint32_t now,std::ostream& log);
// Read-only, atomic cache-health check.  Does not imply every GTA model has
// a corresponding ready source page, and never performs IPC on the game thread.
bool ReadyForNativeReclaim(unsigned minimumCachedMiB) noexcept;
// Nonblocking: only writes atomic coordinates for the cache worker.
void WorldPositionHint(float x,float y) noexcept;
// Nonblocking main-thread read of hints. False means unavailable/unchanged/busy.
bool TryCatalogHints(asc::native::CatalogCellReply& out,
                     uint64_t& generation) noexcept;
void Stop();
} // namespace asc::external_cache
