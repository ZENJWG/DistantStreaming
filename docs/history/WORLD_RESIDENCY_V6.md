# World Residency V6 — observed, verified IMG-member warming

V6 extends V5 while preserving the draw-distance-free / Project2DFX division.

## Implemented

- Protocol **v10** introduces `OpenWarmImgMember` and `ObservedImgMemberWarm`.
  Both the Win32 ASI and x64 helper must be rebuilt from the same revision.
- In the existing Mod Loader module-local `ReadFile` interception, only the
  exact file handle and byte range **actually selected/read** by Mod Loader
  are eligible. This is NOT a directory scan and does not infer unobserved
  Mod Loader override precedence.
- The ASI worker checks the original file identity and current catalog cell
  before requesting IMG-member verification. The helper duplicates the
  observed handle, validates the GTA SA IMG `VER2` directory with strict
  bounds/sector checks, and requires the observed read to fall wholly inside
  exactly one, nonoverlapping DFF or TXD member matching current IDE hints.
- IMG directories are cached by versioned file identity, for at most eight
  archives in the helper. A changed file identity causes a new parse.
- The helper queues **only 1 MiB pages intersecting that member**. The cache
  is page-granular: edge pages may also contain neighboring IMG bytes.
  It does not register that IMG for whole-archive warming or periodic repair.
- Session quotas cap member bytes (512 MiB hard maximum), total nominated
  bytes, member count, and the warm-job queue. Source handles for short-lived
  member jobs are closed after completion or terminal failure. The helper's
  existing 8-MiB in-flight cap and physical-memory backpressure remain.
- The source parser rejects unsupported formats, malformed lengths, truncated
  directories, overlapping entries, and ambiguous directory membership.
  No synthetic GTA I/O completions, native model pinning, or entity instancing
  is introduced.

## Initial test configuration (experimental)

```ini
[WorldResidency]
Enabled=1
PrefetchModels=0
WarmObservedModLoaderSources=1
WarmCatalogDependencies=1
WarmVerifiedImgMembers=1
ObservedWarmMaxSourceMiB=128
ObservedWarmMaxTotalMiB=512
ObservedWarmMaxSources=32
```

`WarmVerifiedImgMembers=0` remains the shipping default. Leave
`[ExternalAssetCache64] BackgroundArchiveWarming=0` and compare the same
saved route with V5 first.

The three parent switches must be enabled before V6 member warming is active.
When `WarmCatalogDependencies=1`, full-archive warming from observed IMG files
remains **disallowed** regardless of the new switch.

## Runtime evidence to collect on Windows

- `imgExtentMatched`, `imgExtentRejected`, `imgExtentMiB` counters.
- `AssetCache64` helper resident pages, disk bytes, ready hits, Mod Loader
  read fallbacks, helper errors and memory-pressure deferrals.
- Identical fixed routes with/without the new toggle and Project2DFX enabled,
  comparing P95/P99 frame times, load stalls, pop-in, free x86 VA and RAM.
- Replacement of a Mod Loader IMG during runtime, malformed IMG directories,
  rapid teleport, helper restart, multiple same-name members, and mod-stack
  compatibility. GTA must retain its native source reads when the helper
  declines to warm a member.

## Known limitations

- Verified IMG member warming is **observed-demand-driven**. It cannot warm
  assets which GTA/Mod Loader never selects and reads.
- No authoritative resolution for unobserved Mod Loader replacements, complete
  COL dependency graph, or whole-world source prefetch is implemented.
- Cached raw DFF/TXD bytes do not relocate GTA's RenderWare allocations
  outside the 4 GiB x86 virtual-address limit.
- All Windows x86/x64 compilation, ASI hooking, and GTA runtime tests remain
  **unverified** in the portable environment. Only the portable C++ suite is
  available here.
