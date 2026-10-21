# Helper-first *native* residency reclaim (experimental)

## What can move between processes

The 64-bit AssetCache64 worker stores **raw DFF/TXD/COL/IMG source bytes**;
it does not store GTA's live RenderWare objects, pointer graphs, loaded TXD
dictionaries, collision pointers, or GPU device resources. Those necessarily
live in the 32-bit GTA process. No DLL can transparently redirect GTA's
pointer-owned live objects to an x64 process without replacing the resource
model/renderer.

## Implemented native memory reduction

When `[OffscreenCleanup] HelperFirstNativeTrim=1` and Mod Loader's real
`std.stream.dll` read bridge is active and reports >=64 MiB cached on the
external worker, the game render callback uses a hysteretic memory gate:

* Below 256 MiB **budget headroom**, reclaim begins, and it stops at 512 MiB.
* It only scans when GTA is in safe normal outdoor gameplay, the camera is
  valid, and native outstanding requests are <=24.
* Existing stale offscreen instance removals enqueue candidate DFF models.
* A second, incremental model-ID sweep evaluates max 256 entries at a
  200 ms interval. A model needs zero engine RW references, loaded state,
  no engine KEEP/GAME/MISSION/SCENE/PRIORITY flags, known file size, no
  recent native request, no ped/vehicle ownership, and >=5000 ms of
  continuous eligible observation before **native** `CStreaming::RemoveModel`.
* Max one model is released per frame in the default profile.
* Any failed check restarts its candidate clock; leaving the pressure mode
  also clears the observation history.
* GTA retains streaming state ownership and re-requests models via its native
  pipeline; the existing x64 Mod Loader cache remains an *opportunistic* source
  of raw bytes for those reads. There is no guaranteed model-ID-to-remote-page
  mapping, so a re-request can still read from disk.

**Risk:** Too-small native budgets or aggressive cleanup can cause model
thrashing or pop-in when turning. This is experimental. The exact Windows
gta_sa.exe / OLA / Mod Loader combination must be gameplay tested.

## What to look for

`Helper-first native reclaim: ... nativeModelsRemoved=...,
nativeBytesReclaimedMiB=...` is the metric proving that GTA's native
stream manager actually freed resident objects. A large helper cache alone
does not prove this. Test sustained camera turning at ground level across
3000 units. If any visible LOD or normal object pops or disappears, set
`HelperFirstNativeTrim=0`, and if needed `UnloadUnusedModels=0`.

The cache worker's `helperCachedMiB` should stay separate from the GTA
`streaming used/budget/headroom MiB` telemetry. They are different layers.
