# Additional scheduler and cache review

This pass follows the EXE/gta-reversed comparison. Changes are confined to
`I:\ag32gf3g\distantstreaming`; no game files or configuration values changed.
Backups are in `.review-backups/2026-10-20-scheduler-pass/`.

## Changes

- **Skip scans that cannot produce an admission.** The predictive controller
  already charges every speculative request at least 1 MiB and reserves native
  memory headroom. Calculate that allowance before collecting candidates. If
  less than 1 MiB remains, skip sector traversal, ranking and dynamic candidate
  construction. Request expiry/accounting still runs. This applies the existing
  admission rule earlier; it does not add a new memory limit or throttle GTA.

- **Avoid cooldown after an observed successful load.** An expired ledger
  entry could add a failure cooldown when its model had been observed loaded
  on a previous tick but had since been unloaded. That delayed later speculative
  reloads, including after pressure prevented candidate collection. Apply that
  cooldown only when neither the current nor previous observation was loaded.
  Genuine failed speculation retains its existing cooldown.

- **Reduce request-diagnostic overhead.** Scope the existing readable-region
  cache separately around request observation and around the adjacent state
  sweep/profiler poll. Recheck table binding once for those adjacent read-only
  state passes. All scopes end before native request/loading/instancing calls.
  Regions and game pointers are not retained between ticks.

- **Avoid repeated player/type lookups.** Dynamic classification obtains the
  model type once per observation. Resolve player position lazily for the first
  eligible traffic observation and reuse it for that drain batch. An empty or
  ineligible batch does not perform that player lookup. Previously eligible
  records could repeat both type queries and the same player-position lookup.

- **Allocate the sector planning buffer once.** Reserve the bounded candidate
  rectangle before collecting cells. Its dimensions are clamped to the existing
  120-by-120 grid. Ranking, top-K selection and endpoint coverage are unchanged.

- **Shorten helper cache lock duration.** With no registered warm sources,
  warm resident coverage must be zero. Skip walking every cached page under
  the metadata mutex in that case. Other coverage and allocation counters
  are still reported. Demand-only caching is the default configuration.

- **Reject jobs after archive-worker failure.** An exception could end the
  optional in-process archive worker while its state continued accepting misses.
  Mark it stopped when that happens and reject further queued work. Immutable
  cached hits remain usable until normal shutdown clears the cache. This is
  separate from the external x64 cache and remains disabled in the primary INI.

## Verification and limits

MSVC Release builds of the Win32 ASI and x64 helper succeeded. Fast packaging
updated `dist`, including ASI symbols. Both binaries must be used together;
the transport protocol remains unchanged. Logs:

- `build-vs2026-win32/scheduler-pass-build.log`
- `build-review-x64/scheduler-pass-package.log`

No tests, benchmarks or game launch were performed, as requested. These changes
remove identified allocations, scans, queries and queueing mistakes; actual
frame-time and gameplay effects remain unmeasured. Earlier review/test results
apply to their respective earlier builds.
