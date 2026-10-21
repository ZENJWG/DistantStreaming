# World Residency V3 — Observed source warming and candidate graph

## Scope

This changes the actual ASI, x64 helper, catalog library and tests from V2.
No draw-distance, LOD-renderer, new `CEntity` spawning or 32-bit memory-limit
workarounds are introduced.

### Observed effective sources (opt-in)

When `[WorldResidency] Enabled=1` and `WarmObservedModLoaderSources=1`,
the ASI's existing **module-local Mod Loader ReadFile IAT hook** marks a job
only after Mod Loader actually reads a selected IMG sector or a complete
loose DFF/TXD/COL source. The existing worker validates the source's full
file identity after the read, then transfers the **same original HANDLE**
to the x64 helper via `OpenWarmSource`. This is not guessed from folder
ordering or from IMG file name collisions.

Source promotion is done ONLY on the background worker. Native ReadFile and
Mod Loader completion behavior are untouched. The helper duplicates the
HANDLE, warms 1 MiB source pages in a separate background thread, and
retains the same cache identity validation and cache-capacity constraints
as existing archive warming. The new feature never registers sources
after a failed/mismatched read.

Three independent admission limits avoid runaway x64 handle and RAM use:
`ObservedWarmMaxSourceMiB`, `ObservedWarmMaxTotalMiB`,
and `ObservedWarmMaxSources`. Defaults are 512 MiB/file, 2048 MiB total,
64 distinct source identities. `WarmObservedModLoaderSources=0` by default.
No warming occurs if the IAT observer cannot be installed.

**Important:** This proves only that Mod Loader selected a source at the
observed moment. It does NOT resolve its priority order across all mods,
prove that all world assets have been seen, or register unseen asset sources.
Do not equate helper cache capacity with bytes actually served.

### Read-only archive asset candidate graph

`WorldCatalog` now records bounded `.dff`, `.txd` and `.col` IMG v2
directory candidates (including file offsets and extents) and IDE
model-to-TXD references. It preserves collisions rather than selecting
one candidate. `Catalog::Dependency()` and `CandidateAssets()` provide
read-only portable diagnostics. All candidate counts are labeled as
**unverified** archive matches; an actual selected Mod Loader HANDLE is
still the only source proof used for warming.

Helper protocol is now **v7** (both ASI and x64 helper must be rebuilt
and deployed together); `CatalogStatus` reports candidate entries,
ambiguous names and IDE-to-TXD links. No API transfers guessed archive
filenames to GTA's synchronous streaming path.

### Configuration

```ini
[WorldResidency]
Enabled=1
PrefetchModels=0
WarmObservedModLoaderSources=1
ObservedWarmMaxSourceMiB=512
ObservedWarmMaxTotalMiB=2048
ObservedWarmMaxSources=64
```

Begin with `PrefetchModels=0` to isolate caching from changes in streaming
behavior. `InterceptModLoaderLooseReads=1` is required in
`[ExternalAssetCache64]`. Leave bulk `BackgroundArchiveWarming=0` when
testing the observed-source path alone.

### How to validate on Windows

1. Build **Win32 ASI and x64 helper** from the same source revision, v7.
2. Run first with world residency/catalog on but observed warming off.
   Record baseline frame P50/P95/P99, cache bytes served, Mod Loader
   eligible reads, source cache population, native backlog and x86 free VA.
3. Re-run the same save and route with observed warming enabled.
   Inspect `observedWarmSources`, `observedWarmMiB`,
   `observedWarmDenied`, `observedWarmErrors`, plus actual cache hits/bytes.
4. Repeat with Project2DFX-zen, OLA and Mod Loader installed.
   Investigate any crash, mismatch, mission/area behavior, loading stall,
   or new pop-in before enabling native catalog prefetch separately.

## Still not implemented

- Complete authoritative Mod Loader override priority and profile enumeration:
  live observed reads are proof only for visited assets.
- Complete effective DFF/TXD/COL dependency graph, including loose files,
  COL/IPL activation state and all game-mod-specific precedence.
- Whole-map *unobserved* source warming and persistence of catalog data.
- Windows x86/x64 compilation and live GTA gameplay benchmarks.

This V3 is a source update and testable implementation step,
**not** a claim that every native model is resident beyond the x86 4 GiB VA.
