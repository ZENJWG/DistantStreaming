# Distant Streaming v18 — camera-aware map draw-distance fix

## Why v17 could write 14,341 model distances without visible effect

`CRenderer::SetupMapEntityVisibility` uses `min(TheCamera.m_fLODDistMultiplier * modelInfo->m_fDrawDistance,
modelInfo->GetColModel()->GetBoundRadius() + CRenderer::ms_fFarClipPlane)`.
`CRenderer::ScanWorld` uses the RW camera `farPlane` for frustum-based world-sector traversal.
`CStreaming::RemoveLeastUsedModel` also considers far clip when deciding if a model may be evicted.

Merely increasing the IDE/model distance does not increase those camera/sector ceilings.
In `Idle` the game reapplies `RwCameraSetFarClipPlane(Scene.m_pRwCamera, CTimeCycle::GetFarClip())`
after constructing the render list. The controller therefore needs to update BOTH the current
timecycle far clip and the RW camera's far clip, at game process and draw event boundaries.

## Implemented changes

- `src/game/DrawDistanceWin.cpp` now has `Manager::ApplyCamera()` to extend the active
  timecycle `CColourSet::m_fFarClip`, optional `m_fFogStart`, and RenderWare's
  `RwCamera::farPlane` through `RwCameraSetFarClipPlane`.
- `src/game/main.cpp` invokes this at game process and drawing callbacks; it also
  registers passive `renderSceneEvent` diagnostics to detect other ASIs resetting
  camera range. This adds no new inline renderer detours.
- `include/asc/DrawDistancePolicy.h` implements capped, testable pure calculations.
- `[DrawDistance]` gains `ExtendCameraFarClip`, `CameraFarClipUnits`,
  `ExtendFogStart`, `FogStartFraction` settings.
- Model metadata handling still explicitly skips MODEL_INFO_PED, MODEL_INFO_VEHICLE,
  and MODEL_INFO_WEAPON even for per-ID or named overrides.

## Important ped/vehicle limitation

There is only ONE active main RenderWare camera far plane. Increasing this camera's far clip
necessarily changes the global view frustum; it **cannot guarantee perfectly identical ped/
vehicle projection/clipping**. Their model draw-distance fields remain unchanged, so the
plugin does not increase their per-model streaming/distance budgets. Complete hard exclusion
would need a separate static-world rendering pass or per-entity visibility hooks, neither of
which is included here.

## Performance and stability

A bigger far plane increases the volume of scanned world sectors and may increase visible
model count/draw calls. This does not extend GTA.exe's 32-bit address space. Your sample
log shows ~761 MiB remaining and emergency memory pressure at the end of the run.
Start at camera range 900–1100; do NOT start with GlobalMultiplier=10.
A prudent initial test is:

```ini
[DrawDistance]
Enabled=1
GlobalMultiplier=1.2
BuildingsMultiplier=1.25
LodMultiplier=1.5
BigBuildingsMultiplier=1.4
ExtendCameraFarClip=1
CameraFarClipUnits=1100
ExtendFogStart=1
FogStartFraction=0.8
```

`ExtendCameraFarClip=0` returns to model-only behavior and cannot defeat world-sector
camera culling. `ExtendFogStart=0` leaves weather fog unchanged.

## Runtime diagnostics

In `DistantStreaming.log`, look for:

- `Map draw distances: ... camera far clip=enabled`
- `Map draw distances CAMERA: requested=..., timecycle after=..., RW camera farPlane=...`
- `Map draw distances WARNING: another renderer reset RW camera farPlane=...`

If the second line never appears, the game camera or timecycle binding is unavailable,
the controller isn't reaching callbacks, or the game is in a skipped state (such as cutscene).
If mismatches grow, another mod may override the camera after our update.

## Verification status

Portable unit tests: 27/27 passed in Linux build. Windows x86 ASI compilation,
the HOODLUM executable's event timing, other graphics mods, and in-game map visibility
require local Windows testing. No claim of guaranteed zero pop-in is made.

Engine reference:
- https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/Renderer.cpp
- https://github.com/gta-reversed/gta-reversed/blob/master/source/app/app_game.cpp
- https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_sa/game_sa/CTimeCycle.h
- https://github.com/DK22Pac/plugin-sdk/blob/master/shared/Events.h
