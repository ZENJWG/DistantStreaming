# Native helper handshake recovery (v5)

## Symptom
`Native memory: helper failed to become ready; using vanilla resources`
appears even when `ASC_HDService64.exe` briefly runs. The game's D3D9 path continues,
but persistent x64 CPU backing cannot be used.

## Source changes
- `src/game/NativeBacking.cpp`: logs helper launch, process exit and named-pipe
  handshake failure stage plus Windows error; accepts a delayed pipe for up
  to five seconds; closes just-launched unsuccessful child processes instead of
  leaving stranded instances; acknowledges any valid protocol reply.
- `src/game/main.cpp`: moves initial IPC startup to an asynchronous worker after
  the game D3D device is observed. Only D3D9 installation takes place on the
  game thread. Handshake failures retry every ten seconds instead of being
  permanently suppressed after the first failed attempt.
- `tests/native_helper_startup_tests.cpp`: a Windows integration test for an
  invalid executable path and two consecutive successful startup handshakes.

## Preserved
Native textures, mesh buffers, HD rendering, OLA bindings, native offload and
the v4 no-fixed-1024-MiB streaming budget remain enabled and unchanged.

## Build and deploy
1. Run `build.bat` from a Windows development environment.
2. Install the rebuilt ASI **and** matching `ASC_HDService64.exe` from `dist`
   alongside your `DistantStreaming.ini`. Do not mix versions.
3. Confirm game startup now logs:
   `Native memory: x64 helper PID=..., handshake=connected`.
4. Inspect `Native memory: device bridge ACTIVE` and the next
   `Native memory: helper=connected` telemetry line.

If the helper fails to connect, report the diagnostic fields:
`stage=...`, `Win32=...`, `process=...`, `exitCode=...`.
Exit code 2 means invalid helper arguments, 3 means helper could not open
its parent process and 4 means the helper could not create the named pipe.

## Validation
The portable Linux CMake/CTest suite passes (15/15). The Windows 32-bit
ASI, 64-bit helper and GPU tests must still be built and run on Windows.
The previous `native_d3d_resources` crash is not claimed fixed by this patch.

## Runtime behavior
When the connection fails, the game's original D3D9 resource implementation
continues running, and the ASI retries. Retries do not disable any configured
native-memory options or impose another fixed VRAM or streaming-memory cap.
