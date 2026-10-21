# DistantStreaming V6.2 — INI-controlled fast model loading

## Goal

Make native GTA model requests from the verified 64-bit world catalog as fast
as the existing game can safely handle, without bypassing its x86 virtual-address
limit or implementing Project2DFX visibility/draw-distance/LOD functionality.

This changes **request admission**, not render completion. It does **not**
spawn all IPL world entities instantly. A native model request can take multiple
frames to load/instantiate. The x64 helper retains source bytes, not RenderWare
objects.

## What changed

- `[WorldResidency] SpeedPreset=0..3` controls fallback budgets:
  - `0` manual/balanced defaults
  - `1` balanced: 4 requests/tick, 16 checks/tick, 48 requests/second,
    400us/tick, 500ms x64 query interval
  - `2` fast: 10 requests/tick, 32 checks/tick, 120 requests/second,
    750us/tick, 250ms x64 query interval
  - `3` very fast: 20 requests/tick, 64 checks/tick, 240 requests/second,
    1200us/tick, 100ms x64 query interval
- Explicit INI overrides: `MaxRequestsPerTick`, `MaxChecksPerTick`,
  `WorkBudgetUs`, `MaxRequestsPerSecond`, `CatalogQueryIntervalMs`,
  `RescanIntervalMs`, `MaxNativeBacklog`, `MaxFrameMs`, `CooldownMs`,
  and `MinFreeVaMiB`.
- Reconsider resident cell hints periodically (`RescanIntervalMs`) instead
  of only on cell changes. A 0-initialized cooldown no longer delays the
  first request after a fresh startup.
- Checks native GTA backlog before *each* speculative request.
- Per-tick CPU and per-second request caps prevent bursts; live
  model-name/id verification, game safe state, OLA binding,
  VA/memory limits, GPU pressure and frame guard all remain mandatory.
- Speed profiling counters in periodic runtime log.

## Very fast profile supplied in `config/DistantStreaming.ini`

```ini
[WorldResidency]
Enabled=1
PrefetchModels=1
SpeedPreset=3
MinFreeVaMiB=1024
```

Other values inherit from preset 3 unless uncommented in the INI. Set
`SpeedPreset=2` if the game stutters, or set `PrefetchModels=0` to turn off
catalog-native prefetch without disabling the catalog itself.

**There is no genuine "instant load every model" switch.** The 32-bit game
process can hold only a bounded active working set.

## Actual world-entity instancing (separate opt-in)

`[WorldResidency]` never calls `CEntity::CreateRwObject()`. The legacy
`[AssetPreload]` subsystem optionally does so **only for existing entities in
GTA's currently loaded world sector/LOD lists**. It does not create missing
IPL placements or alter Project2DFX's renderer.

To try this separately, take a backup and change:

```ini
[FrameStability]
AllowSpeculativeAssetPreload=1

[AssetPreload]
Enabled=1
PreloadAllAssets=0
ForceLoadModels=1
ForceWorldInstances=1
ModelsPerTick=2
MaxMaterializedObjects=128
MaxInstancesPerTick=4
InstanceBudgetUs=750
InstanceMinimumFreeVaMiB=1536
```

Start conservative. Raising `MaxMaterializedObjects` increases native
RenderWare allocations and can worsen frame time. Do not enable
`PreloadAllAssets=1` at first; broad pinning can exhaust x86 address space.
The `[WorldResidency]` speed preset does not automatically enable these
unsafe native-entity operations.

## Validation

- Portable build + all 42 CTest tests passed, including preset ordering,
  rate limits, wrap-safe sampling and high streaming-memory-pressure gating.
- Windows Win32 MSVC compilation, the x64 helper runtime and in-game
  performance **have not** been tested here. Rebuild both targets and
  compare startup, teleportation, driving, FPS P95/P99, backlog, streaming
  memory, free VA, and native-load times with preset 1 vs preset 3.
- Keep Project2DFX responsible for visibility, draw distances and LODs.
