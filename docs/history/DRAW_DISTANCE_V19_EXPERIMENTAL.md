# Distant Streaming v19 — Experimental extended static-world draw range

## Root cause confirmed from GTA SA reversed

`CRenderer::ScanWorld` (0x554FE0) always scans ordinary world sectors to
`MAX_LOD_DISTANCE` (300 GTA units), even when the camera's far plane and a
model's `m_fDrawDistance` are much larger. The separate distant-LOD scan
uses the far plane, so changing model metadata alone cannot make normal
buildings appear much farther away.

`CRenderer::SetupEntityVisibility` (0x554230) also adds
`m_fDrawDistance * TheCamera.m_fLODDistMultiplier - 300` to the distance passed
to `CRenderer::SetupMapEntityVisibility` (0x553F60) for long-distance models.
With a large multiplier, that can cancel the very increase the model received.

Sources:
- https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/Renderer.cpp
- https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_sa/game_sa/CRenderer.h

## What v19 adds

**This is an experimental Windows/HOODLUM implementation. It is not
validated in GTA or by MSVC and must be tested against a backed-up install.**

When `ExtendedWorldScan=1`, on the game's main thread and only after validating
the live OLA/model bindings, the controller attempts guarded MinHook detours on:

- `CRenderer::ScanWorld` at 0x554FE0 (original renderer runs first).
- `CRenderer::SetupMapEntityVisibility` at 0x553F60 (original renderer still
  owns visibility/LOD/fade/streaming semantics).
- It invokes `CWorldScan::ScanWorld` at 0x72CAE0 with an extended, camera-
  facing polygon; static sector building/dummy and static repeat-sector
  object lists only. It does **not** inspect vehicle/ped lists.
- It corrects the native 300-unit offset **only** when measured distance,
  model range and passed distance precisely match the original compensation
  case. Other visibility calls are forwarded untouched.
- It extends `CRenderer::ms_fFarClipPlane` during `ScanWorld` because vanilla
  snapshots this separately before calling `ScanWorld`.

The feature is opt-in because native renderer detours may conflict with other
rendering ASIs. If hooks are already detoured or cannot be verified as executable
within the GTA PE image, both hooks stay off and the plugin logs why.

## Initial test settings

Edit `DistantStreaming.ini`:

```ini
[DrawDistance]
Enabled=1
GlobalMultiplier=2.0
ExtendCameraFarClip=1
CameraFarClipUnits=1100
ExtendedWorldScan=1
WorldScanRangeUnits=550
ExtraModelsPerFrame=12
ExtraVisiblePerFrame=32
ExtraScanMinFreeVaMiB=1024
```

Test *standing still* with one unobstructed landmark ~350–500 units away.
This only scans static map models; procedurally generated grass, water, point
lights, and other specialized effects are separate systems.

For an intentional high-range test, increase `WorldScanRangeUnits` to 800
and `CameraFarClipUnits` to 1500 **after** the 550-unit test succeeds.
The 10x multiplier does NOT itself set the world-scan range. The scanner is
bounded to 900 units in this version. Going much farther efficiently would
need a redesigned renderer and larger visibility arrays.

Your last uploaded GTA logs show free virtual address space falling to
~767 MiB. With `ExtraScanMinFreeVaMiB=1024`, the added scan **stops** at
that pressure level on purpose; there is no guarantee of increased distance
at that point. Do not lower the cutoff casually just to force results;
exhausting 32-bit VA can crash the game.

## Verify with DistantStreaming.log

Look for:

- `Extended world scan ACTIVE: ...` — both native renderer detours installed.
- `Extended world scan DISABLED: ...` — hook conflict or unsupported build.
- `Extended world scan: active=1, frames=..., sectors=..., eligible entities=...,
   added to visible=..., model requests=..., 300-unit compensation corrected=...,
   VA-pressure skipped=...`

If `frames` rises but `sectors` stays zero, the native scanner or camera
frustum construction needs a Windows debug session. If `sectors` rises but
`eligible entities` is zero, inspect OLA map model entries and sector
ownership. If `VA-pressure skipped` increases, lower the distance multiplier
or free native address space rather than raising the GTA streaming budget.

## Limitations and verification

- GTA's original renderer remains x86; external 64-bit cached DFF/TXD
  bytes are not automatically instantiated RenderWare resources.
- `CStreaming::RequestModel()` queues models, it does not create an
  on-screen instance instantly. Streaming completion and instance
  materialization require subsequent game frames.
- Both visible-array and LOD scratch capacity are capped; extra map
  models can be skipped even when the source asset exists.
- Camera and 3D far-clip planes are shared with vehicles/peds. The
  per-model controls and the extended **sector scanner** exclude
  vehicles/peds, but a camera far-plane change cannot exclude them entirely.
- Other mods can own the ScanWorld/SetupMapEntityVisibility entry points;
  the safe behavior is to refuse installation rather than overwrite them.
- Portable policy tests are not proof of Windows detour ABI or in-game behavior.

## Build

Use `build.bat` in a VS 2026 Build Tools environment. Keep the x64 helper
and Win32 ASI compiled from the same source bundle. Rename or remove older
`DistantStreaming.asi`/`AdaptiveStreaming.asi` duplicates before installing.

The feature is enabled only when the INI switch is set. For safer isolation,
test with `AssetPreload=0` and `OffscreenCleanup=0` while keeping the draw
distance controller on.
