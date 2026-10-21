# DistantStreaming v16 — HOODLUM cache reliability and verification

## Scope

This bundle is a **source-cache enhancement** for the existing GTA SA 1.0
HOODLUM / original D3D9 / RenderWare ASI. It is NOT a 64-bit replacement
for RenderWare and **cannot** make all decoded geometry, collision, trees,
materials or IPL instances live inside a 32-bit process simultaneously.

The separate x64 `DistantStreaming_AssetCache64.exe` caches **original source bytes**; GTA
still materializes and evicts native models subject to x86 virtual-address
limits. Keeping every source file resident does not prove zero pop-in.

## Changes since v15

1. `src/service64/NativeMemoryService.cpp`: speculative warm jobs retry
   a failed page at 200/400/800/1600/3200 ms rather than permanently
   abandoning the archive on its first transient failure. A 60-second
   repair sweep revisits missing pages only when **all registered page
   allocations can fit** the current helper budget. A capped retry count
   avoids a disk-error spin loop. Memory pressure still trims the cache.
2. `AssetCache::SourceSize`: snapshot current file metadata on every warm job
   so EOF is distinct from a temporary cache-read failure. The cache's
   generation key still includes file identity, size, and modification time.
3. `src/service64/NativeMemoryService.cpp`: successful new resource allocations
   trigger cache trimming. A failed backing allocation no longer evicts
   existing pages before the allocation has succeeded. Exceptional warm-source
   queue registration cleans up the source instead of terminating the helper.
4. `src/game/AsyncAssetCacheWin.cpp`: periodic telemetry now actually emits
   discovered/registered/rejected IMG candidates, resident/expected warm
   pages, candidate coverage %, dedup waits, deferrals and failures.
   `registeredSourcesFullyResident` only means **registered IMG candidate
   pages**; it cannot prove effective Mod Loader override coverage.
5. HOODLUM unknown CdStreamRead signatures remain **fail-closed**; direct
   channel-state completion (`ServeReadyCdStreamHits`) remains OFF by default.
   Request-only `HookMode=1` is retained to avoid an unverified Load detour.
6. `config/DistantStreaming.ini`: GPU soft target raised to 18 GiB with 2 GiB
   headroom for the specified 24 GiB GPU. The actual local budget reported by
   WDDM wins; this never forces driver allocation. Updated both config and
   runtime GPU clamps from 16 GiB to 32 GiB so the 18 GiB value actually
   takes effect; existing lower-cap GPUs remain constrained by WDDM.
7. New `include/asc/WorldWarmPolicy.h` and portable tests. Extended
   `tests/asset_cache_windows_tests.cpp` covers source-size refresh and page
   repair following trim and file modification.
8. New `tools/world_source_manifest.py`: optional **offline** candidate
   inventory under `models/`, `modloader/`, and `data/`, including loose
   asset files. This is separate from the runtime warm cache and does not
   change file precedence. Duplicate basenames are flagged as *potential*
   overrides only.
9. `verify-windows.ps1` builds the ASI + helper using the project's existing
   dependency bootstrap, configures an independent MSVC x64 test build, and
   runs CTest including the native file-I/O integration test.

## Build and test on Windows (required)

Prerequisites: the Visual Studio/CMake environment documented in
`tools/AUTO_DEPENDENCIES.md`, PowerShell 5.1+, Windows SDK, and plugin-sdk
download permissions (unless dependencies were bootstrapped previously).

From PowerShell in the extracted source root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\verify-windows.ps1
```

Or, to build only:

```cmd
build.bat
```

Expected build artifacts are `dist\DistantStreaming.asi` (x86),
`dist\DistantStreaming_AssetCache64.exe` (x64), and `dist\DistantStreaming.ini`.
**Install the helper and ASI from the same bundle**; the existing IPC
protocol version is **3**, not backward compatible with older helpers.

The Windows-only tests exercise actual overlapped reads, source changes,
concurrent warming/demand reads, cache trimming and recovery. A passing
Windows build does NOT prove in-game hook compatibility.

## Optional source inventory (Python 3)

```powershell
python .\tools\world_source_manifest.py `
    --game "C:\Games\GTA San Andreas" `
    --output "world-source-manifest.json"
```

The inventory includes candidate IMG, DFF, TXD, IDE, IPL, COL and DAT files
under the three scan roots. It is **not** a definitive source-resolution
manifest. It does not parse binary IPL contents, enumerate off-root sources,
or resolve effective Mod Loader precedence. Compare its IMG count and bytes
with `discoveredIMG`, `registeredIMG`, and `warmPages` from the ASI log.
Remember that duplicate aliases can reduce the number of *unique* archives
reported by the helper.

## HOODLUM runtime smoke test

1. Back up prior ASI/helper/INI and save files. Do not mix v15 and v16 binaries.
2. Keep `HookMode=1`, `ForceWorldInstances=0`,
   `ServeReadyCdStreamHits=0`, and `D3D9.Mode=Direct`.
3. Launch the exact supported HOODLUM build, initially without other ASIs.
   Confirm hook preflight either accepts the known entry **and relocated-body**
   signatures or declines safely.
4. Drive/teleport across LS, SF, LV and rural areas twice, recording loading
   artifacts, frame-time p99, peak virtual address, GPU budget and helper stats.
5. Check whether `registeredIMG` reaches `discoveredIMG`, then whether
   `warmPages` reaches expected pages **without memory pressure**. If the
   source set exceeds 32 GiB, it cannot be all resident in this cache.
6. Verify save/load, interior transitions, weather/time changes, graphics
   reset, 30-minute driving test and clean shutdown. Repeat with Mod Loader
   and other ASIs re-enabled incrementally.
7. If any crash, disable external cache first. Keep native loader fallback.
   Do not force unknown executable signatures or adjust model pinning limits
   to 4 GiB.

## Verification in authoring environment

* Portable Linux CMake/Ninja C++23 build: PASS.
* Portable CTest suite: 24/24 (including warm policy and offline inventory).
* MSVC x86 `.asi` and x64 `.exe` build: **NOT RUN** (no Windows SDK here).
* Windows native concurrency test: **NOT RUN**.
* HOODLUM live runtime / D3D9 / FPS / 24 GiB GPU validation: **NOT RUN**.

Do not label this update production verified until the Windows test suite and
gameplay smoke tests have been completed on the target environment.
