# Source review and optimization — 2026-10-20

Follow-up: [EXE inspection and gta-reversed comparison](EXE_AND_REVERSED_REVIEW_2026-10-20.md).
That pass addresses the full VA-walk spike described below and makes additional
runtime changes. Its Release build was compiled without running tests; the test
counts in this document apply to this earlier pass.

All changes and build outputs are confined to `I:\ag32gf3g\distantstreaming`.
Original changed files, removed renderer files and previous distribution
binaries are preserved under `.review-backups/2026-10-20-optimization/`.

## Project scope

The CMake build graph is the authority for what runs:

| Area | Review focus |
| --- | --- |
| `src/game` | Scheduler cadence, predictive sector traversal, camera catch-up, native budget ownership, cleanup, GPU observations, hooks and asynchronous cache transport |
| `src/core`, `include/asc` | Request admission, pressure/backlog guards, catalog parsing/persistence, immutable archive pages and bounded policy algorithms |
| `src/service64` | File-handle identity, overlapped reads, concurrent page flights, publication limits, warming and IPC |
| `tests`, `tools`, `config`, CMake | Release assertions, Windows tests, helper dependency tracking, local tool discovery and profile consistency |
| Other source/history | Root `src/*.cpp`, `include/mws`, and `src/fxp` contain historical optimizer/renderer code outside the active targets. Old patches, ZIPs and engineering notes are historical, not the current release specification. |

This is a source/build review, not an exhaustive proof that every module is
bug-free. Runtime hooks and mod compatibility still need GTA testing.

## Implemented fixes

1. **Windows catalog snapshot replacement.** `LoadOrBuild` previously called
   its rebuild path while its rejected snapshot's `ifstream` was still open.
   Windows could then reject both replacement and deletion of the old file.
   The existing persistence test reproduced this after a same-size IDE edit.
   Close the reader before rebuilding so later starts can reuse the repaired
   snapshot instead of repeatedly parsing the world. Also close the test's
   own reader before its corruption/replacement scenario.

2. **Fewer OS calls during supplemental scans.** Predictive collection and
   the first, read-only camera scan now use the existing bounded positive
   region cache. Each invocation starts with a fresh cache. It expires before
   native requests, blocking loading or `CreateRwObject`; the instancing scan
   continues to query memory directly. Entity eligibility, traversal quotas,
   scoring and native visibility are unchanged. A real Windows memory test
   verifies that 4,096 checks inside one region require one OS query, then
   checks alignment, region bounds, nested scopes and protection changes
   between scans. This is a syscall-count result, not an FPS benchmark.

3. **Camera dependency request pacing.** A batch formerly checked backlog
   only before scanning, then could issue new DFF/TXD requests after its
   dependencies had filled the queue. Each supplemental request now rechecks
   the live queue. Promotions of existing requests remain possible, and
   explicitly configured blocking catch-up retains its behavior. Shared
   textures are requested once per batch; loaded or already-priority TXDs
   need no repeated promotion. A native call can recursively enqueue its own
   dependencies, so the check does not impose a strict global queue ceiling.

4. **Correct helper rebuild detection.** The fast-build fingerprint followed
   quoted headers from `service64` but missed implementation changes in the
   linked `asc_core` library. Include core sources and their quoted headers.
   Regression coverage confirms that a core implementation change invalidates
   the fingerprint while an ASI-only camera change does not. All core sources
   are included conservatively, including static-library members that may not
   ultimately be pulled into this executable.

5. **Use installed CMake.** Fast builds now discover Visual Studio's bundled
   CMake through `vswhere`, avoiding a needless full bootstrap when standalone
   CMake is missing from PATH. This was exercised on the actual machine.

6. **Remove dead renderer remnants.** Archive the excluded `DrawDistanceWin`
   implementation/header, its policy header and its unbuilt test. Remove
   ignored `[DrawDistance*]` sections from the GLOBAL5, STUTTER_STABLE_5X and
   v20_VISUAL_TEST profiles. Their historical filenames remain for existing
   references. The primary INI is unchanged. These removals improve source and
   configuration consistency; excluded code never contributed runtime cost.

7. **Repair outdated config assertions.** The config test expected a 32 GiB
   default helper budget although the current parser/header use 8 GiB. It also
   expected disabling cleanup's blocking override to disable frame stability's
   separate override. Test the current defaults and both override states.

## Validation and release files

- MSVC Release Win32 `DistantStreaming` and x64
  `DistantStreaming_AssetCache64` builds succeeded.
- Release CTest: **42/42 passed on x64**, **41/41 passed on Win32**.
  The extra x64 suite exercises real concurrent helper-cache file I/O.
- New Windows memory-validation and PowerShell dependency-tracking tests are
  registered in CMake. Existing assertion-based tests retain `/UNDEBUG`.
- Python is unavailable, so the four optional Python checks were not
  registered or run. A separate PowerShell check verified the renderer-symbol
  and supplied-profile constraints corresponding to `test_no_drawdistance.py`.
- `fast-build.ps1` succeeded and populated `dist`. Its second invocation
  correctly reported `Cache64 unchanged: x64 compile/link SKIPPED`.
- Packaged PE machine types are x86 (`014C`) for the ASI and x64 (`8664`) for
  the helper. ASI symbols are included; the primary/distribution INI contents
  match their originals. No game installation was modified.
- Existing Plugin-SDK warnings and the `/GL` library's linker `/LTCG` restart
  notice remain; neither prevented the Release build.

Logs: `build-review-x64/ctest.log`, `build-review-win32/ctest.log`,
`build-review-x64/fast-build.log`,
`build-review-x64/fast-build-unchanged.log`, and
`build-vs2026-win32/optimization-build.log`.

## Remaining performance work requiring measurements

- Compare the same saved-game route/camera with the previous and new ASI,
  using identical game settings and mods. Examine window P95/P99 frame times,
  camera/controller CPU times, native backlog, free VA and cache hit/copy cost.
  Check turns, dense sectors, teleports and save reloads for missing assets.
  `tools/analyze_telemetry.py` summarizes the CSV once Python is available.
- `StreamingMemory::Update` still performs a full VA-region walk approximately
  every ten seconds. Measure its contribution to frame spikes before moving
  or incrementally scheduling that walk; memory-pressure detection must remain
  conservative.
- Cache misses still schedule an extra source read in the helper. Use the
  existing hook/copy/queued/fetched-byte metrics to determine whether low-reuse
  sources justify interception. File identity must remain validated on reads;
  caching HANDLE metadata without reuse/mutation handling is unsafe.
- Catalog prefetch and local predictive/camera requests have separate pacing.
  If they compete in actual gameplay, consider a shared speculative admission
  budget based on measured native backlog and frame cost.
- Other legacy optimizer/FXP code and historical documentation can be moved
  into an archive in a later repository cleanup. They are not a current FPS
  cost because the active targets do not compile them.

GTA was not launched. Stable high FPS and changes to gameplay frame times have
not been established by these builds, unit tests or syscall-count checks.
