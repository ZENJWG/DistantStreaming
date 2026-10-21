## 2026-10-20 — High-end loading profile and recurring CPU work

- Reuse predictive/camera sector-planning buffers and dynamic-candidate storage.
- Poll ready catalog inventory at 1 Hz independently of fast cell queries.
- Skip new-pin classification/model scans when a non-rotating pin cache is full;
  retain pressure recovery, retries and bounded entity materialization.
- Tune the primary and GLOBAL5 INIs for aggressive loading on 32 GB+ RAM / 12 GB
  VRAM systems, including verified observed-source warming and bounded instancing.
- Trim repeated comments from DistantStreaming INIs, remove unused legacy keys,
  and repair the escaped newline that hid `MaxModLoaderReadMiB` in a comment.
- Preserve original INIs in `.review-backups/2026-10-20-high-end/config/`.
- Win32 ASI and x64 helper MSVC Release builds passed; matching artifacts and
  the primary high-end INI were packaged in `dist`.
- No tests or gameplay benchmarks run for this pass, as requested.

## 2026-10-20 — Demand priority and catalog coverage

- Defer optional catalog/statistics RPCs while demand cache jobs are queued,
  including checks between consecutive optional RPCs.
- Refresh stationary-player catalog hints at the configured rescan interval;
  rotate placement cursors across 64-model batches without changing protocol v10.
- Share a 1500 ms disk-I/O budget across page reads or IMG-directory chunks,
  and a 5000 ms ASI transport deadline across request/reply stages.
  Overlapped cancellation is drained before buffers are released.
- Maintain resident-page counts by source identity so warm coverage reporting
  no longer scans the full page index under the metadata mutex.
- Consolidate current README behavior and separate historical milestone notes.
- MSVC Release builds of the Win32 ASI and x64 helper succeeded; `build.bat fast`
  refreshed the matching binaries and ASI symbols in `dist`.
- Tests were not run for this pass, as requested. Gameplay effects are unmeasured.

## V6.2 — fast model loading profiles (2026-10-19)

- Added INI speed presets (balanced/fast/very fast) for optional verified
  catalog-native requests, with independent request/check/time/rate limits.
- Shortened configurable x64 catalog query interval; added current-cell
  rescan to reconsider missing models after successful prior requests.
- Enforced live native backlog/streaming budget checks and fixed the initial
  model cooldown that could delay first-frame prefetch.
- Supplied an opt-in very-fast catalog prefetch profile and documented how
  to separately enable bounded existing-world-entity materialization.
- No Project2DFX draw-distance, LOD, or visibility functionality added.
- Portable build and 42/42 CTest pass; Windows build/playtest not verified.

## World Residency V6.1 — Windows build repair (2026-10-19)

- Fixed MSVC C2672 from mixed signed/unsigned `std::max` arguments in the ASI scheduler.
- Restored runtime catalog prefetch counters and recorded identity rejections / budget deferrals.
- Restored ASI cache-state IMG extent and catalog inventory counters and populated
  catalog inventory values from helper status replies.
- Corrected stale read-only catalog wording and literal `\\n` telemetry line endings.
- Avoided duplicate `WIN32_LEAN_AND_MEAN` definition in the world-list guard.
- No IPC protocol change (still v10), draw-distance code, or new speculative loading.
- Portable Release build and 42/42 CTest checks pass; Windows build/gameplay must
  still be confirmed on an MSVC-equipped Windows machine.

## World Residency V6 — 2026-10-19

- Protocol v10: opt-in `OpenWarmImgMember` RPC for the exact Mod Loader IMG
  handle and observed read, plus bounded catalog hints.
- Verified GTA SA `VER2` directory parsing with strict member identity,
  range, ambiguity, and source-file bounds checks.
- Warming restricted to 1 MiB source pages that intersect a verified
  DFF/TXD member; short-lived helper handles and existing RAM quotas preserved.
- Eight-source, identity-keyed directory metadata cache and worker counters.
- New portable extent/parsing regression tests (42 passing total).
- Windows ASI/helper compilation and game validation still pending.

## World Residency V5 — 2026-10-19

- Protocol v9: helper catalog cell queries carry bounded model-to-TXD hints.
- Opt-in dependency-aware observed loose DFF/TXD source warming;
  rejects guessed paths and unverified IMG membership.
