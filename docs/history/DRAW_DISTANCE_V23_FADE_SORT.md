# v23: preserve real distance through fading and sorting

The supplied v22 log reports an effective normal scan and renderer far plane of
5400 units, 1,634,736 compensated visibility inputs, and 54,073 cumulative
instantiated submissions. It no longer supports blaming the old 600/1100 range
settings. The RenderOneNonRoad diagnostic hook is still unavailable; submission
counts alone do not establish what appeared on screen.

Review found an incomplete distance correction. Native SetupEntityVisibility
stores `actualDistance + modelRange - 300` in its reference output, then passes
that value by value to SetupMapEntityVisibility. The existing map hook fixed
the by-value argument only. Callers still received the inflated reference output
and forwarded it to AddEntityToRenderList for sorting and alpha fading.

For example, a 600-unit entity with an 1800-unit model range could be accepted
using 600, then sorted/faded using 2100. Native CalculateFadingAtomicAlpha uses
that distance against the model range, so the entity can fade to zero despite
being accepted by visibility.

v23 adds a guarded SetupEntityVisibility hook at 0x554230. It repairs the
reference output after native visibility returns, using the same exact-bias
matcher as the input correction. Native type, time-of-day, area, occlusion,
LOD-child decisions and visibility result remain intact. All four required
hooks roll back together if installation fails.

Sources:
- https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/Renderer.cpp
- https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/VisibilityPlugins.cpp

The supplemental scanner also collects the bounded native sector footprint and
sorts sectors by distance to the camera before visiting entities. Native polygon
row order could spend the entity/submission budget on distant sectors first.
Already-stamped native entities no longer consume the extra-entity visit budget.
World/entity pointers are still re-read live; only sector coordinates are saved.

Headroom checks now examine the lists actually written by supplemental geometry
(visible entities/LODs and deferred LOD lists). Occupancy in the unrelated
invisible-effects or super-LOD list no longer disables all extra normal geometry.
The destination capacity margins remain intact.

New telemetry:
- `returned fade/sort distances corrected`: reference-output repairs.
- `Map draw distances FINAL lists (not GPU draws)`: selected entities still in
  opaque/LOD or alpha lists at the render-scene observation, absent entries,
  and their geometric distance range. This works without the unavailable
  RenderOneNonRoad hook. It is list-survival evidence, not a GPU draw counter.

Build: MSVC Win32 Release. No tests or in-game validation run.
Only the distantstreaming project was edited. Updated ASI/PDB/map are in dist.
