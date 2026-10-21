# Runtime performance changes — 2026-10-21

All changes and outputs are confined to this project. Original edited sources
are preserved under `.review-backups/2026-10-21-runtime-performance/`.

- Skip installing the CdStream dispatcher hook when ready-hit serving is off.
  Previously, that mode still performed metadata checks and queued helper reads
  without serving dispatcher hits. Mod Loader caching and selected-source
  observation remain independent. Enabling `ServeReadyCdStreamHits` retains the
  existing guarded dispatcher implementation.
- Poll passive streaming-state slices and request-profile completions every
  100 ms instead of every scheduler tick. This reduces diagnostic CPU cost and
  temporal resolution; memory/backlog checks and request observation retain
  their cadence. The 512-entry sweep therefore takes longer to complete.
- Revisit unchanged stationary predictive scenes on a 250 ms idle cadence.
  Player/camera movement over one world unit, camera direction changes over
  0.025 in vector length, new observed requests, native backlog/memory changes,
  area changes and camera validity changes restore scanning on the next
  eligible controller tick. Movement is compared to the last actual scan,
  so small changes accumulate. Dynamic readiness and expiry still run.
  No entity pointers or old candidates are reused between scans. Newly
  appearing entities without another trigger may wait for the idle interval.
- Fill reusable controller candidate storage through the game adapter, avoiding
  a per-scan vector copy/allocation. The adapter guarantees finite scores,
  unique valid IDs and the controller's priority order, so its output skips
  redundant normalization. Other adapters retain the existing normalization
  through a backward-compatible default implementation.
- Track helper bytes fetched and ready-hit bytes served per exact source
  identity in a bounded 256-slot table. After at least 8 MiB fetched, sources
  serving less than one byte per eight fetched back off to one optional fetch
  admission per five seconds. Existing queued work can still finish. Ready
  lookups remain enabled and useful hits can end the backoff. Hash collisions
  forget statistics; they never permit source-byte substitution. Identity
  includes volume/file IDs, size and modification time, excluding read extent.
  Broad observed-source/dependency/member warming now requires useful ready
  hits at the same 1:8 ratio. Unrelated explicitly configured background archive
  warming is unchanged. Native reads remain the fallback for every skipped job.

Controls in the primary INI:

| Section/key | Default | Effect |
| --- | --- | --- |
| `Diagnostics/StateSampleIntervalMs` | `100` | Passive diagnostic poll interval, clamped to 20–1000 ms |
| `Prefetch/IdleScanIntervalMs` | `250` | Stationary predictive interval, clamped to 0–1000 ms; 0 restores scanning every eligible tick |
| `ExternalAssetCache64/AdaptiveSourceWarming` | `1` | Set to 0 to restore unfiltered helper fetch/warming admission |

`lowReuseSkipped` and `lowReuseWarmSkipped` in the existing cache log count
adaptive admission and broad-warming skips. They do not count cancelled native
requests. Low cache reuse can reflect an absence of repeated demand rather than
a cache malfunction.

Verification: Release Win32 ASI and x64 helper compilation. No tests, benchmark
or game launch was requested or performed; gameplay gains remain unmeasured.
