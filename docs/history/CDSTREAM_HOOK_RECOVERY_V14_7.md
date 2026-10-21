# DistantStreaming v14.7 — CdStream hook protection recovery

## Observed in user's v14.6 runtime log

- `AssetCache64: CdStreamRead hook creation failed: 7`
- MinHook `MH_ERROR_NOT_EXECUTABLE` = 7, *not* a memory-allocation or
  `MH_ERROR_UNSUPPORTED_FUNCTION` error.
- `readMode=no-hook`, `cdCalls=0`, `cdHits=0`, `bytesServed=0`.
- The helper connected and cached approximately 1131 MiB of source data.
- No models were force-preloaded; the 32-bit process's virtual-address-space
  guard remained active around 739–1604 MiB free.

## Change

The `CdStreamRead` hook installer logs the target's page status, then
checks for a committed `MEM_IMAGE` page belonging to an executable PE
section of the host GTA executable.

- If the page is already executable, normal v14.6 installation continues.
- If it is exactly `PAGE_READWRITE`, recovery is considered **only** when
  the first five bytes match the documented GTA SA HOODLUM trampoline
  `E9 9B 58 16 01` from 0x00406A20 to 0x0156C2C0, and the trampoline's
  destination is an executable page of the same host executable.
- When verified, it sets the entry's page to `PAGE_EXECUTE_READ` before
  MinHook creation. It is intentionally not restored to non-executable
  `PAGE_READWRITE` on shutdown.
- Other bytes, foreign trampolines, other image mappings, or non-executable
  destinations are **not** modified. GTA's original streaming loader
  continues unchanged.
- Hook errors now use `MH_StatusToString` plus numeric status.

Address/reference: GTA-reversed `CdStreamRead` at 0x406A20, and
SAEncyclopedia's HOODLUM executable relocation to 0x0156C2C0.
Runtime verification of the user's actual executable is still required.

## Scope

Only `src/game/AsyncAssetCacheWin.cpp` changes from v14.6, besides this
document and patch diff. Existing build.bat and INI are untouched. This is
a Win32/MSVC game-hook change; Linux portable tests do not compile that file.

## Verification

- Portable GNU C++ CMake build passed.
- `ctest`: 20/20 passed, covering existing core/cache policy.
- No Windows MSVC game build or GTA in-game validation performed.

## How to interpret the next log

1. A line like `CdStreamRead target ... protect=0x4` followed by
   `verified HOODLUM entry recovered RW -> RX` shows the recovery path.
2. `CdStreamRead hook active` and then `cdCalls>0` show requests reached
   the native boundary.
3. `cdHits>0` and `bytesServed>0` are required to demonstrate actual
   cache service.
4. If the entry is unfamiliar or still non-executable, **do not** disable
   the protection check. The installed GTA executable/hook layout must
   be fingerprinted and analyzed before building an alternate hook.

The x64 helper provides external CPU asset backing only; it cannot
extend the addressable memory inside the 32-bit gta_sa.exe process.
