DistantStreaming for GTA San Andreas
===================================
Streaming and asset caching for GTA San Andreas Classic PC 1.0 US.
Includes predictive model loading, memory management, Mod Loader caching
and diagnostics. An optional 64-bit helper caches file bytes outside GTA.
GTA itself remains 32-bit. 

INSTALLATION
------------
Requires a working ASI loader and 64-bit Windows for the helper.
Build the source or obtain matching binaries, then copy into the game's
ASI-loader directory (normally beside gta_sa.exe):

  DistantStreaming.asi
  DistantStreaming.ini
  DistantStreaming_AssetCache64.exe

Keep the helper beside the ASI; it starts automatically when enabled.
Always update both binaries together. Close the game before replacing files.
To uninstall, remove these files while the game is closed.

BUILDING
--------
Run build.bat from the project directory. Outputs appear in dist/.
Use build.bat fast for an existing setup or build.bat full for full setup.
The bootstrap targets Visual Studio 2026 C++ tools, Windows SDK, CMake 4.2+,
Git and PowerShell 5.1, and can install/update dependencies.
The ASI builds as Win32/x86; the helper builds as x64.

TROUBLESHOOTING
---------------
Check DistantStreaming.log beside the ASI for startup and helper errors.
If the helper is missing, confirm its EXE is beside the matching ASI.
For stutter or crashes, compare caching/controller settings one at a time
and reduce memory budgets if needed. Results depend on your mod setup.

See docs for technical details. Older notes may contain
superseded settings. Licensing records are in LICENSING.md,
THIRD_PARTY_NOTICES.md and licenses/.
