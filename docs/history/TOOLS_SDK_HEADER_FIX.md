# Plugin-SDK SA header paths (VS 2026)

The compilation log shows a completed SDK `Plugin.lib` build and MinHook install,
then missing `eWeaponType.h`, `eCrimeType.h`, and `eEntityType.h` when building
DistantStreaming. They live in `plugin_sa/game_sa/enums`. Plugin-SDK also
requires `plugin_sa/game_sa/rw`, plus the `RW` preprocessor definition for SA.

The project now applies the exact SDK include directories in its `CMakeLists.txt`.
This change does not alter the `.deps/plugin-sdk` checkout. It introduces no
functionality from LoadWholeMap.

## Applying

1. Replace the project's top-level `CMakeLists.txt`.
2. Run PowerShell: `$env:ASC_SKIP_UPGRADES = '1'; .\\build.bat`.
3. On a successful build check `dist/DistantStreaming.asi` and INI.
4. If CMake behaves unexpectedly because of a stale cached configuration,
   remove only `build-vs2026-win32` and run build.bat again; do not remove `.deps`.

## Validation limits

Portable code and unit tests were compiled in Linux. The SDK-target compilation,
MSVC linking and GTA SA gameplay have not been performed here.
