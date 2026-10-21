# HD texture replacement — opt-in native D3D9 MVP

## Overview

`DistantStreaming.asi` now includes a conservative **optional** texture substitution
pipeline in the existing project. No second model-streaming controller, duplicate
MinHook initialization, raster-pointer rewriting, or game-memory-pool modification
has been introduced.

This is **source code**; native MSVC builds and GTA gameplay have NOT been
verified here. Portable tests passing does not mean the mod works in game.
The project **does not guarantee 8 GiB of dedicated GPU VRAM residency**.

## Components

1. `src/service64/Main.cpp`: 64-bit DDS catalog and data server. It validates
   DXT1/3/5 DDS files, rejects invalid and duplicate names, pages large
   `List` catalogs by index and serves `Get` named-pipe requests. The pipe
   is `\\\\.\\pipe\\DistantStreaming.HD.v2` and uses protocol version 2;
   do not mix an older x64 service with this ASI. The service prepares *bytes*, not GPU
   textures. The maximum compressed per-texture transfer is **64 MiB**.
2. `src/game/HdTextureClient.*`: 32-bit asynchronous named-pipe client.
   Network/pipe transfers happen in its worker, never in D3D hooks. A bounded
   queue carries validated DDS blocks to the render thread.
3. `src/game/HdTextureRenderer.*`: scans up to 64 live TXD slots per frame
   when a sweep is active. It obtains source texture names and borrowed D3D9
   texture pointers from GTA SA 1.0 RenderWare rasters, intercepts D3D9
   `SetTexture`, uploads DXT resources, and substitutes GPU texture *bindings*
   without modifying raster ownership.
4. The original game's TXD loading/unloading and the existing DistantStreaming
   scheduler determine which textures are live. Those names are the
   dynamic-prefetch signal. This is **not** a separate world-distance planner.

## Required build environment

- Windows x64, MSVC, Windows SDK, CMake.
- GTA SA **PC 1.0 US** with working ASI loader and compatible Plugin-SDK.
- Run the existing `build.bat` (Visual Studio 2026) to build **both** the
  Win32 ASI and x64 service, run CTest in both build trees, and copy
  `DistantStreaming.asi`, `DistantStreaming.ini` and
  `ASC_HDService64.exe` into `dist/`.
- Alternatively, build the 64-bit service in a second tree, for example:

  ```powershell
  cmake -S . -B build-x64 -G "Visual Studio 18 2026" -A x64 -DASC_BUILD_ASI=OFF
  cmake --build build-x64 --config Release --target ASC_HDService64
  ```

- In the game’s `DistantStreaming.ini`, enable `[HDTextures] Enabled=1`.
  **Disabled by default.** The service must be started separately before GTA SA.

## DDS input preparation

