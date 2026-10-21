# World Residency V5 — Observed dependency-aware warming

V5 extends the **draw-distance-free** V4 source. Project2DFX-zen
continues to own visibility, far clip, distant LOD and rendering.

## Implemented

- The portable world catalog's `Query(cell)` now returns aligned
  `modelIds`, `modelNames`, and read-only IDE `txdNames` dependency hints.
  TXD hints require matching IDE model-ID identity; they are *not* proof
  that a particular IMG archive or Mod Loader replacement is effective.
- The ASI ↔ 64-bit helper named-pipe protocol is **v9**. Its fixed
  `CatalogCellReply` includes 64 bounded TXD names; both binaries must
  be rebuilt together.
- New opt-in `WorldResidency.WarmCatalogDependencies` policy. It applies
  only when `Enabled=1` and `WarmObservedModLoaderSources=1`.
- In this mode, the cache worker accepts an observed source only after
  checking that it was an actual Mod Loader module-local `ReadFile` handle,
  the duplicated file identity still matches the captured read, the
  current file path is classified as a Mod Loader *loose* source,
  the hint cell still equals the latest player cell, and the ASCII basename
  exactly matches the DFF model or TXD dependency of that spatial cell.
- Native GTA loads, nonmatching sources, IMG archives and unsupported
  paths are never redirected by this policy. The matching source is
  still subject to existing per-file, total-byte and count quotas.
  All checks happen on the ASI's background worker. No game-thread IPC.
- Adds `dependencyWarmMatched`, `dependencyWarmUnmatched`,
  `dependencyWarmNoHints`, `dependencyWarmUnsafe` diagnostics.
- Portable tests cover model-ID→TXD hint extraction, exact matching,
  case-folding, missing hints, unknown extensions and malformed inputs.

## Why the gate is intentionally conservative

Even a source file that exists in the catalog may *not* be the
replacement selected by Mod Loader. The only source authority available
here is a file HANDLE actually read by the Mod Loader worker. A
filename match is used solely to **restrict** an already-authoritative
observed HANDLE, never to guess a new handle or open a candidate path.

The feature is not whole-world preload. It cannot identify effective
unobserved overrides, decode arbitrary COL graphs, warm unobserved
sources, keep all world models instantiated, or bypass the x86 4 GiB VA
limit. IMG-wide dependency warming remains disabled when this new
selective mode is on, because merely observing an IMG read cannot prove
which archive member was selected for a hinted model.

## Testing on Windows (experimental)

1. Build Win32 ASI and x64 helper together: both require protocol v9.
2. Baseline: `Enabled=1`, `PrefetchModels=0`,
   `WarmObservedModLoaderSources=0`; check catalog status.
3. Enable `WarmObservedModLoaderSources=1` and
   `WarmCatalogDependencies=1`. Keep native requests and unrelated
   whole-archive warming disabled during isolated testing.
4. Visit areas with known modloader loose `.dff`/`.txd` overrides.
   Confirm `dependencyWarmMatched` grows for matching files; inspect
   `dependencyWarmUnmatched`/`NoHints`/`Unsafe`. Verify no crashes,
   stale source data, or malformed textures.
5. Compare the same route with V4 and V5; examine helper disk bytes,
   cache hits, x86 VA free, P95/P99 frame times and pop-in.
6. Test Mod Loader archive overrides, rapid teleportation, archive
   changes on disk, helper restart, save/load and disabling the feature.
   Native GTA reads must continue regardless of helper availability.

## Still remaining

- Comprehensive effective source resolution for unobserved Mod Loader files
  (requires a supported authoritative source API or live selection evidence).
- Verified source/extents for IMG members and full DFF/TXD/COL graph.
- Selective full-world warming within available x64 RAM.
- Windows x86/x64 compilation and gameplay validation.

The 41 passing portable tests do not validate the Windows-only modules.
