# Historical hook coexistence and speculative memory stability — 2026-10-18

**Current-state note:** the draw-distance renderer, its render-list fallback
and any extended LOD/visibility hooks discussed in the original investigation
were subsequently removed from DistantStreaming. The memory-pressure and
foreign-detour safety considerations remain useful historical context.
No renderer hook described below is shipped as a DistantStreaming draw-distance
feature.


## Evidence that motivated this pass

The post-attribution `DistantStreaming.log` showed `SetupEntityVisibility`
(0x554230) beginning with `E9 3B BE 6D 1C`, whose target is
0x1CC30070. This is a foreign entry-point patch: ownership cannot be
determined from the jump address alone. The optional `RenderOneNonRoad`
probe was also unavailable, so its zero-count data are **not** GPU draw
evidence. Core extended world and LOD scanning remained active.

The latest CSV reported about 782 MiB free virtual address space and
streaming usage very close to its effective budget. The shipped profile
still enabled speculative `AssetPreload.Enabled=1` and
`PreloadAllAssets=1`; these are not required for the extended renderer
to request objects it discovers.

## Changes

- Added `asc::hook_entry::Inspect`, an allocation-free x86 entry-byte
  classifier for relative JMP, short JMP, FF 25 indirect JMP,
  PUSH/RET, MOV EAX/JMP, and traps/returns. Both core and optional
  hook verification now reject those pre-patched entry forms.
- Enhanced rejection reports with target region protection, MEM_IMAGE
  versus MEM_PRIVATE status, allocation base, and containing module when
  Windows resolves it. MEM_PRIVATE is explicitly marked
  `owning mod unknown` instead of guessing.
- Retained and now explicitly logs the existing
  `AddEntityToRenderList` distance-correction fallback when
  `SetupEntityVisibility` is unavailable. The optional render-pass
  entry is also diagnosed when it cannot be instrumented.
- Changed the shipped `config/DistantStreaming.ini` performance profile
  to `AssetPreload.Enabled=0` and `PreloadAllAssets=0`. **This does not
  disable `ExtendedWorldScan` or `ExtendLodGrid`.** Users with a custom
  INI should copy these two settings manually to test the reduced
  speculative workload.
- If the preloader is explicitly enabled, its pressure policy begins
  a small, periodic release of *ASC-owned* speculative KEEP_IN_MEMORY
  pins at `EmergencyFreeVaMiB + RecoverySlackMiB` (default 1280 +
  512 = 1792 MiB). It never requests a synchronous model unload,
  clears a mission/game/scene flag, or evicts a model GTA still needs.
  The engine controls when released assets are actually evicted.
- New preloader diagnostics: `earlyRecoverySlackMiB`,
  `recoveryBatches`, `recoveryPinsReleased`, and
  `protectedPinDeferrals`.

## Configuration

```ini
[AssetPreload]
Enabled=0
PreloadAllAssets=0
; Only applicable if Enabled=1:
RecoverySlackMiB=512
```

Keep existing draw-distance and LOD settings.

## Validation and limitations

35/35 portable Debug CTest tests passed, including a new
foreign-entry-detour unit test and early recovery policy assertions.
This environment cannot build the HOODLUM Win32 ASI or validate a
particular mod's trampoline in GTA. No FPS uplift is claimed.

This is conservative interoperability, not generic MinHook stacking:
another mod's hook at 0x554230 is *not* chained into or overwritten.
The private allocation address in the log still cannot identify its
owner. Because the existing add-entity fallback is active, the visible
draw-distance fix should be retained; this still needs a live test.

The preloader previously reported only ~1 speculative pin during
sustained memory emergencies. Disabling it may therefore save only a
small fraction of the observed 32-bit VA growth: native models,
textures, RenderWare objects, and other mods may dominate.