Create a folder, for example `C:\Games\GTASA\HDTextures\`, containing DDS files:
`grass.dds`, `road.dds`, etc., where the filename stem matches the
**RenderWare texture name** in a loaded TXD (not the TXD filename).
Names may contain only ASCII letters, digits, underscore and dash. Names are
case-folded to lowercase. DDS formats are restricted to BC1/BC2/BC3
(legacy DXT1/DXT3/DXT5), with a normal uncompressed 128-byte DDS header,
2D textures and valid mip layouts. One file per normalized name.

Run the service in a terminal:

```powershell
.\ASC_HDService64.exe "C:\Games\GTASA\HDTextures"
```

Run GTA SA 1.0 with the compiled `DistantStreaming.asi` and
`DistantStreaming.ini` in the usual ASI deployment location.

Example config:

```ini
[HDTextures]
Enabled=1
ServiceProbe=0
ProbeIntervalMs=5000
TextureCacheTargetMiB=6144
ScanIntervalMs=300
MaxUploadsPerFrame=1
UploadBudgetMs=3
```

**RAM warning:** DXT mip blobs are buffered temporarily in the game process.
The 64 MiB per-texture limit still makes large HD textures costly inside a
32-bit VA process. A 4096x4096 DXT5 mip chain is ~21.33 MiB; an 8192x8192 DXT5
chain (~85 MiB) exceeds the MVP IPC cap and will NOT load.

## Lifetime, safety, and other ASIs

- The D3D9 hook is only installed after the project's GTA executable and
  original model-streaming hooks have passed runtime checks.
- Uses the same initialized MinHook library; shutdown order:
  D3D9 hooks/resources, IPC worker, original model hooks.
- Refuses already-detoured or non-image D3D9 function entry points instead of
  intentionally chaining an unknown third-party graphics hook.
- Binding substitution references textures; original RW rasters and texture
  dictionaries are never modified. The source-to-name map refreshes each sweep.
- Replacement textures use `D3DPOOL_DEFAULT`; intermediate
  `D3DPOOL_SYSTEMMEM` staging textures are released after `UpdateTexture`.
- On device Reset, all replacements are unbound and released; reloaded after
  reset. A failed upload leaves vanilla texture bindings unchanged.
- A fixed cache target tracks *compressed D3D texture allocation estimates*,
  not actual GPU physical residency. D3D9 and WDDM may page resources.
- An inactive TXD frees its replacement on pressure; live TXD entries with
  no recent binds become eviction candidates after 5 seconds. No vanilla
  raster is freed by this renderer.
- The worker's prepared-block queue is bounded to 128 MiB; only one 64 MiB
  asset transfer is active. A soft frame-time budget avoids starting another
  upload after the limit (a single D3D9 call may still exceed it).
- Service responses validate DDS headers **again** when served, so a file
  replaced after startup is not trusted using stale metadata.
- Eviction only selects entries not in the currently scanned live texture map;
  it does NOT free vanilla raster resources.
- The x64 service uses a local blocking named pipe and default user security.
  Run only with trusted DDS files and a trusted local account.
- Invalid/missing or unsupported assets fall back to the vanilla texture.

## Limitations and verification required

1. The RenderWare raster GPU-pointer offset (`RwRaster + 0x34`) is based on
   documented GTA SA 1.0 RW/D3D9 structures. It **must** be verified against
   your executable build with debugger and captured resource pointers.
2. Some third-party ASIs/proxies may already detour D3D9. The new layer
   refuses obvious existing detours, but compatibility cannot be guaranteed
   without a real Windows load-order test.
3. Driver behaviour may differ: compressed `D3DPOOL_DEFAULT` creation and
   `UpdateTexture` are tested only by a real D3D9 Windows runtime.
4. Uploads are limited by count and elapsed CPU wall-time; the engine cannot
   preempt a single blocking D3D9 upload, and GPU stalls are not fully measured.
5. Live name matching uses texture names across TXDs. If two TXDs contain
   the same name, both use the same DDS replacement. TXD-qualified selection
   is not in this version.
6. No automatic x64 service launch. Start `ASC_HDService64.exe` yourself.
7. No 64-bit GTA executable, no new GPU address space, no forced dedicated-VRAM
   residency, and no proven 8 GiB occupancy measurement.
8. On large datasets, the initial texture map may populate across multiple
   game frames due to the bounded sweep; the first visible frames may show
   vanilla textures before replacements finish loading.

## Validation

Portable checks performed in this environment:

```shell
cmake -S . -B build-linux -G Ninja -DASC_BUILD_ASI=OFF -DASC_BUILD_TESTS=ON
cmake --build build-linux
ctest --test-dir build-linux --output-on-failure
python3 tools/audit_separation.py
```

**11/11 portable tests passed** and the codebase separation audit passed.
Windows MSVC Win32 + x64 builds and actual GTA SA runtime have **not** been
executed in this environment.

After building on Windows, run the real x64 named-pipe protocol smoke test
(requires Python 3):

```powershell
py -3 tests\windows_hd_service_smoke.py dist\ASC_HDService64.exe
```

To observe WDDM process GPU accounting while the game is running:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\watch-hd-vram.ps1
```

WDDM counters provide allocation accounting, **not** a guarantee that 8 GiB
is physically resident at any moment.

### Before distributing as a functioning mod

1. Build both architectures with the exact Plugin-SDK revision used by the
   original DistantStreaming project.
2. Attach Visual Studio to `gta_sa.exe`; ensure hook targets and raster pointer
   offset resolve to valid COM objects.
3. Test one 512x512 DXT1 with an unmistakable replacement pattern, then
   a 2048x2048 and 4096x4096 DXT5 image.
4. Test reset (alt-tab/fullscreen changes) and game exit; inspect for errors
   and resource leaks in `DistantStreaming.log`.
5. Test with other installed ASIs; if HD replacement reports hook conflict,
   leave it disabled rather than forcing another detour.
6. Profile CPU private bytes, remaining virtual address space, GPU dedicated
   memory with external Windows graphics tools, and FPS/frametime.
7. Only raise `TextureCacheTargetMiB` after measuring real residency and
   verifying actual hardware VRAM capacity.

## Upstream reference material

- Microsoft D3D9 CreateTexture:
  https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-createtexture
- Microsoft D3D9 SetTexture:
  https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-settexture
- Plugin-SDK SA:
  https://github.com/DK22Pac/plugin-sdk