- Background-worker selected-handle identity revalidation, diagnostic counters,
  new portable matching tests; no Project2DFX draw-distance features.
- Still pending: authoritative unobserved Mod Loader resolution, IMG member
  source identity, complete COL graph and Windows gameplay testing.

## World Residency V4 (experimental, protocol v8)
- Added versioned disk snapshots of the x64 read-only world catalog, with source-fingerprint invalidation, bounds checks and corruption fallback.
- Cache resides under LOCALAPPDATA per GTA installation, not in GTA's 32-bit address space or installation directory.
- Expanded protocol diagnostics with `fromDiskCache`; ASI and helper must be rebuilt together.
- Added portable cache/rebuild regression tests. Windows x86/x64 gameplay build remains unverified.

## World Residency V3 (experimental, protocol v7)
- Added opt-in, quota-limited x64 background warming of live, validated Mod Loader-selected file HANDLEs; no filesystem override guessing.
- Added bounded read-only IMG v2 DFF/TXD/COL candidate indexing, IDE model/TXD edges, ambiguity diagnostics and versioned IPC counts.
- Added portable warm-admission tests and synthetic IMG asset-source tests. The x86/x64 Windows build and gameplay behavior remain unverified.
- No Project2DFX render/draw-distance ownership was added.

## World Residency V2 — experimental source update

- Catalog: safe GTA SA bnry IPL decoding, including IMG VER2 streaming IPL blocks;
  exact IDE/IPL ID matches and unambiguous names required for model hints.
- Worker IPC protocol v6: bounded name + ID hint lists and source coverage counters.
- Native bridge: opt-in, live model-name/ID-validated, nonblocking unpinned GTA
  streaming requests with VA/backlog/frame/GPU/cooldown safety checks.
- Project2DFX draw distance/LOD/visibility ownership unchanged.
- Portable synthetic IMG/binary IPL and admission tests added.
- Remaining: effective Mod Loader source mapping, source-aware full-world cache
  warmup, persistent on-disk catalog and Windows in-game validation.

## 2026-10-19 — WorldResidency A+C pilot

- Added read-only x64 world placement catalog sourced from registered text IDE/IPL.
- Added asynchronous catalog building, fixed-size protocol v5 queries and
  game-thread-safe position hints via existing cache worker.
- Added opt-in `[WorldResidency]` configuration and runtime coverage diagnostics.
- Explicitly leave catalog-native prefetch disabled until validated name/OLA
  mapping; no renderer or Project2DFX duplication.
- Added portable parser fixture tests and protocol v5 assertion.
- **Both ASI and helper must be rebuilt and deployed together.**

## Draw-distance code removal (Project2DFX separation)

- Removed DistantStreaming's optional draw-distance renderer and model-distance
  metadata controller, including camera far-clip/fog overrides, sector/LOD scan
  extensions, and renderer traversal hooks.
- Removed related INI settings, profile-only 5x visual experiments, policy
  headers, CMake targets, and dedicated tests.
- Retained native streaming/predictive scheduling, memory management, x64 source-byte
  cache and read-only D3D9 draw-call/frame-pacing diagnostics.
- Project2DFX (or the game) owns draw distance, LOD rendering and visual effects.
- Entries below describe earlier historical revisions; draw-distance features
  described there are no longer present.

## 2026-10-18 — Main-thread stutter stability and bounded camera catch-up

- Add an opt-out frame-stability gate for the optional predictive controller.
- Default to nonblocking camera requests, including with legacy user INIs
  requesting synchronous camera flushes; enable the unsafe override explicitly
  only when investigating loader performance.
- Bound supplemental camera spatial traversal by cells, nodes and elapsed time.
- Cap predictive controller sectors and candidate discovery, with adaptive
  backoff after an expensive tick. Native streaming and extended ground/LOD
  renderer scans are untouched.
- Disable speculative whole-world pinning by default while stability mode is
  active, retaining an explicit opt-in.
- Add per-second controller pacing diagnostics and a 5x-compatible stable INI.
- 41 portable Debug tests passed; Windows game runtime remains unverified.

## RTX 4070 SUPER D3D9 managed-resource priority and 12 GB budget

- D3D9 SetTexture/SetStreamSource/SetIndices vtable observers (slots 65/100/104)
  chain original successful API calls, then sample 1/128 resource bindings.
  Tag-owned managed assets can be boosted once; no new GPU resources or pool
  changes, and no extra COM references retained beyond the query.
