# OLA compatibility — active DistantStreaming priority (2026-10-16)

## Why the old build ran with no visible effect

The user's log showed a live streaming array (`071B3020`, readable) but invalid
Plugin-SDK snapshot pointers (`909005C1` cached streaming pointer and `06090000`
model-table pointer). OLA reported 50,000 model slots. The previous controller
stopped because it treated relocated OLA pointers as an error.

## Revised ownership model

**OLA owns:** relocating model/streaming tables, pools, expanded limits, and
everything unrelated to the native streaming-memory budget.

**DistantStreaming owns:** predictive streaming requests, its request detour,
graphics replacement cache (experimental), and when explicitly enabled, the
single `CStreaming::ms_memoryAvailable` native budget scalar.

The included active INI sets:

```
[Diagnostics]
HookMode=1
ControllerEnabled=1
HookIntegrityMonitor=1
HookCheckIntervalMs=1000
RecoverPristineHooks=1
ReadinessTelemetry=0

[Memory]
ManageStreamingMemory=1
DistantStreamingOwnsNativeBudget=1
StreamingMemoryLimitMiB=896
AddressSpaceReserveMiB=384

[Prefetch]
Enabled=1
MaxRequestsPerTick=8
MaxAdaptiveRequests=6
MaxOutstanding=36

[HDTextures]
Enabled=1
TextureCacheTargetMiB=6144
```

The native budget has no fixed 1,024 MiB source cap; it is constrained by
address-space headroom and the 32-bit counter. It is recalculated against
available x86 process virtual address space. It is **not** 8 GB VRAM.
When OLA is detected with ASC ownership, ASC checks the budget once per second
and reasserts at render/game-process frame boundaries the configured amount if changed. Matching OLA's initial
`[SALIMITS] MemoryAvailable` to 2000 can avoid repeated writes.

## Request-hook protection

The original request entry is `0x004087E0` (GTA SA 1.0 US).
DistantStreaming installs a MinHook jump, then verifies its target periodically.

- **Owned**: normal scheduler operation.
- **Pristine**: if a third party restored the *verified original bytes*,
  `RecoverPristineHooks=1` can re-arm this ASI's hook.
- **Foreign**: if the entry contains unknown bytes / a different jump, the
  controller logs `HOOK INTEGRITY` and stops speculative requests. Replacing
  that unknown detour would risk infinite recursion, overwritten trampolines,
  and new crashes. No arbitrary memory-page locking, forced detour ownership,
  or disabling OLA occurs.

The monitoring applies to the **Request hook used in `HookMode=1`**; the
optional HOODLUM Load trampoline is intentionally not part of this active
performance profile.

## OLA pointer rebinding and fallback

After streaming initializes, this ASI probes `CStreamingInfo::ms_pArrayBase`
and independently decodes the GTA SA 1.0 US model-table reference at
`0x00546680`. It probes candidate model objects before rebinding only its
local Plugin-SDK pointer snapshots. Binding retries up to 12 times at
500 ms intervals and is periodically checked for later changes.

A failed model table still **prevents speculative model requests and the HD
renderer scan**. The native memory limit is independently updated while the
game is initialized, so the entire memory limiter does not silently stop.
Expanded model IDs beyond the current 20,000-ID internal predictor ceiling
are not yet handled.

## Performance profile

The active profile uses 40 ms scheduler ticks, 2.8-second lookahead,
asynchronous camera readiness (no native blocking disk-load wait),
3 offscreen instance deletions per frame maximum, and no destructive
native model/texture unloads. Dynamic request observations are on; readiness
engine-state telemetry remains off due to the diagnosed OnRequest crash.

The HD renderer is experimental and needs an **available x64 DDS service**.
The configured 6,144 MiB cache is a *software budget*, not a demonstrated
dedicated GPU VRAM allocation. If the service is unavailable it cannot load
replacement DDS textures.

## Runtime validation

Build with `build.bat`, install only one `DistantStreaming.asi`, and keep OLA.
Look for these lines in `DistantStreaming.log`:

```
OLA bindings: ... stream_ok=1 ... model_ok=1
OLA bindings: ACTIVE
ASC BUDGET OWNER: OLA loaded; enforced native budget MiB=...
ASC activity: cells=... issued=... outstanding=...
```

While travelling through previously unloaded areas, an `issued` count above
zero demonstrates that speculative streaming requests are being made.
`issued=0` at a single location is not necessarily a failure.

On a runtime conflict or crash, first set `HDTextures.Enabled=0`, then
`OffscreenCleanup.Enabled=0`, then `DynamicReadiness.Enabled=0` to isolate
the optional features without disabling the core controller. For complete
passive comparison use `HookMode=0`, `ControllerEnabled=0`, and
`ManageStreamingMemory=0`.

No Windows x86/OLA gameplay test or 8 GB VRAM-residency verification is
claimed from the portable source tests.
