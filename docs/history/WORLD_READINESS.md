# Faster building and texture readiness — 2026-10-15

The latest supplied run confirms observation coalescing is active: zero lost
observations, 626 sampled model completions, and 42 speculative requests in
16 CSV samples. Some sampled readiness and queue p95 windows reached about
4.7 seconds. These are measurements of the previous deployed revision, not
of the changes below. Queue-state sampling cannot isolate disk, decoding,
rendering, or GPU upload costs precisely.

## Avoidable delays addressed

1. **Completed-load cooldown.** The controller previously added a six-second
   cooldown when an already-loaded tracked model later unloaded, even if it
   was still wanted. It also cooled down completed models whose tracking
   history expired. Both paths now permit a real reload at the next eligible
   tick. Failed/cancelled speculative requests retain their churn cooldown.
2. **Loaded assets occupying candidate capacity.** World scanning now removes
   loaded, absent-on-disk, and protected models before scoring/top-K selection.
   Missing assets can fill the candidate budget. Pending models remain visible
   for demand refresh, while admission still avoids duplicate requests.
3. **Prediction sectors starved by dense current sectors.** Scan the current
   and predicted endpoint sectors before the remaining ranked cells.
4. **Missing LOD coverage.** Add local scans of GTA's separate 30x30 LOD pointer
   grid. Deduplicate coarse grid cells, reserve 1,024 linked-list visits for
   LODs, and allow their longer draw distance. Ordinary buildings/dummies use
   up to 3,072 visits; the total remains bounded by 4,096 node visits per tick.
   This is local predictive loading, not a whole-map LOD preload.
5. **Near world geometry waiting at ordinary priority.** Rank nearby missing
   world candidates first. Issue at most two new near-world requests per tick
   with native `PRIORITY_REQUEST`, within the shared quota/backlog limits.
   Yield this priority path whenever GTA already reports priority requests;
   do not use it for dynamic traffic, throttled/critical memory, unsafe gameplay,
   or frame/load guards. Existing vanilla calls/flags are unchanged.
6. **Moderate pressure completely stopping prediction.** `Throttled` now allows
   at most one ordinary request per tick. Critical pressure and actual process/
   address/physical guards still stop requests. This prevents indefinite
   zero-admission at moderate streaming occupancy.

`RequestModel` remains asynchronous. Its native handling requests the model's
TXD dependencies using the supplied flags; the plugin does not perform direct
texture conversion, entity spawning, or a synchronous bulk load. This behavior
was checked against the [GTA reversed streaming implementation](https://raw.githubusercontent.com/gta-reversed/gta-reversed/master/source/game_sa/Streaming.cpp).

## New default profile

Copy **both** the rebuilt ASI and updated INI to use the new profile. Existing
explicit INI values are honored; the plugin does not overwrite the game's INI.

| Setting | Previous | New |
| --- | ---: | ---: |
| UpdateIntervalMs | 100 | 50 |
| LookAheadMs | 1500 | 3000 |
| MaximumPredictionUnits | 180 | 300 |
| SectorRadiusUnits | 90 | 150 |
| MaxSectorScans | 32 | 64 |
| MaxCandidates | 128 | 256 |
| MaxRequestsPerTick / MaxAdaptiveRequests | 4 | 8 |
| MaxOutstanding / BacklogLimit | 12 | 24 |
| ThrottlePercent / HighPercent / ResumePercent | 75 / 85 / 70 | 85 / 92 / 80 |
| MaxPriorityRequestsPerTick | unavailable | 2 |
| PriorityRadiusUnits | unavailable | 100 |

Set `MaxPriorityRequestsPerTick=0` to disable the added priority path. Quotas
count speculative model requests; vanilla may add TXD/dependency requests.
Frame-time feedback still reduces admission to one or zero when necessary.
The LAA-aware streaming limit, VA reserve, and physical-memory guard remain.

Reuse the bounded world scoring buffer across ticks and replace candidate
deduplication hash sets with small fixed bitsets. Doubling scheduler frequency
therefore does not also require per-candidate hash-node allocations. Traversal
and admission remain on the game thread; no RenderWare work moves to workers.

## Validation and next run

- VS 2026 Win32 Release ASI build passes.
- All seven CTest suites pass, including regressions for immediate loaded-to-
  unloaded recovery, completed-history expiry without cooldown, near-world
  ordering, priority caps/shared quotas/yielding/disable setting, moderate
  pressure progress, and critical/frame/backlog guards.
- The source separation audit passes.
- World-sector/LOD traversal compiles against the installed Plugin-SDK. Its
  gameplay behavior and the changed profile's performance are not yet tested.

The startup log now records `World readiness` with the active tick, lookahead,
request quota, and priority quota. New CSV columns: `world_priority_issued`,
`priority_backlog`, `readiness_pending`, `readiness_max_pending_ms`, `frame_guard`,
and `memory_guard`. Priority counts are reporting-interval totals. Pending,
backlog and guard fields describe their sampled state. The analysis script
also reports worst-window queue/readiness p95 for comparing matched routes.

After a normal game restart, test the same building/texture route, first on
foot and then at speed. Compare readiness/queue windows, pending backlog,
memory headroom, and frame times. No new runtime speedup is claimed yet.
Instant visibility cannot be guaranteed: disk reads, texture decoding,
allocation, GPU uploads, draw distance and the game's rendering cadence still
determine when an asset actually appears. A zero or short readiness sample
does not prove that a building was rendered at that instant.
