# DistantStreaming stutter-stability pass (2026-10-18)

## Evidence from supplied capture

- The 39-row CSV ends with a 7.64 ms rolling median frame time and 10.15 ms
  rolling p95; earlier p99 reaches 70.61 ms. This is an intermittent
  frame-pacing problem, not a continuous 15–20 FPS cap.
- Startup logs show `CameraReadiness blocking=1` and `Asset preload enabled=1`,
  despite safer configurations shipped in the preceding project.
- Observed native synchronous camera loads: worst single flush 27,411 us;
  a one-second window has 51,255 us cumulative block time.
- DistantStreaming `controllerTick` reaches 49,094 us; most of its cost is
  optional prediction, camera catch-up, and reporting on GTA's main thread.
- About 1,821 MiB native streaming used / 1,898 MiB native budget at the end,
  with 844 MiB free address space. Native helper-first reclaim remains zero.
- GPU VRAM use is ~1 GiB against a reported ~11 GiB local process budget;
  the GPU allocation budget is not the dominant pressure in this capture.

## Implemented

1. New `[FrameStability]` gate, active by default. Overrides legacy
   `CameraReadiness.BlockingLoad=1`, unless explicitly opting in to unsafe
   synchronous blocking using `AllowBlockingCameraLoads=1`. This prevents the
   ASI from calling `CStreaming::LoadAllRequestedModels(true)` for catch-up.
   Vanilla GTA and mods retain their original loading behavior.
2. Disable optional speculative world-preload pins by default when this gate
   is active. Opt in with `AllowSpeculativeAssetPreload=1` if intentionally
   benchmarking preloading.
3. Run optional prefetch controller at >=120 ms intervals; if a tick costs
   >=4 ms, back off for >=350 ms. Refresh native memory guards and process
   normal game callbacks at their original rates.
4. Cap speculative predictive world scans at 24 cells and 96 candidates.
   The game's own world scanner, DistantStreaming extended ground scan and
   far LOD rendering are **not** capped by these new limits.
5. Cap camera catch-up at 16 cells / 768 nodes / 1500 us per scan, with
   a bounded instancing count and the existing 1 ms instancing time check.
   These are supplementary readiness checks, not render visibility checks.
6. Add `Frame stability:` startup config and one-second
   `Frame stability controller:` gate counts for future log comparison.

## Configuration (safe defaults)

```ini
[FrameStability]
Enabled=1
AllowBlockingCameraLoads=0
AllowSpeculativeAssetPreload=0
ControllerIntervalMs=120
SlowControllerUs=4000
SlowControllerRetryMs=350
MaxPredictiveCells=24
MaxPredictiveCandidates=96
```

The supplied `config/DistantStreaming_STUTTER_STABLE_5X.ini` retains the 5x
multiplier and 6000-unit range from the prior experimental profile. That range
can intrinsically increase CPU/render and native RAM load, so the 3000-unit
profile remains the useful control comparison.

## Important limitations

This change is intentionally **not** a general-purpose asynchronous engine:
GTA SA's entity arrays, RenderWare instances, native model streaming state,
and D3D9 device cannot safely be mutated from an arbitrary x64 worker. Existing
x64 worker IPC and file-cache operations remain asynchronous, while GTA
resource ownership remains native. A single native CreateRwObject call can
still exceed the soft per-pass budget and cannot be interrupted safely.

The portable CMake project tests policy logic. It cannot establish Win32 ASI
compatibility or guarantee measured in-game FPS improvement. In particular,
memory pressure and optional Mod Loader interference are separate remaining
causes of frame-time spikes.

## Validation checklist

- Confirm startup says `Frame stability: enabled=1, blocking camera loads=0,
  speculative preload=0`.
- Confirm `Camera catch-up: ... blocking=0` and bounded `scan cells/nodes/us`.
- Confirm `Frame stability controller: ... slowRuns=...` and that slow spikes
  trigger skipped predictive ticks.
- Compare the same route and camera, including a 3000-unit control profile.
- Watch camera `camera_block_peak_us` (target: zero), frame p95/p99,
  engine backlog, native memory headroom, and no visual holes/LOD omissions.
