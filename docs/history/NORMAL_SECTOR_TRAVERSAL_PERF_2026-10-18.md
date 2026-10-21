# Frame-scoped normal-sector traversal optimization (2026-10-18)

## Trigger
The prior in-game log measured `normal visit us last/avg/peak` of
`115270/21669/124106`, compared with LOD visit `4786/4689/41394`.
The user confirmed horizontal ground-level distant geometry renders.

## Code changes
- Added **positive-only**, fixed-capacity 32-region validation cache used
  during the extended normal and LOD traversals. When a pointer resides wholly
  inside a *previously validated* committed-readable memory region, skip a
  redundant `VirtualQuery`. Unknown, misaligned, overflowing and invalid
  pointers still pass through the original guard and OS query. Cache resets at
  every `BeginRendererFrame`; no entity pointer is cached.
- Deduplicate GTA repeat-sector object list traversal within each frame.
  Normal world sectors map repeat lists by `(x % NUMREPEATSECTORS_X,
  y % NUMREPEATSECTORS_Y)`; scanning the same list at every matching sector
  causes redundant node validation without finding new entities. World
  building/dummy lists and LOD lists are still scanned as before.
- Counters: `normal linked-list nodes checked`, `repeat lists scanned`,
  `repeat list revisits avoided`, `readable region cache hits`, and
  `readable region OS queries`. Cached region hits are not proof of object
  lifetime; native game mutation remains a risk with any pointer dereference.

## Boundaries
- No draw-distance, sector range, alpha, native scan-code, entity-visibility,
  LOD fairness, or native GTA renderer budget was changed.
- No hard CPU cutoff or cross-frame entity cache (which could cause flicker).
- Does not repair Cache64's incompatible/empty archive-handle representation.
  Leave `ServeReadyCdStreamHits=0`, `BackgroundArchiveWarming=0`.

## Validation
- CMake portable Debug build and all 32 CTest cases passed, including new
  region-cache eviction/reset/bounds tests.
- The Win32 ASI and visual/FPS improvement cannot be verified here without
  MSVC and GTA SA. In-game regressions to watch for: missing objects, flicker,
  crashes, and changed `normal visit us avg/peak`.

## Same-location A/B test
Use a stable view with the previous and updated ASI separately. Compare
`normal visit us avg/peak`, `repeat list revisits avoided`, `readable region
cache hits/OS queries`, FPS p50/p95 and distant geometry. The counters are
cumulative: use deltas over an equivalent run, not their raw totals. If
objects disappear, revert to the previous ASI immediately.
