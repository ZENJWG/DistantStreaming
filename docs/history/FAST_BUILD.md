# Fast Windows builds (x64 Cache64 helper + Win32 ASI)

Run `build.bat` as usual. **After one successful full setup**, the default
path is now incremental and **does not compile or link**
`DistantStreaming_AssetCache64.exe` again unless its inputs changed.
Changes to the game-side streaming, caching or memory modules rebuild the Win32 ASI
but reuse the existing independent x64 cache executable.

## Commands

| Command | Action |
|---|---|
| `build.bat` | Quick incremental build if configured; otherwise first-time bootstrap |
| `build.bat fast` | Require quick mode; never check/install/update Git, SDK, vcpkg, or Visual Studio |
| `build.bat cache64` | Force rebuilding the Cache64 target, then build the ASI |
| `build.bat full` | Run full dependency setup, Plugin-SDK build, both target builds |
| `build.bat "C:\plugin-sdk" "C:\plugin-sdk\output\lib\Plugin.lib" "C:\vcpkg"` | Full build using supplied dependency paths |

## What is skipped

The fast path skips **all** repeated Git/winget/vcpkg/Plugin-SDK bootstrap
work. For the x64 helper, it calculates a SHA-256 fingerprint of its three
translation units, all other headers in `src/service64/`, their recursively
included project-local headers, the root `CMakeLists.txt`, and the x64
`CMakeCache.txt`. When unchanged and a successfully stamped executable exists,
the x64 CMake/MSBuild invocation is not started at all.

If helper code, its protocol/header dependencies, or x64 build settings change,
only the `DistantStreaming_AssetCache64` target is rebuilt. The CMake
target uses MSVC `/MP` to parallelize compiler processes for a fresh build.
Win32 ASI builds always use MSBuild's incremental dependency checking.

The stamp is `build-vs2026-x64/cache64-inputs.sha256`, recorded **only after**
a successful helper build; never manually copy a stamp to an unrelated build.
If the stamp or exe is missing, the helper is built. The `dist` outputs and
symbols are synchronized only when their content changes.

## Expectations / constraints

- A **clean** or changed-helper build still takes compile/link time; "instant"
  means *no unnecessary helper build* for ASI-only edits.
- Fast mode requires the original `build-vs2026-win32` and
  `build-vs2026-x64` configurations and working Windows MSVC tools.
- To update managed dependencies and toolchains, use `build.bat full`.
- Changing cache protocol structures in `include/asc/NativeMemoryProtocol.h`
  invalidates the helper fingerprint (so ASI/helper protocol versions match).
- Never copy an old helper from a *different* checkout/build; on uncertainty
  use `build.bat cache64` or `build.bat full`.
- Do not remove either build directory between runs: doing so loses incremental
  compiler state.
- Windows compilation was not available in this environment; the PowerShell
  scripts have not been executed under Visual Studio and require validation there.
