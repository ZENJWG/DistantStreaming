# Contributing

Please open an issue describing the proposed change before large refactors.

## Development

- Use CMake and the supported Windows/MSVC toolchain for game-specific targets.
- Keep the Win32 ASI and x64 helper protocol compatible; update both when changing shared messages.
- Run applicable CTest targets and Python tests before opening a pull request.
- Include reproduction steps and performance measurements for streaming changes.
- Do not commit build outputs, game files, local dependencies, or telemetry logs.

## Pull requests

Describe the motivation, implementation, testing, compatibility risks, and rollback plan.