- GPU pressure threshold now derived explicitly from the smaller of the
  Windows WDDM LOCAL process budget and the configured target, less headroom.
- Tuned supplied GPU settings for RTX 4070 SUPER (12 GB):
  `TargetMiB=10240`, `BudgetHeadroomMiB=1536`; previous 24-GB values removed.
- New portable policy tests; existing draw-distance, x64 helper, Mod Loader,
  and native reclamation paths untouched.
- GPU promotion is **not** a mechanism to discard GTA D3DPOOL_MANAGED CPU
  backing stores: those are needed for D3D9 device reset and Lock APIs.

## Draw-distance multiplier clipping/reclassification fix

- Keep the default safe 3000-unit ceiling; add an **opt-in** GTA SA 5x
  profile (`config/DistantStreaming_GLOBAL5_EXPERIMENT.ini`) with 6000-unit
  model cap and synchronized camera/sector/LOD scan ceilings.
- Explicit model-distance clipping diagnostics, actual per-category
  evaluation/write counters and complete parsed multiplier reporting.
- Reclassify models immediately (bounded to ModelsPerFrame per tick) when
  delayed sector scans reveal buildings, big buildings or LOD parent types.
- Add regression tests for the user's exact multiplier configuration.

2026-10-18: Added bounded helper-aware native CStreaming::RemoveModel reclamation for stale unreferenced models, with hysteresis, cache-health gate, extended quiet period, per-frame rate limiting and logged native bytes freed. Raw source cache remains separate from GTA RenderWare memory.



### 2026-10-18 — Mod Loader loose-asset x64 cache read-through

The `std.stream` implementation in the official Mod Loader source chooses
actual IMG handles and loose DFF/TXD/COL handles for replacement assets and performs
`ReadFile(handle, buffer, size, ..., OVERLAPPED*)` inside its own streaming
worker. Those replacements do **not** have an authoritative handle in the
vanilla GTA IMG array. The previous cache hook therefore saw `cdEligible=0`.

This release adds a module-local ReadFile IAT cache bridge for
`std.stream.dll`, installed with compare-and-exchange on the background worker.
It accepts either an aligned `.img` sector read at an actual OS offset,
or a *complete* loose `.dff`, `.txd` or `.col` read under a `modloader` folder, verified with a real OS HANDLE plus file index, size,
last-write time and offset. It never resolves a loose replacement by scanning
IMG archives or inventing a model pointer. Native `ReadFile` remains the
fallback for every miss, partial read, foreign provider or unsupported module.

The helper receives demand jobs via the existing worker-only named-pipe
protocol. Its 1 MiB uncompressed pages stay in the x64 process, with a
bounded 32-bit READY-copy L1 (48 MiB by default). A READY L1 hit can satisfy
Mod Loader's immediate-success branch; the original worker remains responsible
for synchronization, model lifetime, and GTA loading. No new 8 GiB game
allocation or aggressive unloading is performed. The default helper *ceiling*
is 8192 MiB, actual resident memory follows useful requests. Whole-archive
warming stays disabled and compressed cold storage is deferred until hit-rate
and decompression performance justify it.

The x64 cache is a **byte/page cache**, not a model residency or GPU
texture manager. GTA and Mod Loader own the lifetime and parsing of
geometry, textures, collisions and RenderWare allocations. The 8 GiB value
is a maximum cache budget, not immediate allocation. Hot uncompressed
pages minimize latency and avoid decompression work; cold-page compression
should be evaluated separately after confirming useful hit ratios.

New diagnostic keys: `modLoaderIatState` (0 untried, 1 plugin missing,
2 installed, 3 no compatible import, 4 foreign/modified provider),
`modLoaderCalls`, `modLoaderImgReads`, `modLoaderLooseReads`,
`modLoaderImgHits`, `modLoaderLooseHits`, `modLoaderEligible`, `modLoaderHits`,
`modLoaderMisses`, `modLoaderQueued`, `modLoaderPending`,
`modLoaderPassThrough`, `modLoaderServe`. A newly connected helper may
remain tiny until real Mod Loader replacement files are streamed; baseline
game IMG reads continue through native GTA when original slots are absent.

