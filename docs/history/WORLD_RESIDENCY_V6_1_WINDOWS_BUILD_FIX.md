# DistantStreaming World Residency V6.1 — Windows build repairs

This is a source-only patch on top of V6; it addresses the compilation
errors in the supplied Visual Studio 2026 Win32 build log.

## Fixed build failures

- `src/game/main.cpp:605`: `std::max(0, unsigned)` was invalid because of
  mixed signed and unsigned arguments. Native request backlog is already
  unsigned, so it is converted directly to `uint32_t`.
- `src/game/main.cpp`: declared `catalogPrefetchIssued`,
  `catalogIdentityRejected`, and `catalogPressureSkipped` in `Runtime` and
  wired them to the guarded native-prefetch loop.
- `src/game/AsyncAssetCacheWin.cpp`: restored atomic counters used by V6 IMG
  member diagnostics (`imgExtentMatched`, `imgExtentRejected`, and
  `imgExtentBytes`), and the WorldCatalog64 inventory counters
  (`catalogBinarySources`, `catalogBinaryPlacements`, `catalogImgArchives`,
  `catalogAmbiguous`). Catalog status updates now populate all four.
- Also corrected misleading catalog-read-only wording, telemetry newlines,
  and the warning from defining `WIN32_LEAN_AND_MEAN` twice.

## Validation

- On Linux, CMake Release build succeeded and 42 of 42 CTest tests passed.
- The reported missing identifiers are now declared and referenced with
  consistent types.
- **No Windows MSVC build or GTA runtime validation was performed here.**

## Windows validation

1. Save your current binaries and configuration before replacing sources.
2. Apply `DistantStreaming_WorldResidency_V6_1_MSVC_Fix.patch` to an
   **unchanged V6 checkout**, or extract the complete fixed source ZIP.
3. From a Visual Studio Developer PowerShell, run `build.bat full` once.
   This is particularly important after prior Cache64 protocol changes:
   incremental builds can skip an unchanged x64 target.
4. Confirm both the Win32 `DistantStreaming` target and
   x64 `DistantStreaming_AssetCache64` target build successfully.
5. Keep experimental native prefetch and IMG-member warming disabled
   until an in-game smoke test confirms protocol v10 connection,
   catalog status, memory headroom, and model-load behavior.

No changes to Project2DFX-owned draw-distance or rendering behavior.
