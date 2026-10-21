# RTX 4070 SUPER (12 GB): D3D9 on-bind priority and memory budget

## Objective

Prioritize **already-owned** managed GTA D3D9 textures/vertex/index buffers
that the renderer actually binds, while maintaining the independent x64
source-byte cache. This is a residency hint, not a cross-process GPU cache.

## What this release does

- `IDirect3DDevice9` vtable slots 65 (`SetTexture`), 100 (`SetStreamSource`)
  and 104 (`SetIndices`) call their original COM function with identical
  arguments and HRESULT. After a successful call, approximately every 128th
  non-null binding is inspected for DistantStreaming's private lifetime tag.
- Existing `D3DPOOL_MANAGED` assets tagged at creation get a one-time
  `SetPriority(64)` if their current priority is lower. Never downgrade a
  priority assigned by a different mod. No additional COM resource retained.
- No per-frame resource residency list, no extra resource proxy, no runtime
  data copy, no extra GPU allocation, no synchronous PreLoad.
- `IDXGIAdapter3::QueryVideoMemoryInfo` polls WDDM in the existing background
  sampler: `safeLimit = max(0, min(TargetMiB, WDDMBudget) - BudgetHeadroomMiB)`.
  The existing optional speculative preloader pauses at or above that limit.
  It remains OFF by default.
- `DistantStreaming_RTX4070SUPER_12GB.ini` contains TargetMiB=10240,
  BudgetHeadroomMiB=1536, PrioritizeUsedManaged=1,
  UsedManagedPriority=64, BindPrioritySampleRate=128.
  GPU target is a ceiling, not a pre-allocation or guarantee.
- Prior existing `HelperFirstNativeTrim` may still call native
  `CStreaming::RemoveModel` for eligible *unused* models; it is the actual
  GTA 32-bit VA reclaim mechanism. This GPU pass **does not itself reclaim
  any GTA virtual address space**.

## Critical architectural constraints

Microsoft D3D9 managed resources have a CPU system-memory backing store,
and D3D9 runtime reconstructs their GPU copies on device reset.
There is no safe generic 'discard managed CPU copy' operation.
Blindly converting `D3DPOOL_MANAGED` to `D3DPOOL_DEFAULT` breaks lock/update
and device-loss/reset requirements (and can conflict with Mod Loader).

Assets and RenderWare pointers used by the running GTA 32-bit process cannot
be held as native live objects in `AssetCache64.exe`. The helper continues
to cache source file bytes; GTA parses them into its own process as needed.

The device vtable may be replaced by another mod. As before, DistantStreaming
refuses to overwrite a foreign table when ownership changes. If external
code bypasses the cloned table, bind counts can be zero.

## Diagnostics

`GPU residency:` log reports:

- WDDM LOCAL usage and budget, threshold, pressure state;
- `managed use bind calls/sampled/promoted`;
- `sampled without tag`;
- source asset helper cache hits and GTA native streaming memory separately.

When `promoted=0` and `bind calls>0`, it may simply mean no sampled
managed resource was tagged, or the device was hooked after resource
creation. `promoted>0` is evidence of executed priority hints, not a count
of GPU-resident objects or native RAM reclaimed.

## Installation and rollback

1. Build the x86 ASI with `build.bat`; keep the existing 64-bit helper.
2. Copy `config/DistantStreaming_RTX4070SUPER_12GB.ini` beside the ASI
   **renamed `DistantStreaming.ini`**; back up the old INI first.
3. Configure draw distances in Project2DFX, not DistantStreaming.
4. `PrioritizeUsedManaged=0` disables all new bind interception.
   `ManageResources=0` disables managed hints entirely.
5. Watch native streaming headroom / pop-in / visible ground alongside
   WDDM LOCAL usage. A higher VRAM priority does not directly improve FPS.

## Sources

- https://learn.microsoft.com/en-us/windows/win32/direct3d9/managing-resources
- https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dresource9-setpriority
- https://learn.microsoft.com/en-us/windows/win32/direct3d9/lost-devices
- https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/ns-dxgi1_4-dxgi_query_video_memory_info
- https://www.nvidia.com/en-us/geforce/graphics-cards/40-series/
