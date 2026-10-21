# World Residency V4 — Persistent world catalog (experimental)

This update is applied to V3's **draw-distance-free DistantStreaming**.
Project2DFX-zen exclusively owns visibility, extended LOD, far clip and draw distance.

## Implemented

When `[WorldResidency] Enabled=1`, the x64 helper now calls
`Catalog::LoadOrBuild(gameRoot, cachePath)`. It saves the *read-only* snapshot
under `%LOCALAPPDATA%\DistantStreaming\WorldCatalog\catalog-<game-root-hash>.bin`.
The game directory is not modified. A valid cache avoids reparsing the
full spatial placement catalog and candidate graph at the next launch.

The portable catalog library owns a versioned, endian-checked binary format:
- Whole-payload FNV-1a integrity check and explicit size limit (192 MiB).
- Registered static placements, cells (reconstructed and checked), candidate
  DFF/TXD/COL IMG extents, model-to-TXD dependency hints and status counters.
- Input fingerprint from canonical installation path, spatial cell size,
  `gta.dat`, full registered IDE and standalone IPL content, discovered and
  registered IMG source metadata + IMG directory, and IMG-embedded IPL bytes.
- Bounded fingerprint I/O (512 MiB). When fingerprinting is not feasible or
  the input changes while scanning, build from source *without* writing cache.
- Reject/refresh on mismatched inputs, invalid counts, corrupt header,
  truncated payload, checksum failure, inconsistent cells and invalid records.
- Best-effort temporary-file output; inability to write under LOCALAPPDATA
  never stops the game or catalog. All disk I/O remains in the x64 background
  catalog worker, not GTA's game thread.

The diagnostic `WorldCatalog64` log includes `fromDiskCache=0/1`.
The ASI/helper protocol is now **v8**. Deploy BOTH binaries from one build.

## Not an authoritative model source

Disk persistence accelerates the candidate catalog; it does **not** prove
Mod Loader's effective override selection, safely keep all native models
resident, preload every unobserved mod, or solve GTA SA's 32-bit VA limit.
Only live observed Mod Loader HANDLE identity is accepted for source
promotion, exactly as in V3. Candidate archive names remain informational.

No `CEntity` creation, remote RenderWare pointers, renderer hooks, or
Project2DFX functionality are added by this increment. Prefetch remains
opt-in and bounded by the existing runtime checks.

## Windows validation

1. Build the Win32 ASI and x64 helper together (protocol v8).
2. Enable `[WorldResidency] Enabled=1` but leave `PrefetchModels=0` and
   `WarmObservedModLoaderSources=0` for isolated first tests.
3. First run: confirm catalog `ready=1`, `fromDiskCache=0`, then inspect
   `%LOCALAPPDATA%\DistantStreaming\WorldCatalog\`.
4. Restart using the same game installation and mods: verify
   `fromDiskCache=1`, unchanged generation, placement counts and per-cell
   model hints, and lower catalog construction latency.
5. Edit a registered IDE/IPL file or an IMG IPL/directory, restart, verify
   `fromDiskCache=0`; verify next run is a hit. Delete the cache or corrupt
   it and ensure fallback rebuild and safe gameplay.
6. Test concurrent starts, write-protected LOCALAPPDATA and executable
   installation path changes. Recheck Mod Loader, Project2DFX-zen and OLA
   interoperability; measure P50/P95/P99 performance and x86 free VA.

## Limits still outstanding

- Effective override resolution for **unobserved** Mod Loader sources.
- Full effective DFF/TXD/COL dependency graph for arbitrary loose mods.
- Verified safe whole-world warming, *after* override selection is known.
- Windows compilation of both binaries and live GTA SA regression testing.

Portable CTest checks cannot establish a functioning Windows installer,
real Mod Loader IPC, actual GTA model lifetimes or frame-time gains.