Safety/testing: Native GTA CdStream and RenderWare behavior unchanged.
Windows GTA+Mod Loader integration MUST be verified in game before declaring
performance or streaming correctness. For isolation, set
`ServeModLoaderReadyHits=0` to keep observation/prefetch but never substitute
Mod Loader bytes, or `InterceptModLoaderLooseReads=0` to disable the bridge.
Reference: https://github.com/thelink2012/modloader/blob/master/src/plugins/gta3/std.stream/backend.cpp
## Drawing-stage and sampled D3D9 draw-cost profiling

- Instrumented DistantStreaming's `drawingEvent` into eight named CPU wall-time
  stages: `gpuFrame`, `memoryReassert`, `telemetryFrame`, `controllerTick`,
  `distanceTick`, `cameraApply`, `stateCapture`, and `cleanup`. The stages
  preserve operation order and existing early exits; they do not change
  visibility decisions or LOD/model request budgets.
- Added passive, bounded profiling for device D3D9 vtable slots 81-84:
  `DrawPrimitive`, `DrawIndexedPrimitive`, `DrawPrimitiveUP`, and
  `DrawIndexedPrimitiveUP`. Each method always calls its original COM slot
  with unchanged arguments; only every 128th call takes performance-counter
  timestamps. Unsampled calls use one relaxed atomic increment.
- Report `Frame pacing drawing stages` and `Frame pacing draw calls` alongside
  the existing `Present`/`EndScene` report. Slow sampled draw calls (>=1, >=5,
  >=15 ms) are counted separately. Timings measure CPU duration *inside the
  API*, not asynchronous GPU execution; other devices and presentation
  backends may escape observation.
- New `[GPUResidency] DrawCallTelemetry=1` setting (optional, default enabled).
  Set `0` to eliminate the additional four draw-method vtable observers.
  Existing `FramePacingTelemetry` continues to control callback-stage timing.
- Added portable regression tests for stage-window reset, sampling stride,
  per-kind counting, slow-call buckets and tick-to-microsecond conversion.
  This diagnostic-only change intentionally does not modify streaming,
  AssetCache64 safety, camera range, or the world/LOD scanner.

## Frame pacing / D3D9 CPU timeline diagnostic pass

- Added a passive, configurable `FramePacingTelemetry=1` profiler to the existing
  D3D9 device vtable clone (COM slots 17 `Present`, 42 `EndScene`).
  Original COM pointers are called unchanged; no executable patch, sleep,
  VSync override, forced FPS cap, or foreign renderer-hook modification.
- Reports `Frame pacing D3D9` every GPU residency interval, including Present
  interval distribution, Present call duration, outside-Present duration,
  EndScene duration and EndScene-to-Present gap. Device `Reset` invalidates
  prior timing boundaries; errors, missing observations and clock regression
  are explicitly reported.
- Reports `Frame pacing callbacks` for DistantStreaming drawingEvent and
  gameProcessEvent callback execution, **not** total GTA simulation time.
- CPU timings cannot measure GPU rendering latency. `PresentEx` and
  additional swap-chain Present are not intercepted; zero Present observations
  indicate incomplete coverage, not an idle renderer.
- Added portable clock-sequence, reset, histogram and snapshot tests.
- Retained previous draw distance/LOD behavior, native asset requests,
  model budgets and Cache64 native fallback.

## 2026-10-18 — Renderer interoperability and speculative preload pressure

- Do not overwrite foreign renderer detours (including indirect, PUSH/RET,
  MOV/JMP x86 entries); log executable target region and its module
  when resolvable. Explicitly report distance-correction fallback and
  unavailable render-pass probe separately.
- Ship a lower-pressure INI profile (`AssetPreload.Enabled=0`,
  `PreloadAllAssets=0`) while preserving the extended world/LOD scanner.
- Start releasing optional ASC-owned preload pins earlier if a custom
  profile enables speculative preloading; add release/ownership counters.
- 35 portable Debug tests pass; native Windows and GTA runtime unverified.

## 2026-10-18 — Streaming request feedback and flow profiling

- Mark optional renderer-issued RequestModel calls as plugin-originated to avoid treating
  them as vanilla demand in the request observation/predictive controller.
- Distinguish native and plugin RequestModel calls in the existing safe Request hook.
- Add bounded rolling model-state sweep, request backlog trends and per-stage scheduler
  CPU timings without installing the legacy native load trampoline.
