# Distant Streaming v18 — MSVC DrawDistanceWin.h compilation fix

The 2026-10-17 v18 archive accidentally repeated two method declarations
and three groups of camera telemetry fields inside `draw_distance::Manager`.

Symptoms:
- C2535: `draw_distance::Manager::ApplyCamera` already declared
- C2535: `draw_distance::Manager::ObserveCamera` already declared
- C2086: `lastCameraReport_`, `cameraApplies_`, `cameraSkipped_`,
  `cameraMismatches_`, `lastCameraTarget_`,
  `lastObservedFarClip_`, and `lastObservedFog_` redefined.

Changes:
- Kept one declaration of ApplyCamera and ObserveCamera.
- Kept one copy of each camera telemetry member.
- No .cpp changes, no memory hook changes, no changes to INI defaults.

Validation in Linux environment:
- g++ -std=c++23 -fsyntax-only test including DrawDistanceWin.h: PASS.
- CMake portable build: PASS.
- CTest: 27 / 27 PASS.
- Visual Studio Win32 build: NOT RUN here; please rerun build.bat on Windows.

Install hotfix:
Extract the hotfix ZIP into the project root, overwriting
`src/game/DrawDistanceWin.h`.
Then run `./build.bat` from PowerShell.

The full SOURCE ZIP includes this fix and can be extracted to a new folder.
Avoid installing two different DistantStreaming.asi binaries simultaneously.
