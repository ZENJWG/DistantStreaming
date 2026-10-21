# Normal and timed draw-distance correction (2026-10-17)

## v21 follow-up: LOD selection order

The follow-up log confirms `Extended world scan ACTIVE v20`, 526,238 corrected
distance calculations and 99,214 instantiated supplemental submissions. Enabling
the hooks therefore did not fully explain the visual problem.

The v20 extension scanned normal/timed children AFTER the entire native
`ScanWorld`, including its LOD-parent scan. Native GTA scans children first, then
uses their rendered-child counters when selecting the parent. Running the extra
children late could leave an already-selected low-detail parent in the render
lists. The initial fix did not address this ordering defect.

v21 hooks `CWorldScan` (0x72CAE0) and recognizes the native LOD callback
`ScanBigBuildingList` (0x554B10). It completes the bounded extra normal scan
before entering that LOD polygon traversal, after the native normal traversal
has returned. This also avoids re-entering a running native polygon scanner.
The native visibility routines still manage timed-model eligibility, child
counters, shared parents, fading and fallback for missing children. No global
LOD disabling or permanent entity-link changes are used.

The optional extra far-LOD scan remains after native scanning, before native
`ProcessLodRenderLists`. Per-frame budgets reset once at renderer entry, and
the boundary hook runs the extra normal pass at most once per renderer frame.
All three required renderer hooks are rolled back if installation fails.

The startup line now says `Extended world scan ACTIVE v21` and telemetry adds
`normal scans before LOD`. The diagnostic RenderOneNonRoad hook was unavailable
in the supplied run, so submission counts do not prove final rendered output.

The supplied game INI has `WorldScanRangeUnits=10000`, which the existing parser
caps at 1200, and `CameraFarClipUnits=1100`, which further caps the supplemental
scan at 1100. This patch does not remove those existing bounds.

Win32 Release built successfully; no tests or in-game validation were run.
Updated ASI, PDB and map are in this project's `dist`; game files are untouched.

## Initial correction

The supplied game log confirms 16,359 model-distance writes with no external-writer
conflicts, but reports `experimental extended world scan=0`. The model values were
changing; the renderer extension needed to use those values beyond the native
normal-world range was disabled.

GTA's `ScanWorld` builds the normal-sector footprint at 300 units independently
of the camera far plane. `SetupEntityVisibility` also adds `modelRange - 300`
to distances beyond 300 when the model range is enlarged. Both normal atomics
and timed atomics use this path. Increasing metadata or camera far clip alone
does not remove those restrictions. See the native implementation:
https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/Renderer.cpp

Changes:

- Enable the existing guarded renderer extension in the normal project INIs
  and when the INI key is absent. An explicit `ExtendedWorldScan=0` is still
  respected and now logs the remaining normal/timed-model restriction.
- Apply optional fog extension even when weather or another mod already supplies
  a far plane above the configured target. The supplied log had a 1500-unit far
  plane but fog starting around 32-82 units; the old conditional skipped fog.
- Below `ExtraScanMinFreeVaMiB`, scan only already-instantiated entities with
  resident model objects, blocking extra requests and instantiation instead of
  dropping the entire supplemental scan. The supplied session fell below the
  default 1024 MiB threshold. Request and visible-list budgets remain enforced.
- Route normal-sector requests through the same load-state, request-cooldown,
  and near/far request reservation policy used by the far-LOD scanner.

The normal-model ceiling remains `WorldScanRangeUnits` (600 in the normal INI),
plus native model/far-clip/occlusion/time-of-day checks and fixed list budgets.
`GlobalMultiplier=10` does not imply ten times the visible world range.

Build: MSVC Win32 Release target `DistantStreaming` completed successfully.
Updated ASI, PDB and map are in `dist`. No tests or in-game validation were run.

Only this project was edited. To install, copy `dist/DistantStreaming.asi` to
the game and set `[DrawDistance] ExtendedWorldScan=1` in the game's existing
INI, retaining its other custom settings. A pre-existing explicit zero is not
overridden by the new missing-key default. Hook conflicts are still rejected
and reported as `Extended world scan DISABLED`.