- Do not change extended world/LOD visibility, GTA native loading or Cache64 completion.
- Add one portable StateSweep/BacklogMeter regression suite.

## 2026-10-18 — Extended sector traversal CPU optimization

- Deduplicate modulo-mapped repeat-sector object lists per renderer frame.
- Reuse only committed-readable `VirtualQuery` regions within the current
  renderer frame to reduce redundant pointer safety queries.
- Add traversal and validation-cache counters to extended-scan diagnostics.
- Preserve existing world/LOD coverage, draw distances, and Cache64 native
  fallback; no hard time budget or persistent entity cache.
- Portable Debug CTest: 32/32 passed; Win32/in-game performance unverified.
- Details: `docs/NORMAL_SECTOR_TRAVERSAL_PERF_2026-10-18.md`.

## 2026-10-18 — Frame-time instrumentation and backlog-safe requests

- Preserve visually verified normal-world and LOD discovery/submission.
- Throttle only new supplemental model requests as native streaming backlog rises;
  always admit at least one when the configured budget is nonzero.
- Add per-stage microsecond timing for normal scan and LOD plan/traversal.
- Accelerate RenderOneNonRoad statistics with an exact-verified pointer prefilter.
- Diagnose native-success/null-handle Cache64 mismatches without unsafe handle reuse.
- Add portable policy/probe tests. Windows ASI and in-game behavior unverified.
- Details: `docs/PERFORMANCE_BACKLOG_PASS_2026-10-18.md`.

## Experimental follow-up: fair LOD coverage and Cache64 diagnostics
- Rotate extended LOD tile traversal between frames to prevent nearest-first starvation.
- Report `LOD next cell cursor` to confirm scan fairness in repeated views.
- Split Cache64 missing IMG-handle skips into null versus invalid values and
  report populated native handle slots and the last missing archive file ID.
- Leave native streaming, Cache64 hit substitution, and archive warming unchanged.
- Linux portable CTest: 29/29 passing; Windows ASI/runtime unverified.

## 2026-10-18 — Ground-level LOD discovery and Cache64 pressure correction

- Enable far LOD grid in the shipped INI and fix the empty-plan lower-bound bug.
- Run the big-building supplement before the native LOD pass, after high-detail children.
- Make Cache64 speculative archive warming opt-in to avoid consuming >12 GiB of
  physical memory when `cdEligible=0` and no read-through hits occur.
- Report per-gate CdStream skip causes and model-request/LOD-plan diagnostics.
- Repair two outdated portable test expectations and verify the portable suite.

## v20 — Bounded far LOD scanning and render-pass diagnostics

- Added spatial planner for GTA's 30×30 native 200-unit LOD list grid; optional camera-facing near-first LOD supplement up to 2500 world units.
- Kept main high-detail sector scan separate; both paths avoid pedestrians and vehicles and do not cache entity pointers across frames.
- Added per-model streaming cooldown and admission checks, native render-list headroom checks, and counters for LOD submission, RW instancing, and RenderOneNonRoad pass entry.
- Added opt-in `[DrawDistance]` LOD controls and a separate visual-test INI; defaults remain opt-in because native renderer hooks are unverified in-game.
- Added portable spatial planner tests.

# Distant Streaming v19 — Experimental normal-world distance extension (source candidate)

- Added **opt-in**, guarded HOODLUM `CRenderer::ScanWorld` (0x554FE0) and
  `SetupMapEntityVisibility` (0x553F60) hooks to address GTA's native
  300-unit normal-model discovery and visibility-distance compensation.
- Added bounded static building/dummy/object sector scanning, model requests,
  RenderWare list headroom checks and process free-VA safety cutoff.
- Added `ExtendedWorldScan`, `WorldScanRangeUnits`, per-frame limits and
  minimum free VA settings to `[DrawDistance]`. **Disabled by default**.
- Added detailed telemetry to distinguish hook conflicts, scan suppression,
  real model requests and visible objects.
- Added portable policy test assertions and in-game verification guide
  `docs/DRAW_DISTANCE_V19_EXPERIMENTAL.md`.
- **Unverified:** No Windows MSVC build or GTA runtime execution was
  possible here; always test on a backed-up installation.

# Distant Streaming v18 — camera-aware draw distances

