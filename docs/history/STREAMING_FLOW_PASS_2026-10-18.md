# DistantStreaming streaming-flow observation pass (2026-10-18)

## Scope and limits

Low-overhead attribution of GTA's native streaming queue and controller
CPU cost. The optional extended renderer and its supplementary
`RequestExtraModel` path described in older revisions have been removed.
Neither this profiler nor the remaining scheduler modifies renderer visibility.

## New telemetry in DistantStreaming.log

- `Streaming flow`: 1-second *hook-call* counts for native/vanilla and
  DistantStreaming-owned RequestModel invocations, native calls outside
  the main thread, and native backlog sampled on controller ticks.
  Hook counts include nested requests by the game and are not distinct IDs.
- `Streaming state sweep`: a 512-model bounded slice is inspected each
  controller tick, with readable-memory preflight and OLA binding check.
  A complete sweep reports per-state counts across a multi-tick window.
  `observed pending-to-loaded`, `loaded-to-notLoaded`, and
  `pending-to-notLoaded` are **sampled transitions**, not exact completion
  or removal events. Transient transitions may be missed.
- `Streaming scheduler CPU us`: average/peak/count since last log for
  model-state sampling, camera readiness, predictive controller, world
  preloader, archive reporting and Cache64 reporting. These omit other
  GTA engine work and cannot be interpreted as total frame CPU time.

## Safe testing

1. Build on Windows using `build.bat` (the x64 helper can be reused).
2. Replace only `DistantStreaming.asi`; keep the previously working INI.
3. Confirm native streaming and Project2DFX visibility behave as expected.
4. Capture `DistantStreaming.log` and `.csv` for at least 1 minute to
   permit multiple rolling model-state sweeps. Look at hook provenance,
   backlog rising/falling and stage CPU timing to identify the next safe
   optimization.
5. For Cache64, retain `ServeReadyCdStreamHits=0` until the documented
   native-channel vs archive-handle mismatch is resolved. This patch does
   not use channel handles as sources of cached bytes.

## Validation

Portable Debug tests include the rolling state-sweep/backlog-meter tests.
Win32 HOODLUM assembly/hook behavior has not been compiled or tested in GTA.
