# Adaptive memory-pressure protection

Changes:
- At >= configured high engine occupancy, stop speculative preload admissions (rather than permitting one periodically).
- On high occupancy, release two ASC-owned pins per configured emergency release interval, until pressure subsides.
- At the virtual-address-space early-warning boundary (configured emergency free VA + 256 MiB), release two pins per interval.
- At emergency engine occupancy, release four pins per interval; at critically low VA/physical memory release up to eight.
- Do not evict GTA/mission-owned resources: existing ReleaseSome ownership and mission/game/scene flag checks remain in place.
- GPU-only pressure pauses admissions but does not automatically release CPU model pins.

Build and validation:
- Portable preload admission unit test compiled with g++ -std=c++17 and passed.
- Windows ASI DLL and in-game crash scenario have NOT been built or tested in this environment.
- No guarantee against other mods, fragmented VA, native allocation failures, or driver-related crashes.
- Existing INI values control high threshold, emergency VA, and release interval.

Operational note: Releases unpin models; GTA's normal streaming eviction decides when the associated allocations are actually freed.
