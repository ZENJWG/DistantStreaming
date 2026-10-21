# Ground-level extended LOD and Cache64 pressure fix

## Observed in 2026-10-18 log
- Native render far clip 5400; extended normal scan active.
- LOD sectors/LOD submitted remained zero because `ExtendLodGrid=0` and
  the old far LOD scan used the long normal-sector range as the near cutoff.
- The 64-bit helper eagerly warmed 12,845 MiB but `cdEligible=0` and
  `cdHits=0`, while game streaming was in emergency.
- The source leaves `ServeReadyCdStreamHits=0`: the original native stream
  channel completion behavior is not yet verified on this executable.

## Source changes
1. Enable the separate 30x30 LOD grid in the shipped INI, and scan from
   300 units (native short-range handoff) out to the configured far range.
   Never use the *normal* 120x120 scan range as the LOD near cutoff.
2. Scan LOD parents immediately after extended normal/timed children, at
   the native pre-LOD callback boundary, preserving scan-code deduplication.
   Fall back at EndRendererFrame if GTA did not dispatch the callback.
3. Add independent LOD-plan and request-denial counters and camera pitch
   diagnostic. These counters are cumulative except for the footprint cells.
4. Set `BackgroundArchiveWarming=0` by default. Cache64 will now cache
   demand reads only; this avoids pressure from multiple GiB of unused
   speculative IMG pages. User can opt in to prewarming explicitly.
5. Add `cdSkipDisconnected`, `cdSkipAddresses`, `cdSkipChannels`,
   `cdSkipDecode`, `cdSkipHandle`, and `cdSkipSource` counters. They identify
   which fail-open CdStreamRead eligibility gate accounts for skipped reads.
   Do NOT change GTA addresses or force `ServeReadyCdStreamHits=1` until
   these counters and native completion semantics are verified.

## Deployment
- Build the Win32 ASI with `build.bat` (fast/incremental is supported).
- Cache64 executable does not require recompiling for this ASI/config change.
- Copy updated `DistantStreaming.asi` and `config/DistantStreaming.ini`
  into the game, preserving personal settings as needed.
- If retaining an older INI, **manually set**
  `ExtendLodGrid=1`, `LodGridRangeUnits=5400`,
  `BackgroundArchiveWarming=0`.
- Keep `ServeReadyCdStreamHits=0` until completion-state tests exist.
- In matching sky-down and ground-forward views, compare
  `LOD plan calls`, `LOD planned cells`, `LOD sectors`, `LOD submitted`,
  `native stream-me`, `request budget skips`, and all `cdSkip...` counters.

## Constraints
This is a bounded, opt-in experimental renderer extension. The GTA/OLA
engine memory budget can still evict distant resources; static IPL and
render-list limitations still apply. No native GTA SA rendering or
CdStream completion simulation has been run on this Linux host.

## Follow-up: LOD scan fairness

The later in-game log showed 173,393 cumulative LOD tiles planned but only
4,919 visited across 527 frames. Previously, the closest sorted cells were
visited every frame, starving farther LOD tiles whenever frame budgets were
exhausted. `ScanFarLodGrid` now rotates a numeric cursor across the freshly
computed per-frame plan; no CEntity or list-node pointer survives the frame.
Existing limits (`LodCellsPerFrame`, `LodEntitiesPerFrame`, render-list capacity)
remain unchanged. `LOD next cell cursor` reports the next starting index.

**Not fixed by this change:** Cache64's `cdSkipHandle` counter was 886/931.
The helper is connected, but all reads were rejected and background warming
and hit substitution are disabled by configuration. Do not enable unsafe
hit substitution based on this LOD scheduler patch. Verify the packed file ID
and GTA image handle table for this exact executable build separately.
