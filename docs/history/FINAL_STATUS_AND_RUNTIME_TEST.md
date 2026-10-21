# HD D3D9 integration — deliverable and verification gates

## Implemented in this source package

- Retained existing single ASC scheduler/MinHook owner and the original
  plugin's model-streaming hooks.
- 32-bit D3D9 SetTexture substitution of matching RenderWare TXD texture
  names, opt-in and limited to known GTA SA 1.0 US globals.
- 64-bit named-pipe DDS service, lowercase ASCII names, paginated asset
  catalog, legacy BC1/BC2/BC3 header/size validation and data revalidation
  before serving files.
- 32-bit bounded asynchronous asset transport (<=64 MiB per DDS;
  <=128 MiB total prepared queue), upload time/count throttling and
  DEFAULT-pool texture storage.
- Estimated texture-byte budget, bind-age eviction, retry cooldowns,
  D3D9 Reset handling, and refusal of obvious competing D3D9 detours.
- `build.bat` now builds Win32 ASI + x64 service and packages both files.

## Not verified / not guaranteed

- No Windows toolchain or GTA SA game binary was available in this runtime,
  therefore this package is SOURCE, not a compiled/field-tested mod.
- 8 GiB is a **configurable estimated texture-cache budget**, not verified
  physically resident GPU VRAM. D3D9/WDDM determine actual residency.
- The live D3D9 raster handle at RwRaster+0x34 and event timing must be
  verified on the actual executable. The existing core hook signatures
  target the project's validated **HOODLUM 1.0 US** setup; a different
  release may refuse to install.
- Only DDS 2D DXT1/3/5 textures up to 64 MiB including mip chain.
  A full 8192x8192 BC3 chain (~85 MiB) is intentionally rejected.
- Global texture-name replacement, not TXD-qualified, and no independent
  distance-based world planner. Live TXD load/unload provides coarse dynamic
  demand; binding recency informs eviction.

## Windows operator validation

1. On Windows 64-bit with VS 2026 and GTA SA 1.0 US, run `build.bat`.
   Confirm both `dist/DistantStreaming.asi` and
   `dist/ASC_HDService64.exe` exist; both Win32 and x64 CTest suites
   must pass. Keep a backup of the existing ASI and config.
2. If Python 3 is installed, run:
   `py -3 tests\\windows_hd_service_smoke.py dist\\ASC_HDService64.exe`
   (exercises real x64 named-pipe Ping, List pagination, Get, Error).
3. Test vanilla game with `[HDTextures] Enabled=0` first. Confirm
   existing ASC streaming events, loads and FPS still behave normally.
4. Create an unmistakable **512x512 BC1/DXT1** `testname.dds` with
   actual mip payload; `testname` must match a known *RwTexture* name
   found in a live TXD. Launch service from `dist` with DDS folder.
5. Enable `[HDTextures] Enabled=1`; verify log states service catalog
   ready and D3D hooks installed, then visibly verify the substitution.
6. Test a 4096x4096 BC3/DXT5 replacement. Stream across world cells
   to exercise TXD lifetime transitions and cache eviction. Alt-tab,
   change display mode and exit/relaunch to test Reset/shutdown.
7. For VRAM measurements use `tools/watch-hd-vram.ps1` or GPUView;
   compare idle baseline, 2/4/6/8 GiB cache configurations, WDDM
   GPU allocations, frame-time peaks, and process private bytes.
   A missing WDDM counter must not be interpreted as zero use.
8. Re-run with other installed ASIs. If log says hook conflict, do not
   force the detour; disable the HD layer and identify the conflict.
9. On any instability revert `Enabled=0`; the unchanged original
   model streaming logic still works independently.

## Acceptance criteria for claiming a finished 8 GiB runtime mod

- The actual Win32 ASI and x64 service compile from this revision.
- A known original texture is visibly replaced with the correct HD DDS.
- A 2+ GiB replacement dataset can stream without process VA exhaustion.
- Repeated loading/unloading and reset do not leak or crash.
- A dedicated GPU memory counter demonstrates the target effective
  allocation while GPU frametimes and CPU VA stay within tolerances.
  The budget number in INI or log alone does NOT demonstrate this.

Source package can be handed to a Windows engineer for these remaining
hardware/game-dependent gates; do not distribute as a proven 8 GiB mod yet.
