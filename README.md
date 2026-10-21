# DistantStreaming — GTA San Andreas native streaming and asset caching

DistantStreaming is a **32-bit GTA San Andreas 1.0 US ASI** with an optional
**64-bit source-byte cache helper**. Its responsibilities are native streaming
requests, predictive scheduling, streaming-memory limits, asset-read caching,
safe cleanup, OLA compatibility and performance diagnostics.

**Rendering / Project2DFX separation:** This project **does not include a
draw-distance controller**. It does not change model IDE draw distances,
camera far clip, fog start, extended normal-world or LOD sector traversal,
distant lighting, or render visibility. Those features belong to the game
and dedicated visual mods such as
[Project2DFX-zen](https://github.com/ZENJWG/III.VC.SA.IV.Project2DFX-zen).

The earlier `DrawDistanceWin` implementation, its policy/configuration and
dedicated renderer tests have been removed. Existing `[DrawDistance*]` sections
in older INI files are no longer recognized. Remove them from existing installs
when migrating.

## With SanFrameOptimizer

The supplied INI profiles leave DistantStreaming's optional D3D9
Present/EndScene frame probes disabled to prevent duplicate interception.
GPU resource accounting, managed-resource priority hints, native streaming,
and Cache64 remain enabled. SanFrame's Present-only OSD handles the display.
See [coexistence guide](docs/SANFRAME_DISTANTSTREAMING_COEXISTENCE.md).

## What remains

- **Native streaming:** request observation, optional predictive/camera-aware
  model requests, request coalescing and pressure-aware pacing. This requests
  assets without modifying where or how GTA and other mods render them.
- **Memory:** address-space-aware streaming budgets, OLA binding validation,
  resource residency measurements and optional guarded offscreen cleanup.
- **AssetCache64:** optional x64 file-byte/page cache with bounded x86-side
  staging, GTA CdStream observation and Mod Loader read-through.
- **GPU accounting:** D3D9 allocation tracking, driver budget telemetry,
  optional managed-resource priority hints. This does not replace D3D9's
  resource allocator or move RenderWare objects to another process.
- **Diagnostics:** CSV/log telemetry for streaming, model request call sites,
  memory, helper hit rates, frame pacing and sampled D3D9 draw-call counts.
  The D3D9 draw-call hook is **observational** and does not suppress or
  modify draws or change renderer distances.

## Current World Residency behavior

The supplied `config/DistantStreaming.ini` enables the experimental x64 world
catalog and nonblocking native model prefetch with `[WorldResidency] Enabled=1`,
`PrefetchModels=1` and `SpeedPreset=3` (very fast), with explicit high-end
request limits. The primary INI targets 32 GB+ system RAM and a 12 GB VRAM
ceiling profile. See [current high-end tuning](docs/HIGH_END_TUNING_2026-10-20.md).
Explicit request overrides take precedence over `SpeedPreset`; remove those
overrides before using preset 1 or 2 alone. `PrefetchModels=0` retains catalog
diagnostics without catalog-driven native requests.

The helper inventories supported text and binary IPL placements, including
VER2 IMG entries. Catalog replies are hints: the game thread validates model
names and IDs against live GTA/OLA tables before requesting anything. Request,
CPU-time, backlog, frame-time, GPU-pressure and free-address-space guards apply.
Periodic current-cell queries rotate batches of up to 64 models so crowded
cells can expose models beyond the first batch. Demand cache reads take
priority over optional catalog and cache-statistics polling on the worker.

Native prefetch does not create every world entity or pin every model.
Existing-entity `[AssetPreload]` materialization is enabled in the high-end
primary INI, with request, pin, creation-time and free-VA limits. Catalog
coverage is not a complete, authoritative map of
Mod Loader overrides or effective world assets.

Observed-source warming, dependency warming and verified IMG-member warming
are enabled in the high-end primary INI. These paths use
observed live Mod Loader-selected handles. IMG warming also validates the
`VER2` directory, observed read and model/TXD hints before warming intersecting
1 MiB pages. See [verified IMG-member warming](docs/WORLD_RESIDENCY_V6.md).

Current transport protocol is **v10**. Rebuild and deploy the Win32 ASI and
x64 helper together. Cache disk reads share a 1500 ms budget per read request
or IMG-directory read sequence; the ASI request/reply exchange shares a
5000 ms transport deadline. Cancellation completion must still be drained
before releasing an overlapped I/O buffer, so these are not hard wall-clock
limits for an unresponsive storage driver.

The helper caches **source bytes**, not live game models or GPU objects. GTA
remains a 32-bit process and retains its address-space limitations.

## Building

Use `build.bat` on Windows with MSVC, the supported Plugin-SDK checkout,
and the documented dependencies. The game ASI requires **Win32/x86**; the
cache helper requires **x64**. Project targets are defined in `CMakeLists.txt`.

Deployment outputs are normally:
- `DistantStreaming.asi`
- `DistantStreaming.ini`
- `DistantStreaming_AssetCache64.exe`

Use ASI and helper binaries built from the **same revision**.
The source archive is not a prebuilt Windows release.

Portable tests (outside Windows) can be run with:

```bash
cmake -S . -B build-test -DASC_BUILD_ASI=OFF -DASC_BUILD_TESTS=ON
cmake --build build-test --parallel
ctest --test-dir build-test --output-on-failure
```

The portable test build does **not** validate Windows hooks or actual GTA
gameplay; use the Windows build and in-game smoke tests before deployment.

## Configuration and compatibility

The primary config is `config/DistantStreaming.ini`. Other supplied
configurations are specialized memory/GPU/cache profiles. No supplied profile
contains draw-distance or visual-distance overrides.

`DistantStreaming_GLOBAL5_EXPERIMENT.ini` now mirrors the high-end primary
profile; its legacy filename does not enable a 5x render distance. Remaining
alternative profiles retain their prior tuning with shorter comments and a
repaired `MaxModLoaderReadMiB` line. Original INIs for this pass are retained
under `.review-backups/2026-10-20-high-end/config/`.

To isolate the predictive controller, use
`[Diagnostics] ControllerEnabled=0` while retaining any separately enabled
native budget or GPU diagnostics. The `HookMode` and
`[ExternalAssetCache64]` settings determine active interception/caching.
More detailed descriptions are in the remaining `docs/` files.

For Project2DFX integration, allow Project2DFX to own draw visibility and
effects; use DistantStreaming to observe/satisfy native streaming demand and
manage optional caching within memory pressure limits. Avoid redundant
whole-map/category preloading in both mods without measuring its impact.

## Validation and limits

See the [2026-10-20 optimization review](docs/OPTIMIZATION_REVIEW_2026-10-20.md)
for current fixes, Windows build/test results and remaining profiling work.

The [EXE and gta-reversed follow-up](docs/EXE_AND_REVERSED_REVIEW_2026-10-20.md)
records the supplied executable's layout and further retention, scan and hook fixes.

The [additional scheduler/cache pass](docs/SCHEDULER_CACHE_OPTIMIZATION_2026-10-20.md)
reduces work under memory pressure and fixes reload cooldown and worker-failure handling.

Compare identical game scenarios with and without the ASI, checking
P50/P95/P99 frame times, indexed draws, request/backlog metrics, free
virtual address space, native streaming headroom, helper cache hit ratios,
and visible pop-in. Profiling D3D9 draw calls does not establish GPU time
or which plugin submitted those calls.

Historical `CHANGELOG.md` entries and some older engineering notes describe
features removed from this revision; they are not current functionality.


## Historical engineering notes

These documents describe earlier milestones. Their protocol numbers and
default settings are historical; use the current behavior above and the
supplied INI for this revision.

- [A+C catalog pilot](docs/WORLD_RESIDENCY_PILOT.md) and [original specification](docs/SPEC-1-World-Residency-A-C.md)
- [V2 binary IPL and native prefetch](docs/WORLD_RESIDENCY_V2.md)
- [V3 observed-source warming and candidate graph](docs/WORLD_RESIDENCY_V3.md)
- [V4 persistent catalog cache](docs/WORLD_RESIDENCY_V4.md)
- [V5 dependency-aware warming](docs/WORLD_RESIDENCY_V5.md)

## Experimental native catalog prefetch

The supplied `config/DistantStreaming.ini` now enables bounded, catalog-driven
`[WorldResidency] PrefetchModels=1`. It validates helper hints against GTA's
live model names and IDs, then uses at most two normal native requests per
frame, sharing the core controller's budget. It backs off during frame-time,
native queue and memory pressure. Set `PrefetchModels=0` to disable it without
rebuilding. Profiles without the key stay off. See
[the implementation and A/B guide](docs/CATALOG_NATIVE_PREFETCH_MVP.md).
