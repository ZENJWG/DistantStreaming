# GTA SA draw-distance multiplier fix — v25

This patch addresses **two concrete source issues**:

1. GTA SA's `CBaseModelInfo::m_fDrawDistance` is modified using
   `GlobalMultiplier * one classified category multiplier`, but the prior
   code silently clamped to `MaxDistanceUnits=3000`. `GlobalMultiplier=5`
   for a 850-unit object requested 4250 and was applied as only 3000.
   The default cap remains unchanged for compatibility. Use the separate
   `DistantStreaming_GLOBAL5_EXPERIMENT.ini` to opt into a 6000-unit cap,
   world scan and camera far clip.
2. The 120x120 normal sectors and 30x30 LOD grid are visited incrementally;
   an entity discovered after model metadata had already been processed
   could keep its temporary `Objects` category until another 20,000-model pass.
   Newly discovered building/big-building/LOD flags now queue an immediate,
   bounded model recalculation on the next manager tick.

## Engine reference

Plugin-SDK maps model draw distance to `CBaseModelInfo::m_fDrawDistance`
(offset 0x18). GTA reversed `CRenderer::SetupMapEntityVisibility` uses the
camera LOD distance multiplier AND clamps by renderer far clip + collision
radius. `SetupEntityVisibility` has a separate 300-unit compensation.
The existing guarded renderer hooks and extended normal/LOD scans remain intact.

Links:
- https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_sa/game_sa/CBaseModelInfo.h
- https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/Renderer.cpp

## Category rules

Priority: vegetation > roads > timed > big building > LOD > building >
clump > object. A model has one category, and category multipliers do
NOT multiply one another. Ped, vehicle and weapon models are excluded.

If you set `MaxDistanceUnits=6000`, `GlobalMultiplier=5` requests up to
5 times original IDE values (times one category multiplier) but can still
clip above 6000. To experiment beyond 6000, change MaxDistanceUnits
plus WorldScanRangeUnits and CameraFarClipUnits with caution; full scan
and GPU/render-list capacity may become limiting.

## Logging / verification

Look for:

- `Map draw distances v25: ... global=5 ... category multipliers ...`
- `Map multipliers: requested range ... clamped by MaxDistanceUnits`
- `Map draw distances: ... reclassified=N ... distinct models clipped=N`
- `Map multiplier categories (evaluations/writes): ...`
- `Map draw distances CAMERA: ... RW camera farPlane=...`
- `Extended world scan: active=1 ... normal/LOD...`

**Notes:** The counters are diagnostic **evaluations and metadata writes**,
not GPU draws. Foreign hook owners are still never overwritten. This fix
does not change native RequestModel flags, cache substitution, GTA streaming
eviction, or the RenderWare model lifecycle. Enlarging the visible world can
significantly lower FPS and fill GTA's limited address space. The included
6000-unit 5x profile is opt-in and must be tested on Windows GTA SA.
