# DistantStreaming HOODLUM world-source cache stabilization — 2026-10-17

## Purpose and explicit limits

This is a **source-archive residency** update, not a new renderer. The x64
`DistantStreaming_AssetCache64.exe` retains IMG source pages outside GTA's 32-bit process.
It does **not** make every decoded RenderWare object, texture, tree, collision
mesh, or IPL instance simultaneously live in `gta_sa.exe`. Native streaming,
LOD selection, missions, and ordinary eviction still run in GTA. A 32-bit
Large Address Aware GTA process still has an x86 address-space ceiling.

`CacheCoverage` covers **successfully registered IMG candidates only**. It
does NOT prove all installed assets or all mod overrides were discovered.
Loose DFF/TXD/IPL files, externally redirected archives, and archives
outside the scanned directories are not fully indexed or permanently cached.
Mod Loader effective precedence continues to be determined by GTA/Mod Loader.
No performance improvement or frame-rate guarantee is asserted without a
repeatable in-game test.

## Changes

* `AssetCache.cpp`: immutable source pages with shared ownership, keyed by
  volume / file index / file size / last write time; deduplicated per-key
  loads; no global cache mutex during potentially 3-second `ReadFile` waits;
  up to 8 MiB concurrent in-flight loads; 200 ms bounded wait for a
  demand read colliding with a speculative load; safe original GTA
  fallback on timeout/failure. Perform a cache eviction only after a
  replacement page has been read and checked successfully.
* `NativeMemoryProtocol.h` is now **version 3**. ASI and helper MUST be
  installed from the same bundle. `OpenWarmSource` registers persistent
  prewarm sources separately from transient `OpenSource` reads.
  `CacheCoverage` reports page/size coverage, deferrals, I/O failures, waits.
* `AsyncAssetCacheWin.cpp`: bounded candidate scan under `models/` and
  (optionally) `modloader/`, at most 3,072 IMG paths and 65,536 directory
  entries per root. Batches 4 warm sources at a time on the existing worker,
  never the GTA render thread. The helper reserves the remaining source
  handle slots for native demand reads. New log fields:
  `discoveredIMG`, `registeredIMG`, `rejectedIMG`, `warmPages`,
  `cacheWaits`, `cacheDeferred`, `cacheFailures`.
* HOODLUM `CdStreamRead` hook requires the known entry **and** relocated-body
  fingerprint before detouring, even on executable pages. Unknown/modified
  entries are left untouched. The risky direct `CdStreamRead` channel
  completion remains available as an **explicit opt-in**, but is now OFF
  by default; normal GTA native sector reads continue.
* `StreamingMemoryWin.cpp`: logs largest contiguous free VA every 10 s
  outside the fast frame-boundary path and pauses speculative admission
  when the contiguous free region is below 256 MiB.
* `WorldCachePolicy.h` adds overflow-safe page count and capacity helpers.
  New Linux-portable policy tests and a Windows x64 real-file concurrency
  and source-generation regression test.

## Installation

1. Back up existing `DistantStreaming.asi`, `DistantStreaming_AssetCache64.exe`,
   `DistantStreaming.ini`, and game saves.
2. Use Visual Studio 2026 + Windows PowerShell to run `build.bat` at the
   source root. The existing bootstrap builds Win32 ASI and x64 helper;
   see `docs/BUILD_VALIDATION.md` for environment requirements.
3. Deploy **both** `DistantStreaming.asi` and `DistantStreaming_AssetCache64.exe` from
   the same `dist/` build. Do not combine an older v2 helper with the
   v3 ASI. Merge the `config/DistantStreaming.ini` settings into your
   active INI without overwriting unrelated mod-specific values.
4. Validate the HOODLUM entry/body fingerprints reported in logs. If the
   native CdStream detour is rejected, leave it rejected and use native GTA
   streaming. Do not force unknown hook addresses.
5. Test an empty/default game first, then install other ASIs and Mod Loader.
   Test the same camera/teleport route before and after, recording frame
   times, `cdEligible`, `cdQueued`, `helperCachedMiB`, and `warmPages`.
6. For Windows x64 helper integration tests, configure x64 with
   `ASC_BUILD_TESTS=ON`, then run `ctest -C Release --output-on-failure`
   from that build directory. The portable tests can run on Linux.

## Configuration for 64 GiB RAM

```ini
[ExternalAssetCache64]
Enabled=1
InterceptCdStreamRead=1
ServeReadyCdStreamHits=0
CacheLimitMiB=32768
DiscoverModLoaderArchives=1
GameLocalCacheMiB=48
MaxQueuedReads=64
```

`CacheLimitMiB` is a helper-side ceiling, NOT an allocation. Under OS memory
pressure the helper stops warming and trims pages. `ServeReadyCdStreamHits=1`
should be attempted only after an in-game channel-state audit for your exact
HOODLUM binary. GTA native model pinning limits must **not** be set to 4096 MiB.

## Known remaining issues / non-goals

* No live GTA/Windows run was possible in the source-editing environment.
  Portable CMake tests passed; Windows ASI/service linking, runtime
  hook compatibility, Direct3D9 and GPU usage require local verification.
* Mod Loader loose files, binary IPL placements, effective override order,
  off-directory archives, and native RenderWare decoding are not covered
  by the external page-cache manifest. The script reports only source
  candidate coverage. A complete all-world *decoded* resident renderer
  requires a separate rendering architecture.
* The helper remains a **single IPC connection**. Parallel metadata access
  and warm reads no longer lock each other, but foreground requests are
  still serialized at IPC dispatch.
* GTA local cache uses exact `(file, offset, length)` matches and consumes
  bounded 32-bit VA. A warmed remote source is not necessarily immediately
  available as a direct local game-thread hit.
* Cached source data is invalidated on file metadata identity changes.
  Exotic filesystems with unreliable file identities should fall back
  to native reads.
* If the complete set of registered IMG pages exceeds the helper ceiling,
  `warmPages` will remain below the total; do not report complete world
  residency in that situation.