- Added camera/timecycle far clip extension for map draw distance, optional fog start extension, runtime diagnostics and policy tests.
- Preserved original vehicle and pedestrian model metadata; shared frustum limitation documented.
- No new renderer detours. See `docs/DRAW_DISTANCE_V18_CAMERA_FIX.md`.

## Distant Streaming — v17 branding migration (2026-10-17)

- Renamed Windows ASI target/output, config file, runtime log and CSV,
  companion x64 cache helper, and IPC pipe/mapping prefix.
- Updated Windows build bootstrap and source/tests for renamed artifacts.
- Kept legacy `ASC_*` build options and exported `ASC_IsNativeResource` ABI.
- Added legacy INI key alias and dual-name SanFrameOptimizer coexistence checks.
- This is a name-only release. Streaming/hook behavior is not changed.
- A Windows MSVC build and an in-game HOODLUM smoke test are still needed.

## v17 — Category draw-distance controls (2026-10-17)

- Added `DrawDistanceWin.{h,cpp}` using `CBaseModelInfo::m_fDrawDistance`
  with safe OLA table validation, incremental world-sector classification,
  runtime model replacement handling, collision-free integration and shutdown restoration.
- New `[DrawDistance]` multipliers: buildings, objects, roads, time models,
  LODs, big buildings, vegetation and clumps; per-model ID and name overrides,
  explicit vegetation selectors, documented exclusions of vehicles/peds/weapons.
- Added portable policy tests, updated Windows config parsing tests,
  `docs/DRAW_DISTANCE_V17.md`, and CMake build registration.
- No changes to RenderWare renderer, camera far clip, memory budgets or
  graphics hook installation. Procedural plant, water and 2DFX systems remain
  explicitly outside this model-metadata patch.

## v16 — HOODLUM world-source cache completion (2026-10-17)

See [docs/WORLD_CACHE_V16_COMPLETION.md](docs/WORLD_CACHE_V16_COMPLETION.md).

- Resumable bounded archive warm-up and periodic capacity-gated repair.
- Exact source-length refresh, warm registration cleanup, resource-trim ordering.
- Reliable candidate residency telemetry and portable warm-policy tests.
- 24 GiB GPU soft-target profile, Windows verification script, offline inventory.
- Preserves GTA's original x86 renderer and native streaming fallback.

# Changelog

## Unreleased — 2026-10-15 (source-only, game test pending)

- Add first-observation and change-only diagnostics for live GTA streaming
  capacity (`ms_memoryAvailable`), logging exact bytes and source address.
  Label the OSD value as `[engine]`, remove obsolete vanilla-limit constant,
  and extend regression tests to reject any inferred/doubled capacity.

- Remove Improved Streaming INI-based `DoubleStreamingMemoryLimit` scaling
  from the GTA streaming OSD. Always sample the live GTA SA CStreaming
  available/used counters, including changes by limit adjusters at runtime.
- Add isolated streaming-memory accounting regression test and document the
  distinction between GTA's memory budget and SanFrame's timing budget.
- Correct stale renderer-detection test references to native Vulkan configuration fields removed in 1.4.1. Validate legacy INI ignored and optimization master gate instead; resolves six MSVC C2039 errors in the 2026-10-15 build log.
- Hook ReFXDXVK.asi!Direct3DCreate9 when the ReFX GTA IAT factory bridge
  is active, rather than mistakenly hooking system/private DXVK factories.
- Pass ReFX factory provenance to OSD detection; verify the loaded DXVK
  backend before declaring the game device `ReFX DXVK / Vulkan`.
- Remove eager loading of the ReFX private DXVK DLL from SanFrame's hook setup.
- Reorganize all 140 existing INI keys, with ReFX-aware, quieter defaults.
- Add integration diagnosis and in-game validation checklist.
- No Windows x86 build or GTA gameplay test completed for this source patch.

## 1.4.2 — 2026-10-13

- Bind the chosen OSD font and scaled pixel size explicitly for text, shadow
  and outline layers.
- Log the loaded font-resource module, byte count and actual submitted font;
  track `Overlay.EmbeddedFont` separately from general overlay rendering.
- The installed game ASI was an older build without font resource 101; replace
  it with the font-embedded release to restore BankGothic in game.
- Compiled the Release ASI without running tests or launching GTA.

## 1.4.1 — 2026-10-13

- Removed native Vulkan policy/statistics optimization and its unused API and
  configuration fields. Kept versioned backend detection and OSD callbacks.
