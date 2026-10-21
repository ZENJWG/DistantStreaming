# Local validation — 2026-10-10

Target: DistantStreaming source, GTA SA 1.0 US, VS2026 Win32.
Environment available: Linux GNU C++ 14.2 / CMake 3.31.6.

## Executed successfully

- `cmake -S . -B _build_host -DASC_BUILD_ASI=OFF -DASC_BUILD_TESTS=ON`
- `cmake --build _build_host`
- `ctest --test-dir _build_host --output-on-failure` — 4/4 passed
- Core admission and teleport test — passed
- 4-producer request-ring stress — passed
- Archive sampling boundary and histogram tests — passed
- Dynamic readiness learning, quotas, cooldown, location and pressure — passed
- `-fsanitize=address,undefined` portable build — passed
- Sanitizer CTest 4/4 passed with libasan preloaded (container-specific runtime)
- `python3 -S tools/audit_separation.py` — passed

## Not executed

- Visual Studio 2026 x86 game-target compilation/linking (Windows toolchain unavailable)
- Original GTA SA 1.0 US runtime/hook installation and compatibility
- Real archive-selection timing, frame-time impact, or vehicle/ped spawn behavior

The archive profiling hook now uses the current Plugin-SDK `int(int,bool)`
function signature and returns the original result. Tests cover the pure
sampling policy, but **cannot test the inline x86 detour on Linux**.

No third-party source or binary dependencies are bundled in this ZIP.
`build.bat` downloads dependencies according to the bootstrap policy.
