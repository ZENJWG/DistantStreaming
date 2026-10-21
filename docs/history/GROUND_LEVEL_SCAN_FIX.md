# Ground-level extended scan follow-up (source-level experimental fix)

## Evidence from supplied v25 log
For cumulative `Extended world scan sectors`:
- frames 56 -> 106: (459657-182580)/50 = 5541.5 sectors/frame
- frames 364 -> 438: (801691-798977)/74 = 36.7 sectors/frame
- frames 438 -> 552: (804609-801691)/114 = 25.6 sectors/frame.
Camera far clip remains 5400. The log has no camera pitch, so those intervals
cannot be labeled sky vs ground without a controlled run.

`ExtendLodGrid=0` in the supplied config and native LOD-sector telemetry stays 0.
The renderer model cap of 64 supplemental submissions, nearest-sector-first
order, and strict 4096-entity budget can starve far sectors at ground level.
The final visible-list observer failed to verify OLA arrays; selected/not-observed
counts are not proof of actual GPU visibility.

## Changes
1. Project the existing camera apex and 4 far-plane corners in floating-point
   GTA sector coordinates, create their 2D convex hull, and rasterize
   conservatively into the clamped 120 x 120 sector grid. This removes the
   dependence on the native scan function's treatment of coincident/self-
   crossing/out-of-bounds projected vertices. The native GTA sector scanner and
   renderer pipeline are still used for GTA's own work.
2. Reserve up to half of the 64 supplemental submissions and 4096 candidate
   visits for the farther part of the projected footprint, then process nearby
   and any unused far budget. Model request admission, native visibility,
   time-of-day, occlusion, read-safe linked list limits and OLA checks remain.
3. Extend diagnostics with projected sector-cell count, camera forward.z, and
   native visibility response counters (invisible, culled, stream-me, offscreen,
   occluded). Cumulative counters need delta calculations across reports.

## Validation
- Pure C++ footprint tests (`tests/scan_footprint_tests.cpp`) compile and pass
  under g++ C++23 in this sandbox.
- Win32 GTA plugin build NOT run: Windows Plugin-SDK, MinHook, and native
  engine/Hoodlum execution are not available in this container.
- No in-game observation. This is a source candidate, not a verified visual fix.

Build with the existing project MSVC x86 configuration and retain the original
ASI for rollback. For a reproducible A/B run, hold camera position, heading,
time-of-day and weather fixed; sample level-looking versus steep-down pitch.
Compare projected cells, cumulative normal scans, native status counts, delta
additional requests and actual on-screen assets. If cells are numerous but
culled counts dominate, inspect GetIsOnScreen / occlusion / IPL LOD ownership,
not fog or draw-distance metadata.

Risks:
- Swept convex hull may intentionally scan more sectors than the former
  non-convex/native scan polygon; native visibility still decides what renders.
- Dense scenes and very wide frustums may increase CPU cost despite per-frame
  visit/submission caps; profile frame p95 and adjust MaxDistanceUnits and
  ExtraVisiblePerFrame if necessary.
- This does not enable optional ExtendLodGrid, repair unverified OLA observer
  operands, or change the preloader's VA emergency behavior.
