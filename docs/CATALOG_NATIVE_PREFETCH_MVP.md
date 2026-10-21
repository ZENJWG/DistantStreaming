# Catalog-driven predictive native prefetch (experimental MVP)

## Goal

Reduce late model loads during fast driving and flying, without blocking loads,
memory pinning, or extra rendering hooks. This feature does **not** change
draw distance or force objects visible; it only tries to make already-selected
models ready sooner.

## Data path

1. The existing WorldCatalog builds an untrusted IDE/IPL inventory grouped into
   256-unit cells. It cannot establish Mod Loader archive override precedence.
2. The x64 helper asynchronously queries the predicted cell and publishes
   up to 64 model ID/name hints in its nonblocking mailbox.
3. On GTA's game thread, DistantStreaming predicts player XY from recent motion.
   Prediction is bounded to 320 units over 1800ms; teleportation resets velocity.
4. Every hint must match the currently desired cell, contain a short terminated
   ASCII model name, fall within vanilla model index bounds, resolve with GTA's
   live `CModelInfo::GetModelInfo(name, &id)`, and match the pointer actually
   indexed by the current OLA-bound table.
5. The game's native `GameAdapter::CanRequest` then checks load state and flags.
   `GameAdapter::RequestDefault` issues ordinary, non-pinned, non-priority
   `CStreaming::RequestModel(..., DEFAULT)` requests on the main thread.
6. The scheduler caps extra requests to **two per game frame/tick**, shares the
   controller's request allowance, and has its own eight-outstanding cap.
   It stops on native backlog, low physical/virtual/streaming memory, bad frame
   time, high GPU pressure, active priority loads, interiors, and paused scenes.
   Short cooldowns prevent repeated speculative reloads.

No extra renderer/EndScene hooks or game-thread IPC were added.

## Settings

The supplied main profile (`config/DistantStreaming.ini`) enables this feature:

```ini
[WorldResidency]
Enabled=1
PrefetchModels=1
PrefetchMaxRequestsPerTick=2
PrefetchMaxOutstanding=8
PrefetchNativeBacklogLimit=16
PrefetchMaxChecksPerTick=16
PrefetchMaxMemoryPercent=82
PrefetchCooldownMs=8000
PrefetchLookAheadMs=1800
PrefetchMaxPredictionUnits=320
```

Other profiles with no `PrefetchModels` key remain OFF by default.

Dependencies: `General.Enabled=1`, `Diagnostics.HookMode=1`,
`Diagnostics.ControllerEnabled=1`, `Prefetch.Enabled=1`,
`ExternalAssetCache64.Enabled=1`, WorldResidency enabled, a matching running
`DistantStreaming_AssetCache64.exe`, and a supported game build.
The core `[Prefetch] MaxRequestsPerTick` is a shared total cap.

**Rollback:** set `PrefetchModels=0` and restart. This disables the catalog
request path while preserving the original controller/cache and compatibility
with SanFrameOptimizer. No rebuild is needed for rollback.

## Build / install (Windows)

Use Visual Studio C++ tools, supported Windows SDK, and CMake; in PowerShell:

```powershell
.\build.bat
# To force a matching x64 helper rebuild:
.\build.bat cache64
```

Copy the built `dist/DistantStreaming.asi`, its matching
`dist/DistantStreaming_AssetCache64.exe`, and the updated INI into the game
folder (or your established ASI loader directory). Back up originals and remove
duplicate old ASIs. This change does not modify the helper wire protocol.

## A/B testing

Repeat the same high-speed driving or flying route, with identical time of
day, Project2DFX distance settings, vehicle, weather and camera configuration.
Compare `PrefetchModels=0` and `1`. Measure visible near/far object pop-in,
1% low FPS, frame spikes, native streaming backlog, process memory pressure
and `DistantStreaming.log`.

Logs: `Catalog prefetch: requests=...` confirms actual new GTA-native requests;
`pending`, `completed` track observed request state; `nameReject`,
`identityReject`, `stateSkip`, `duplicate`, `staleCellTicks`,
`pressureTicks` show why hints were not admitted. Status is reported at least
every 10 seconds after operation starts.

The portable test suite covers quota, per-frame cap, stale-cell rejection,
malformed/wrong-ID hints, cooldown, and teleport handling. **Windows ASI
compilation and live GTA/SanFrame gameplay are not tested in this environment.**
The helper catalog is incomplete for some asset types and a cell may contain
interior placements even while the player is outdoors. Keep the admission
limits conservative, and disable the feature if rendering or memory worsens.
