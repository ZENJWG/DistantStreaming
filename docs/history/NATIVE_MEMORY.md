# Native texture and mesh-buffer backing

## What changed

The former HD service only indexed external DDS files. It did not offload GTA's
existing textures/models. Native-memory mode is a separate implementation that
intercepts the game's own managed D3D9 resource creation and uses shared backing
owned/mapped by the x64 companion. No native IMG/TXD/DFF files are modified.

The subsequent source asset-cache layer is described in
[NATIVE_ASSET_CACHE.md](NATIVE_ASSET_CACHE.md): the x64 process reads and retains
the actual source bytes and supplies GTA's streaming requests. This document
describes the separate graphics-backing layer.

The game receives COM facades with managed-resource locking semantics. The
driver receives actual default-pool textures and vertex/index buffers. CPU
backing is a pagefile-backed mapping per resource, mapped in full in the x64
helper. GTA opens its handle but maps only a temporary mip/buffer view during a
lock or restoration. Views are unmapped after use. This permits backing storage
whose aggregate address space exceeds 4 GiB without mapping it all into GTA.

Data already loaded before device interception can also benefit: a bounded
RenderWare TXD scan copies eligible primary texture rasters into the new backing
and replaces the raster's owned texture pointer only after the GPU copy succeeds.
The old texture reference is then released. Other owners/bindings can retain old
references until they naturally release them. Preexisting mesh buffers are not
rewritten inside RenderWare's pooled buffer allocator; newly created managed
mesh buffers are intercepted normally.

## Lifecycle and configuration

Install all three matching files from `dist` in the same mod/ASI directory:

- `DistantStreaming.asi`
- `DistantStreaming.ini`
- `ASC_HDService64.exe`

The ASI automatically starts `ASC_HDService64.exe --native-memory ...` when the
D3D device becomes available. It does not pass a texture directory. Each game
session uses a random named-pipe/mapping namespace. The helper authenticates the
connecting parent PID; the client checks the helper PID. Control I/O has timeouts,
and parent exit interrupts the helper's waits. This is a companion process, not
a registered Windows service.

```ini
[NativeMemory]
Enabled=1
AssetCache=1
Textures=1
MeshBuffers=1
MigrateLoadedTextures=1
BackingLimitMiB=6144
```

The backing limit accepts 128-32768 MiB. There is no eviction of live backing:
after the limit is reached, new resources use the original D3D allocation path.
Individual resources are limited to 256 MiB. Allocation/format/usage failures
also fall back. Current resources retain their mapping handles if the helper
dies, so backing contents survive and device restoration can still read them.
New offloading allocations then fail and use vanilla allocation.

The legacy `[HDTextures]` DDS renderer is disabled while native-memory mode is
enabled, preventing two independent device-interception systems from competing.
Missing `[NativeMemory]` keys use the enabled defaults above. `HookMode=0` still
keeps the entire plugin passive. Already-loaded texture migration additionally
requires the active controller and successful live game-table binding.

## Supported resources and device behavior

- Managed 2D DXT1/DXT3/DXT5 textures and supported integer RGB/RGBA/luminance
  formats, with explicit mip chains. Palette, cube, volume, floating-point,
  autogen, render-target and vendor-specific formats use the original path.
- Managed vertex and index buffers with static usage or WRITEONLY. Dynamic,
  software-processing-only and unsupported usage combinations use vanilla.
- Partial texture/buffer locks, COM identity, surface containers and private
  resource data are implemented. Surface GetDC is unsupported.
- Device binding/getter interception unwraps GPU objects and preserves facade
  identity/lifetimes. State blocks retain captured facades and restore binding
  ownership. Applications must release state blocks before resetting, as with
  standard D3D9 lost-device handling.
- Before Reset, device bindings and default-pool references are released.
  Backing survives. GPU resources are recreated from it lazily on subsequent use.
  Locked resources cause Reset to return INVALIDCALL.
- Upload staging is temporary. New resources do not upload an unnecessary zero
  copy of every mip before the game supplies its contents.

The implementation preserves the D3D device's private vtable tail and RTTI prefix.
Copying just the 119 public entries caused an access violation in the integration
test when the runtime used a private method during SetStreamSource.

## Verification

`asc_native_backing_tests` launches the actual x64 helper. A Win32 client creates
twenty 256 MiB backing mappings (5 GiB total), writes and checks first/last regions
through short views, verifies low client private memory, then verifies release,
helper shutdown, budget refusal, and backing survival after deliberate helper
termination. This allocates 5 GiB of committed backing; it does not populate all
5 GiB with asset data. It skips if 6 GiB of system commit is unavailable.

`asc_native_resource_tests` creates a real HAL D3D9 device and checks:

- managed facades backed by actual DEFAULT-pool driver resources;
- native texture migration and partial/mip locks;
- vertex/index buffer writes and GPU-side vertex-buffer contents;
- GPU-rendered pixel colors before and after reset, including DXT1;
- surface lifetime across reset, state block capture/apply, and cleanup.

These tests run automatically on x64 builds. For Win32 cross-process tests,
configure `ASC_NATIVE_TEST_SERVICE` to the built x64 EXE path. The normal
dependency build script builds the helper first and configures that path.
GPU tests skip when a HAL device cannot be created.

Earlier graphics-backing results on 2026-10-16, before the source asset-cache
update: Win32 ASI and x64 helper built, all 18 CTest tests
passed on each architecture with both native integration tests executed (not
skipped). The separate legacy DDS pipe smoke test and source separation audit
also passed. Matching ASI/INI/helper and ASI symbols were packaged in `dist`.
The later asset-cache changes have only been compiled and reviewed; no tests
were added or run and the new build was not executed, as requested by the user.

Look for `Native memory: x64 helper PID=...`, `device bridge ACTIVE`, and
periodic `helper=connected`/`backing bytes=...` markers in the game log. The
mapped-byte gauges describe explicit backing views, excluding allocation
granularity overhead, temporary D3D staging, and driver mappings.

## Limits that remain

This is a resource-backing bridge, not a full 64-bit engine port. Active GTA
objects, collision, animations, scripts, RenderWare CPU geometry/metadata,
unsupported resources and the engine's streaming accounting remain 32-bit.
The original native streaming budget is not raised to the companion's backing
limit. Default-pool resources can also consume driver address space; the driver
controls physical placement/residency. A 6 GiB backing limit does not guarantee
6 GiB of resident game assets or VRAM.

Resource uploads/restoration can block while allocating/copying data. Larger
resources can cause frame spikes. The live TXD raster-owner migration and
compatibility with the user's ASIs/device wrappers still need an actual GTA
test; the standalone GPU harness does not establish those properties. No FPS,
in-game memory reduction, or full-game stability result is claimed.

Primary D3D references: [D3DPOOL](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dpool),
[resource management](https://learn.microsoft.com/en-us/windows/win32/direct3d9/managing-resources),
[Reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-reset).