- Removed native callback pacing and engine scheduling; ReFX owns presentation.
- Added periodic cumulative diagnostic coverage, including configuration-off,
  auto-disabled, adaptive, unexercised, fallback and failure evidence.
- Added Present outcomes, FX lifetime counters, alpha/shadow counters, pacing,
  worker-job and actual scratch-allocation activity. Final snapshots precede
  counter teardown, and repeated registration retains evidence.
- Removed the unsupported claim that execution proves full effectiveness/FPS.
- Compiled the Release ASI only; no tests or GTA session run for this change.

## 1.4.0 — 2026-10-13

- Implemented five separately configurable engine experiments: system-timed
  friction/wind reuse, ordered FX operator plans, indexed alpha insertion,
  indexed stock shadow grouping, and resource-aware streaming cost prediction.
- Kept experiments disabled by default after measuring mixed CPU results;
  documented numerical, mutation and live-gameplay limits.
- Preserved stock alpha pool/tie order, shadow casting/water/drawing, custom FX
  traversal, game-thread uploads, mission urgency and streaming progress.
- Added signature/conflict guards, owner-thread fallbacks, project invalidation,
  hook-overwrite reporting for FX operators and execution counters.
- Avoided padding-dependent operator keys and per-particle plan-array copies.
- Separated friction/wind cache domains to prevent adjacent FX objects from
  repeatedly evicting each other; matched native shadow loop exit state.
- Expanded executable differential tests and added randomized list/group/cost
  tests and optimizer-master-switch coverage. Embedded-font tests still pass.

## 1.3.1 — 2026-10-13

- Restored local BankGothic embedding after the 1.3.0 build disabled it.
- Resolve font resources by code address instead of the ASI filename.
- Honor the configured font size for both embedded and scalable fallback fonts,
  including explicit ImGui 1.92 font-size selection.
- Borrow pinned font resource data rather than retaining another 44 KB copy.
- Added resource/glyph/lifetime tests against a renamed ASI.
- Clear FX render resources on project unload even in instancing-only mode.

## 1.3.0 — 2026-10-13

- Integrated FXP particle simulation, compiled interpolation, matrix caching,
  triangle instancing and actual-execution diagnostics into one ASI and INI.
- Embedded build-time FX shaders; removed the runtime shader compiler dependency.
- Shared SanFrame submission/state boundaries and reset handling with FXP.
- Added conservative fixed-function, threading and native-backend fallbacks.
- Reused FX state-block objects; removed legacy particle LOD storage/scheduler,
  standalone loader, duplicate logs and unused SDK implementation objects.
- Added compiled-curve reciprocal widths/front-cache lookup and avoided zeroing
  particle packet lanes that are immediately overwritten.
- Fixed stale curve evaluation after failed compilation, null output handling,
  and cached-curve rebuilding at the cache capacity limit.
- Reduced repeated streaming-table validation and disabled-report profiling.
- Added Win32 integration, configuration and native-routine differential tests.
- Prepared Git ignores, CI, dependency pins, license notices and source/release
  packaging. Previous scratch notes/ZIPs were retained in `.local-archive/`.

In-game FPS improvement and whole-mod-stack compatibility remain unmeasured.

## 2026-10-18: bounded RequestModel call-site profiler

- Sample 1/128 main-thread GTA-originated requests: caller address, model ID, delayed model state, detour versus original-call CPU timings.
- Add bounded model load-observation latency, heavy-hitter histograms and explicit sample-drop/approximation counters; native streaming untouched.
- See `docs/REQUEST_CALL_PROFILE_2026-10-18.md`.

### 2026-10-18 — Request resource attribution and memory diagnostics
- Corrected sampled RequestModel ID classification beyond DFF (TXD, COL, IPL, DAT, IFP, RRR, SCM, internal and unknown extensions).
- Added safe delayed state sampling for standard non-DFF resource IDs (individually memory-checked) and distinct state-unavailable tracking.
- Annotated hot caller return addresses with owning Windows module and RVA when resolvable.
- Preserved high-resolution QPC ticks in RequestModel timing and printed approximate ns averages/peaks.
- Added once-per-second available VA, cached largest hole, GTA private bytes, streaming headroom and backlog report.
- No request suppression, scan-range change, cache substitution, or native Load detour changes.
