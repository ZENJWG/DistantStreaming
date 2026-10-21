# Distant Streaming — name migration

The GTA SA streaming project previously distributed as **AdaptiveStreaming**
has been renamed to **Distant Streaming**.

| Previously | Now |
|---|---|
| `AdaptiveStreaming.asi` | `DistantStreaming.asi` |
| `AdaptiveStreaming.ini` | `DistantStreaming.ini` |
| `AdaptiveStreaming.log` | `DistantStreaming.log` |
| `AdaptiveStreaming.csv` | `DistantStreaming.csv` |
| `ASC_AssetCache64.exe` | `DistantStreaming_AssetCache64.exe` |
| `AdaptiveStreaming.pdb` / `.map` | `DistantStreaming.pdb` / `.map` |
| `AdaptiveStreamingSA` CMake project | `DistantStreamingSA` |
| `\\.\pipe\ASC.NativeMemory.<session>` | `\\.\pipe\DistantStreaming.NativeMemory.<session>` |

## Build

On Windows from this directory:

```bat
build.bat
```

Optional Windows integration tests:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\\verify-windows.ps1
```

`dist/` is populated by the PowerShell bootstrap and should contain the
renamed ASI, INI, and 64-bit helper. Do not install two different versions
of the streaming ASI at the same time.

## Upgrade

1. Back up your old installation, INI, and logs.
2. Stop GTA. Remove/disable **all** old `AdaptiveStreaming.asi` copies,
   including copies loaded from Mod Loader; avoid two active hook owners.
3. Compile the new bundle. Copy the renamed files into the desired ASI
   install directory, with the helper beside the ASI.
4. Merge your custom settings from `AdaptiveStreaming.ini` into the new
   `DistantStreaming.ini`. Do not keep both configurations assuming both
   will be loaded.
5. Start GTA and inspect `DistantStreaming.log`; confirm the x64 helper
   can connect and that hook compatibility checks pass.
6. Keep the GPU and model draw-distance controls at their previously
   tested values. Naming does not alter 32-bit memory constraints.

## Compatibility

- `ASC_BUILD_ASI`, `ASC_BUILD_TESTS` and legacy `asc` namespaces are
  kept to preserve external scripts and stable internal source references.
- `ASC_IsNativeResource` stays exported under its old name for
  SanFrameOptimizer integration.
- SanFrameOptimizer's module detection now recognizes both
  `DistantStreaming.asi` and the older `AdaptiveStreaming.asi`.
- The `DistantStreamingOwnsNativeBudget` INI key takes precedence over the
  legacy `AdaptiveStreamingOwnsNativeBudget` key if both exist.
- The helper/ASI pipe and shared-mapping names changed, so both binaries
  must come from this renamed build; mixing old and new is unsupported.
- Archived `.patch` and `.diff` files remain historical and are not
  renamed or rewritten because their original context is tied to old
  source filenames and symbols.
- SanFrameOptimizer remains a **separate** project and retains its own
  `SanFrameOptimizer.asi` name.

## Validation

Portable CMake/CTest verifies algorithm tests and the rebrand regression
checks. Windows MSVC compilation and in-game hook behavior cannot be
validated on non-Windows hosts.
