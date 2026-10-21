# Verification record — DistantStreaming Advanced

| Check | Result |
| --- | --- |
| Portable C++ core compilation | PASS (Linux GCC 14.2, C++17) |
| CTest core logic | PASS |
| Four-producer lock-free hook ring stress | PASS |
| Portable ASan + UBSan | PASS (Linux, libasan runtime preloaded) |
| Mock SDK syntax check of GameAdapter.cpp | PASS (not equivalent to real Plugin-SDK) |
| Source separation audit | PASS (no prohibited loader/evict/budget writes) |
| CMake generator docs | VERIFIED: VS 18 2026 requires CMake 4.2+ |
| `GameAdapter` / Win32 compilation | NOT VERIFIED against full Plugin-SDK |
| MSVC VS 2026 32-bit `.asi` compilation | NOT RUN |
| MinHook linking / trampoline operation | NOT RUN |
| Vanilla GTA SA PC 1.0 US game runtime | NOT RUN |
| Measured FPS / latency improvements | NOT RUN |

Validation does **not** mean performance improved or in-game stability
is proven. The project intentionally does not ship a compiled ASI.

Known engineering boundaries:
- CRC32 configuration removed; automatic PE/address checks are basic safeguards, not version authentication.
- Hook addresses are version-specific. Do not use altered/Steam executables.
- Plugin-SDK checkout may have source-level / library ABI differences.
- Data collection from ordinary world sectors is not the full renderer.
- Some timed-model, draw-distance and view filters are approximate.
- Readiness statistics observe models, not spawned entities.
- Optional `GetNextFileOnCd` timing adds overhead when enabled.
- Buffered CSV worker requires normal SDK shutdown event for final flush.

## 2026-10-16 HD source integration
- Added `HdTextureRenderer`, asynchronous `HdTextureClient`, and shared DDS
  layout checks. Both are part of the existing single ASI target.
- Corrected invalid character escapes in the previously supplied x64 server.
- Linux portable build: OK. `ctest`: **10 / 10 passed**.
- Separation audit: PASS (no second model streaming controller).
- Windows MSVC x86 ASI compilation: **NOT RUN** (Windows/Plugin-SDK unavailable).
- Windows MSVC x64 DDS service compilation: **NOT RUN**.
- Live GTA SA 1.0 / GPU residency / game crash regression: **NOT RUN**.
