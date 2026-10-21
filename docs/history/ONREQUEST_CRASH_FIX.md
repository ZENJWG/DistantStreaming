# GTA SA 1.0 US — `readiness::Tracker::OnRequest+0x42` crash mitigation

## Evidence

User crash: `adaptivestreaming.asi+0x53212` inside `readiness::Tracker::OnRequest`,
exception `0xC0000005` while reading `0x90900DE2` (register ECX `0x9090059E`).
Backtrace follows `Runtime::Tick` -> `hooks::DrainVanillaRequests`
-> `RequestObservations::Drain` -> `Tracker::OnRequest`.

Previous memory-budget crash workaround did not resolve this: HD texture replacement,
native memory rewriting, camera catch-up, and cleanup were disabled at the time.

This evidence identifies the **faulting telemetry callback**, but does not
conclusively identify what produced the invalid address or whether other mods
have damaged shared engine structures. The preceding `CrashInfo.SA.asi`
`0xE06D7363` exception may be secondary to crash reporting.

## Specific changes

- `Tracker::OnRequest` now records only a bounded id and timestamp. It never
  dereferences `CModelInfo` or `CStreaming` game-owned pointers.
- Runtime's request drain no longer inspects the CModelInfo pointer array unless
  `DynamicReadiness` is explicitly enabled. This means enabling controller
  and/or ordinary predictive prefetch does **not** implicitly re-enable
  model-table reads on the request-observation path.
- `Diagnostics.ReadinessTelemetry=0` disables `Tracker::OnRequest`, `Sample`,
  and `TakeSummary` by default. All three are independently gated; telemetry
  readiness fields will be zero while disabled.
- When explicitly enabled, `Sample` reads load-state bytes from the live
  `CStreamingInfo::ms_pArrayBase` pointer with `ReadProcessMemory`, which
  returns failure instead of crashing on an unreadable source address.
- First initialized scheduler tick logs live/cache model/streaming pointers.
  An unreadable or mismatched pointer disables the entire game-controller
  branch for this process, including HD scanning.
- Existing hook isolation is preserved: HookMode 0/1/2 and ControllerEnabled
  are independent knobs. Original Request/Load hook logic is not duplicated.

## Why 0x9090xxxx is suspicious

Plugin-SDK's `CModelInfo::ms_modelInfoPtrs` is initialized from the
32-bit instruction operand at `0x40CD67`, and its
`CStreaming::ms_aInfoForModel` cache is initialized using the instruction
operand at `0x408B25` on SA 1.0 US. Mods that patch the underlying
instructions can invalidate such cached pointers (0x90 is an x86 NOP byte).
The crash address is consistent with this class of issue, **not proof** that
a specific mod caused it.

Plugin-SDK defines `CStreamingInfo::ms_pArrayBase` as a reference to the live
game pointer at `0x9654B4`, which is better suited to optional diagnostic
sampling. The project does not rewrite Plugin-SDK static pointers or
third-party mods automatically.

Sources: DK22Pac/plugin-sdk, `plugin_sa/game_sa/{CModelInfo.cpp,CStreaming.cpp,CStreamingInfo.cpp}`.

## Installation (clean diagnostic test)

1. Back up the game directory, previous ASI, and existing INI.
2. Build with `build.bat` on Windows (MSVC / Visual Studio 2026 with the
   project's required Plugin-SDK/MinHook dependencies).
3. Install **one** copy of `DistantStreaming.asi` and its matching
   `DistantStreaming.ini` beside the game or through your normal ASI loader.
   Do not leave a second copy in Mod Loader.
4. Check that `DistantStreaming.log` says
   `ReadinessTelemetry=0`. If an old log persists or lacks this line, the
   game is loading an old ASI/INI.
5. Start with:
   ```ini
   [General]
   Enabled=1

   [Diagnostics]
   HookMode=0
   ControllerEnabled=0
   ReadinessTelemetry=0

   [HDTextures]
   Enabled=0

   [DynamicReadiness]
   Enabled=0
   ```
   This is *passive isolation*, not functioning streaming enhancement.
6. After clean gameplay, test the Request detour only:
   ```ini
   [Diagnostics]
   HookMode=1
   ControllerEnabled=1
   ReadinessTelemetry=0
   ```
   Keep `Prefetch.Enabled=0` and HD disabled for the initial test.
   Look for `SDK diagnostics` and `SAFETY STOP` in the log. If a safety stop
   appears, do not force-enable the controller.
7. Only after that is stable, enable one feature at a time; keep
   `ReadinessTelemetry=0`, `DynamicReadiness.Enabled=0`, and `HookMode=1`
   during the first HD tests. Avoid `HookMode=2` until Request-only is stable.
8. A newly compiled ASI has different offsets from older logs. Include its
   exact PDB/MAP and current log when reporting further crashes.

## Validation status

- Portable Release build and CTest: 12/12 pass (11 algorithms plus a
  new source-regression check that prevents accidental restoration of this
  request-observation dereference).
- `tools/audit_separation.py`: pass.
- Windows/MSVC x86 ASI and in-game rendering: **not available/verified here**.
- An invalid SDK pointer may also affect game code outside this tracker. This
  patch **does not prove** that GTA or other plugins are free of corruption.
