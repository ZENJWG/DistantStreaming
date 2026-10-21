# GTA SA 1.0 US access violation: 2026-10-16 triage

## Supplied evidence

- GTA executable reported as `1.0.0.0 US`.
- `0xC0000005` invalid read of `0x90955E0B`, executing
  `adaptivestreaming.asi +0x1F794`.
- Call stack enters GTA executable from the game/Mod Loader. Module offset is
  a relative virtual address (RVA) within the ASI, not a source line number.
- Logged `HD asset client: enabled`, `HD D3D9 replacement: ON (experimental)`,
  and an estimated HD cache target of 6144 MiB. **A cache target is not VRAM
  residency** and the log does not prove any replacement texture was uploaded.
- Just before the fault, `StreamingMemory::Update` logged an effective native
  streaming budget of 3577 MiB. There is no evidence in the log that this was
  the direct instruction raising the exception. The last completed log entry
  only narrows execution to the subsequent scheduler/game work.
- Both mandatory Request/Load hooks were installed after signature-gated
  HOODLUM target recovery. `modloader.asi` appears on the call stack; its
  presence alone does not prove it is responsible.

## Source changes

- **Historical 2026-10-16 advice, superseded by v4:** a 1024 MiB fixed cap
  was used during crash triage. v4 removes it and retains VA/uint32 bounds.
- `config/DistantStreaming.ini` and `src/game/ConfigWin.cpp`: safe baseline
  with memory management, HD replacement, prediction, camera catch-up and
  offscreen cleanup OFF. Native cap is 512 MiB if deliberately re-enabled.
- `src/game/WorldListSafety.h`, `GameAdapter.cpp`, and `CameraReadinessWin.cpp`:
  reject obviously nonreadable linked-list nodes/entities and invalid camera
  scan cells. These checks are best-effort, not use-after-free protection.
- `src/game/main.cpp`: first three completed scheduler/render callback
  traces and rejected-pointer counters.
- CMake/build script: produce `DistantStreaming.pdb` and
  `DistantStreaming.map` alongside the ASI, if the toolchain emits them.

## Isolate on Windows

1. Back up the **entire game folder and current INI**.
2. For a quick test **before rebuilding**, edit the installed old INI:

   ```ini
   [Memory]
   ManageStreamingMemory=0
   StreamingMemoryLimitMiB=512

   [HDTextures]
   Enabled=0
   ServiceProbe=0

   [Prefetch]
   Enabled=0

   [CameraReadiness]
   Enabled=0

   [OffscreenCleanup]
   Enabled=0
   ```

   Close/restart GTA after each INI change. Do not change unrelated ASIs
   until this baseline is tested. If the game still crashes, temporarily
   remove only `adaptivestreaming.asi` (rename to `.asi.disabled`) and
   retest to confirm association. Do not remove `modloader.asi` simply
   because it appears on the backtrace.
3. Build this revised source using `build.bat`; install the freshly built
   `dist/DistantStreaming.asi` **and** matching `dist/DistantStreaming.ini`.
   Do not mix the old binary and the new config while attributing results.
4. Verify the log prints `DistantStreaming CrashFix build 2026-10-16`.
   The first game ticks should show `TRACE scheduler` phase breadcrumbs.
   If it crashes, the last trace checkpoint helps distinguish camera,
   scheduler, renderer and earlier native game hooks.
5. Enable a single feature at a time: first prediction, then camera
   readiness, then cleanup, then HD textures. Confirm the game survives
   gameplay, vehicle driving, map transitions and alt-tab before advancing.
   If native memory management is needed, test 512 MiB, then at most
   1024 MiB (historical test guidance). Higher native budgets now require
   runtime stress-testing because available VA is not a stability guarantee.
6. If a new crash occurs, attach the matching `DistantStreaming.pdb`
   or `DistantStreaming.map` (keep PDB private if desired), exact
   `DistantStreaming.log`, and crash backtrace with module offset.

## Debugging `+0x1F794` precisely

With the **matching original crash build** and its PDB, in Visual Studio or
WinDbg resolve image base +0x1F794 to a source line. A rebuilt binary has
different function layouts, so mapping its `+0x1F794` to the original fault
would be misleading. The same applies to a `.map` from another revision.

No Windows Win32 MSVC build or in-game test was possible in the present
Linux environment. The package is **source only**, not a certified binary fix.
