# DistantStreaming + SanFrameOptimizer coexistence (source fix)

This document covers the two projects distributed together. Both target
GTA San Andreas Classic PC US 1.0, Win32. SanFrame is the renderer/pacing
optimizer; DistantStreaming is the model-streaming and asset-cache owner.

## Source-level cause

DistantStreaming's `GpuResidencyWin.cpp` clones the live D3D9 device vtable
and, when `FramePacingTelemetry` is enabled, observes device slots 17
(Present) and 42 (EndScene). It also hooks resource creation and binding.

SanFrameOptimizer's `D3D9Hook.cpp` MinHook-detours device Present, Reset and
(optionally) EndScene. Its state cache and submission optimizer patch additional
D3D9 vtable slots. Those hooks are not automatically independent: a copied
or replaced device table can retain hook pointers owned by the other ASI,
and the order in which wrappers run depends on the loader.

Previously SanFrame detected only `DistantStreaming.asi` at device creation.
That missed `.SA.asi`/legacy names and mods loaded after device creation.
Its default `OverlayPresentOnly=0` allowed the EndScene detour anyway.
DistantStreaming also enabled its optional Present/EndScene probes by default
for every supplied profile. These are concrete integration risks identified
from the sources, not a proven single cause of a specific in-game crash.

## Changes in these archives

* SanFrame's `Compatibility.OverlayPresentOnly` now defaults to 1, including
  when the INI option is missing. This avoids the additional inline EndScene
  hook in either ASI load order; the OSD still draws in Present.
* SanFrame's loaded-module detection recognizes
  `DistantStreaming.asi`, `DistantStreaming.SA.asi`, and the known DLL and
  AdaptiveStreaming aliases. Arbitrary renames remain covered by the
  Present-only default, not by automatic filename detection.
* DistantStreaming's five shipped profiles now set
  `GPUResidency.FramePacingTelemetry=0`; missing-INI defaults and diagnostic
  draw sampling default off as well. The probes remain explicitly opt-in.
* When SanFrame is *already loaded* at DistantStreaming's GPU hook
  installation, DistantStreaming additionally disables the optional
  Present/EndScene probe and records this in its log. GPU allocation tracking,
  managed-resource priority hints and WDDM budget sampling remain active.
* This intentionally does not disable SanFrame batching, DistantStreaming
  native streaming, the 64-bit helper or either plugin's essential hooks.

## Building and installing

1. On Windows with Visual Studio C++ tools and the Windows SDK, build
   SanFrame with `build.bat Release` (Win32) and DistantStreaming with
   `build.bat` (the latter also builds its optional x64 helper).
2. Install **the newly built ASIs**, not the source ZIPs, into the game's
   ASI-loader directory. Use the matching `.ini` files with the ASIs.
   Deploy `DistantStreaming_AssetCache64.exe` beside the DistantStreaming ASI
   if its helper feature is enabled.
3. Confirm in the SanFrame log that the Present-only path was selected.
   If SanFrame is already loaded when DistantStreaming attaches GPU hooks,
   DistantStreaming logs that its optional frame probes were skipped.
4. Restart the game and check loading, gameplay, Alt+Tab, device reset,
   save/load, streaming-memory counters and the Cache64 helper display.

## Troubleshooting

If freezing or missing frames persists, collect SanFrame/DistantStreaming logs
and first test `ImmediateBatching=0` and `StaticPropInstancing=0` under
SanFrame's `[Optimization]` section. Those features install numerous other
device hooks and are independent of the EndScene overlay change. If needed,
also test `D3DStateDeduplication=0` in isolation. Do not change GTA streaming
memory ownership to SanFrame; its memory displays are read-only.

This is a defensive compatibility fix, not proof that every graphics mod stack
is stable. A Windows x86/Direct3D9 integration run is still necessary to prove
runtime compatibility and performance. The source archives do not contain
compiled ASI binaries.
