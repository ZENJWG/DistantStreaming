# World Residency V2 (experimental, protocol v6)

## Ownership and physical limits

DistantStreaming owns read-only static placement cataloging and optional streaming requests.
Project2DFX-zen exclusively owns draw distances, visibility, LOD rendering and lights.
GTA stays x86. External Cache64 stores **source bytes and catalog metadata**, not live
`CEntity`, `RwObject`, collision state or GPU-managed native structures. This cannot
remove GTA's 4 GiB virtual-address ceiling.

## Implemented in this source

- The x64 helper builds a bounded, immutable catalog from `gta.dat` IDE/IPL text
  and GTA SA `bnry` IPL instance blocks, including binary IPL files inside
  validated IMG v2 (`VER2`) archives in `models/` and registered IMG paths.
- Binary IPL decoding checks 76-byte headers, instance counts, offsets,
  40-byte records, finite positions/quaternions, model range and placement caps.
  A rejected block contributes zero placements, never a partial block.
- Exact source/entry location is included in each placement's process-local
  generation/identity hash. Text IPL model ID must match the IDE definition.
  Conflicting IDE numeric model IDs or unresolved names never become prefetch hints.
- Catalog v6 queries return bounded model name + candidate ID pairs. The x86 ASI
  receives these over its background cache worker; no game-thread pipe I/O.
- Optional `PrefetchModels=1` performs **main-thread live GTA/OLA model
  name + ID validation** before issuing a bounded `RequestModel(id,0)`.
  Admission rejects low x86 VA, overloaded streaming queues, high frame time,
  unsafe gameplay state, GPU pressure and invalid identity.
  Requests are nonpriority and unpinned. No native entity spawning is added.
- Read-only diagnostics include indexed IMG/binary IPL coverage, unresolved
  identity, and cumulative native prefetch admission/rejection counters.
- Existing source-cache reads, observed Mod Loader hook data and background
  archive warming are preserved unchanged. Helper protocol **v6** requires
  rebuilding/deploying ASI and helper together.

## What this DOES NOT provide

The catalog cannot claim complete coverage of an actively modded GTA installation.
The filesystem scanner **does not establish effective Mod Loader precedence**,
does not track dynamic/mission-created entities, and cannot guarantee live IPL
activation/area/timed semantics. It does not yet produce an on-disk catalog cache,
bind all DFF/TXD/COL dependency edges to effective source identities, or schedule
full-world source warming based on verified source maps. This is intentionally
not enabled by default; guessing source precedence risks wrong memory behavior.

Even with reliable external source caching, GTA's 32-bit native model resources
must stay below its address-space limit. The goal is minimal pop-in and low
streaming latency, not every CEntity/RenderWare allocation resident at once.

## Configuration (safe initial test)

```ini
[WorldResidency]
Enabled=1
CatalogStaticWorld=1
PrefetchModels=0
MaxRequestsPerTick=2
MinFreeVaMiB=768
MaxNativeBacklog=24
MaxFrameMs=25
CooldownMs=15000
```

Only enable `PrefetchModels=1` on a **separate experimental profile** after
verifying both binaries launch and catalog names/IDs resolve correctly.
With a matching x64 service, `WorldCatalog64` logging reports coverage; the
prefetch counter must remain zero if `PrefetchModels=0`.

## Safety and validation

- Portable tests: builds with CMake on Linux and runs CTest. Includes genuine
  synthetic IMG v2/binary IPL fixtures, malformed bounds, identity mismatch,
  same-size source change, and native admission policy.
- Cannot validate Windows (x86 ASI, x64 helper, MSVC link, pointer lifetime),
  Project2DFX + Mod Loader effective override behavior or actual gameplay
  memory/pop-in performance on Linux. **Windows runtime testing is required.**
- Source and protocol change are intended to fail open: disable this feature
  or fail the helper handshake without changing normal native GTA streaming.

## Remaining implementation gates

1. **Observed effective source resolver** for Mod Loader priorities/profiles and
   real IMG/DFF/TXD/COL dependency graph. The filesystem inventory is NOT
   an authoritative replacement for live Mod Loader results.
2. Bounded source-aware whole-map warming with measurable ready-hit/read savings
   and no synchronous calls on the game/render thread.
3. Versioned on-disk catalog serialization with source-content fingerprint
   validation, atomic publication and crash/restart recovery.
4. Windows integration tests (real SA 1.0, OLA, Project2DFX-zen, Mod Loader,
   multiple saves, missions, area/time changes, collisions, A/B profiles).
   Benchmark frame P50/P95/P99, x86 free VA, cache served bytes, load time,
   model backlog and visual pop-in before considering any default-on policy.
