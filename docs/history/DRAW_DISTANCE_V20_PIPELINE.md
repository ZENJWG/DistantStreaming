# Distant Streaming v20 — Extended map-world/LOD pipeline

## Purpose and scope

v19 logs showed `Extended world scan ACTIVE` and many cumulative
`added to visible` entries, but **no observed increase in visible distance**.
Those counters proved that the supplemental scanner ran, not that entities
entered the final rendering passes.

v20 adds a *bounded spatial LOD path* and observability while preserving the
existing normal-world renderer extension, original RenderWare/D3D9,
HOODLUM compatibility guards and ped/vehicle exclusions.

## Native engine call chain

Verified from GTA SA reversed and Plugin-SDK:

- `CRenderer::ConstructRenderList` (0x5556E0) resets visible lists, calls
  `ScanWorld` then `ProcessLodRenderLists`.
- `CRenderer::ScanWorld` (0x554FE0) covers regular and LOD sector scans,
  with vanilla normal world distance limits.
- `CRenderer::SetupEntityVisibility` (0x554230) dispatches normal models.
- `CRenderer::SetupMapEntityVisibility` (0x553F60) uses model metadata,
  current far plane, current model residency, entity frustum and occlusion.
- `CRenderer::SetupBigBuildingVisibility` (0x554650) handles big-building
  LOD parent/child decisions; use this for the supplementary LOD scan.
- `CRenderer::AddEntityToRenderList` (0x5534B0) places visible entities
  into the fixed GTA lists or alpha-sorted list.
- `CRenderer::RenderOneNonRoad` (0x553260) is called from GTA render passes
  for normal non-road geometry. A call here is a **render pass entry**,
  *not proof of a GPU draw call*. Roads may be drawn directly by
  `CRenderer::RenderRoads` and are not covered by this probe.

Engine documentation:
- https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/Renderer.cpp
- https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_sa/game_sa/CWorld.h

## Implementation

The v20 LOD grid is the original native **30×30** world partition, with
200-world-unit tiles, computed at runtime using a bounded spatial planner.
It scans camera-facing, near-first tiles within `[WorldScanRangeUnits,
LodGridRangeUnits]`, using current list heads. Mod Loader and IPL lifecycle
remain native to GTA: no `CEntity*` or iterator is saved across frames.

The near/high-detail scan remains limited to `WorldScanRangeUnits`, and the
separate far LOD pass reaches at most `LodGridRangeUnits`. Both still require
a shared RW far camera range. This design **does not deserialize unloaded
binary IPL placements**, invent placements, alter cars/peds, or guarantee
the entire 6km game map fits in a 32-bit game process.

Admission control:
- Cap visited LOD tiles, visited LOD nodes, extra visible entries, and
  extra requests each frame.
- Check native render-list headroom before calling native visibility.
- Skip entities already stamped with GTA's current world scan code.
- Only request when GTA's own streaming state is `NOT_LOADED`, with per-model
  request cooldown; never preempt native streaming ownership.
- Suppress scanning when free x86 VA falls below ExtraScanMinFreeVaMiB.
- Keep renderer detours opt-in and verify executable target pages.

The test profile leaves aggressive all-model preloading disabled so its
memory use doesn't swamp the visual experiment.

## New INI properties

In `[DrawDistance]`:

| Property | Meaning |
|---|---|
| `ExtendedWorldScan=1` | Opt in to the native ScanWorld/visibility hooks |
| `WorldScanRangeUnits=750` | Normal static sector scan range; source cap 1200 |
| `ExtendLodGrid=1` | Also scan native 30x30 big-building LOD list |
| `LodGridRangeUnits=2500` | Maximum target for far LOD cells; source cap 2500 |
| `LodCellsPerFrame=500` | Native LOD tile visits per renderer frame, source cap 900 |
| `LodEntitiesPerFrame=2048` | Maximum LOD list-node visits |
| `ExtraRequestCooldownMs=3000` | Min time between repeated speculative requests for one model |
| `TraceRenderPass=1` | Optional 0x553260 RenderOneNonRoad hook |

## How to verify

Install only `DistantStreaming.asi` (not `AdaptiveStreaming.asi`).
Use `config/DistantStreaming_v20_VISUAL_TEST.ini` renamed to
`DistantStreaming.ini` alongside the ASI.

Compare the same stationary skyline in two complete game restarts:
1. `[DrawDistance] Enabled=0` (baseline).
2. `[DrawDistance] Enabled=1`, `ExtendedWorldScan=1`, `ExtendLodGrid=1`.

Check `DistantStreaming.log` for
`Extended world scan ACTIVE v20` and the metrics:

- `LOD sectors`, `LOD entities`, `LOD submitted`: LOD world discovery
  and native list submission.
- `model requests`: cumulative extra requests; may not equal unique assets.
- `loaded-model encounters`: requested assets already loaded by GTA.
- `instantiated submitted` / `no RW object on submit`: immediate
  RenderWare entity instance presence after list submission.
- `non-road render-pass entries`: all hooked RenderOneNonRoad invocations.
- `of those from extra scan`: supplemental candidates reaching the
  non-road render routine. This excludes roads, alpha-only and other paths.
- `render hook active`: whether the diagnostic hook was installed.
- `VA-pressure skipped`: scans omitted under 32-bit address-space pressure.

**If no supplemental candidates enter the render pass**, investigate the
native visibility/LOD stage and other ASI renderer hooks. **If they do enter
the render pass but are not visible**, investigate geometry visibility,
occlusion, alpha/fog, RW instances and actual D3D9 submissions.

## Validation and risks

Portable CMake/CTest tests cover spatial planning, request cooldown and
pre-existing policy guards. They cannot compile the Win32 Plugin-SDK unit
or test the exact HOODLUM hook ABI on this Linux host.

No guarantee is given of successful Windows build or visual improvements
until the actual x86 MSVC compilation and in-game tests pass. Do not ship
as a default-on production renderer detour before that validation.

At 2500 units, the game can still have a constrained fixed render list,
even when source bytes live in the external 64-bit cache. Source caching
does not expand GTA's 32-bit decoded RenderWare address space.


### Safety of the VISUAL_TEST profile

This profile opts into native hooks and sets `ExtraScanMinFreeVaMiB=512`
so the experimental scan can run even in an installation previously observed
to have only ~770 MiB of free VA. **This is riskier than the default 1024
MiB threshold.** Back up the installation, do not use it for long sessions,
and switch the feature off if streaming errors, invisible textures, or
crashes appear. For regular play, restore `ExtraScanMinFreeVaMiB=1024` and
reduce ranges/extra budgets to measured stable values.
