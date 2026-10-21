# Performance pass: preserve verified distance, reduce speculative pressure

## Observed from user's latest DistantStreaming.csv / .log
- User confirmed the previously missing horizontal ground-level geometry is visible.
- Final CSV sample: native `vanilla_backlog=1456`, median frame ~164.28ms,
  with controller issue rate 0. These are native-queue observations,
  NOT evidence that supplemental renderer requests alone created the backlog.
- Cache64 `cdSkipHandleNull=711`, `cdHandleSlotsOpen=0`,
  `cdEligible=0`; do not invent alternate file handles.

## Changes
1. Backlog-aware request admission (default on): read the game's native
   `CStreaming::ms_numModelsRequested` at the start of each renderer frame.
   Reduce *only supplemental NEW model requests*: threshold 256->max 4/frame,
   512->max 2/frame, 1024->max 1/frame. Default below the threshold remains
   `ExtraModelsPerFrame`. Native GTA requests and already-loaded supplemental
   visibility are unchanged. Set `BacklogAwareRequests=0` to restore prior behavior.
2. Add QueryPerformanceCounter metrics for normal footprint planning/sorting,
   normal-sector traversal, LOD cell planning and LOD traversal.
   The `last/avg/peak` values are microseconds of work inside this ASI, not
   whole-frame timing or GPU work.
3. The RenderOneNonRoad diagnostic probe now uses an allocation-free
   4,096-bit probabilistic prefilter for quick misses. Every possible hit
   is still verified by exact entity-pointer comparison; false positives do
   not change accounting or renderer behavior.
4. Cache64 observes whether a native CdStreamRead succeeds even though the
   known archive-handle table is empty, and whether the associated channel
   has a non-null file field. This is **diagnostic only**; it never uses
   that file field to load, cache, or substitute bytes.

## First-game-test criteria
- At backlog over 1,024 requests: `supplemental request budget=1` and
  `backlog throttle frames` increasing.
- View distant objects at street level and aerial angle; both should stay
  visible, with no new flicker/pop-in.
- Compare `normal plan us`, `normal visit us`, `LOD plan us`, `LOD visit us`
  and FPS with the same camera position. Observe P95, not only P50.
- If `cdNullHandleNativeAccepted` rises while `cdHandleSlotsOpen=0`,
  the original GTA/other-mod path is successfully reading while this
  detector's handle table is empty; a safe solution needs identifying
  that path, not copying a channel's possibly stale HANDLE.

## Limits
- These changes do **not** guarantee improved frame time or clear GTA's
  native queue. A 1,456-request backlog can originate elsewhere.
- No MSVC/Win32 build or GTA runtime test was possible in this environment;
  the portable tests are not a substitute.
- Preserve `BackgroundArchiveWarming=0` and `ServeReadyCdStreamHits=0`
  until the read path is confirmed.
