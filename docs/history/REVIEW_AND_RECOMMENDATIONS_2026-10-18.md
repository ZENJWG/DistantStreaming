# Focused source review - 2026-10-18

## Changes made

- `src/service64/NativeMemoryService.cpp`: check physical memory pressure from
  the helper's worker loop, including when there are no archive-warming jobs.
  Previously, trimming required a ready warm job even though demand-only caching
  is the default. Below the existing 2 GiB free-physical-memory threshold, trim
  up to 32 MiB of cache per check. Checks run at most once per 250 ms. Warming
  remains paused when memory is low or cannot be measured. This is gradual,
  best-effort reclamation; demand reads can still allocate pages under their
  existing admission policy, and in-flight readers can temporarily retain pages.
- `src/game/AsyncAssetCacheWin.cpp`: require writable `OVERLAPPED` memory before
  the Mod Loader fast-hit path updates `Internal` and `InternalHigh`. Previously
  only committed/readable memory was required. Failed validation delegates to
  the original reader. This remains a point-in-time check, not a guarantee
  against concurrent caller memory changes.
- `CMakeLists.txt`: apply Release assertion protection to every currently
  declared target whose name ends in `_tests`. The manually maintained list
  omitted several existing suites, including renderer work budget, frame pacing,
  request profiling, and hook-entry policy. This prevents those assert-based
  suites from silently becoming ineffective in Release configurations.

Original copies are under `.review-backups/2026-10-18/`, preserving relative
paths. The directory was not a Git checkout during this review.

## Recommended next work, in priority order

1. **Bound actual extended-renderer traversal work.**
   `Manager::ProcessExtendedSector` limits each list to 256 nodes, but the
   shared 4096-entity counter advances only after pointer/type/static/scan-code
   checks. Repeated or rejected nodes still cost CPU. The outer pass can also
   visit many empty sectors. Add aggregate sector/node counters and consider
   a soft time budget checked between entities. Preserve near/far quotas and
   rotate deferred traversal so a fixed cutoff does not starve distant areas.
   A render-list is rebuilt each frame, so carrying a cursor alone may create
   visible omissions; this requires visual review before adoption.

2. **Reduce repeated sector planning.**
   `ScanExtendedNormalWorld` rasterizes the frustum and sorts the complete
   `scanSectors_` vector every frame, potentially covering the 120 by 120 grid.
   Use the existing `normal plan` versus `normal visit` timings to decide which
   part warrants work. Candidate approaches include distance buckets or reuse
   of conservative sector footprints keyed by camera cell, orientation, clip
   range, and view window. Revalidate current visibility every frame; never
   retain entity pointers as a shortcut. Teleports and camera/config changes
   must invalidate any reused planning data.

3. **Make helper cache capacity authoritative at publication time.**
   `AssetCache::Load` captures capacity before asynchronous file I/O. Another
   service command can allocate shared backing and trim the cache while that
   read is in flight. The finishing load still publishes using the older,
   larger capacity. Store the current cache limit in shared cache state and
   check it under the cache mutex when publishing. Account separately for
   indexed pages, pending pages, and pages retained by active readers if the
   configured budget is intended as a strict memory bound.

4. **Measure cache savings against cache interception cost.**
   `SourceKey` queries file metadata before lookup, `Lookup` copies bytes while
   holding the local cache mutex, and misses schedule a second source read in
   the helper. Track supplied bytes and hit rate alongside hook CPU time, copy
   time, and helper disk bytes. Consider reducing or bypassing warming for
   persistently low-reuse sources. Byte/latency savings matter more than cache
   occupancy alone. Any metadata memoization must account for handle reuse
   and source changes.

5. **Consolidate release documentation and version reporting.**
   The README accumulates historical release introductions, while startup code
   still identifies itself as v18 and renderer code refers to v25. Use one
   build/version identifier and a short current-feature/configuration table;
   move historical explanations into the changelog. Put the project under
   version control so fixes and packaged binaries can be traced together.

## Verification and scope

Compiled Release targets `DistantStreaming` (Win32) and
`DistantStreaming_AssetCache64` (x64) using the existing configured build trees.
Both succeeded. The ASI link emitted the existing Plugin-SDK `/GL` / `/LTCG`
restart notice. No tests were run, as requested, and GTA was not launched.
No measured FPS or gameplay-stability claim follows from this source review.

Matching binaries and ASI symbols were copied to `dist`; its INI was preserved.
The older packaged files were backed up alongside the original source copies.
This review focused on active streaming, cache transport, renderer traversal,
and build configuration; it was not an exhaustive audit of every legacy module.
