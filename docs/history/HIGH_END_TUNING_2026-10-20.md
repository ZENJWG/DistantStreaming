# High-end loading profile — 2026-10-20

`config/DistantStreaming.ini` and `config/DistantStreaming_GLOBAL5_EXPERIMENT.ini`
now contain the same aggressive loading profile. They target a fast CPU/SSD,
32 GB or more system RAM and the project's existing 12 GB VRAM target.
These are source-based tuning choices; their gameplay benefit is unmeasured.
The GLOBAL5 filename is historical and does not add a 5x draw distance.

| Area | New primary configuration |
| --- | --- |
| Catalog requests | Up to 30 per tick, 300 per second; 80-model backlog guard |
| Catalog work | 64 checks, 1500 us work budget, 100 ms cell query cadence |
| Current cell | Rotate hint batches every 1000 ms when the worker is idle |
| Predictive controller | 40 ms cadence, 96 cells, 384 candidates, up to 16 requests |
| Prediction | 3.5 s look-ahead, 400-unit prediction cap, 200-unit scan radius |
| Camera readiness | 100 ms cadence, 480-unit cone, 8 model requests, 4 instances |
| World preloader | 6 model pins per tick, 1024 pins / 512 MiB archive-cost cap |
| Existing world instances | 8 attempts per tick, 1250 us soft budget, 1024 successful creations per session |
| Instancing VA guard | At least 1536 MiB free in GTA's 32-bit address space |
| Source warming | Verified observed dependencies/IMG members, 4096 MiB source quota |
| Helper cache | Up to 12288 MiB in the x64 helper; grows on useful demand/warming |
| GTA local cache | 64 MiB; not a mapping of the entire helper cache |
| GPU target | 10240 MiB ceiling with 1024 MiB headroom; Windows budget still applies |
| Retention | 200-unit near-player protection, 2500 ms offscreen grace, 8000 ms request quiet period |

Each scheduler also yields to memory, GPU, frame-time and native-backlog pressure.
The quotas are ceilings, not promised throughput. A native model request may
also enqueue dependencies. Model pins use archive bytes as an admission hint,
not as an estimate of actual resident RAM. Native instancing calls cannot be
interrupted if one call exceeds a soft time budget.

Blocking camera loads, synchronous managed-resource `PreLoad`, unverified
CdStream hit completion, full-archive background warming and all-category model
pinning remain off. Increasing these would introduce blocking work or unrelated
memory use. Vehicle/ped populations, texture resolution, effects and render
distance are owned by GTA, installed assets and visual mods such as Project2DFX.
This profile prepares eligible existing world entities sooner.

Explicit catalog overrides take precedence over `SpeedPreset`. To use a lower
preset alone, remove `MaxRequestsPerTick`, `MaxChecksPerTick`, `WorkBudgetUs`,
`MaxRequestsPerSecond`, `CatalogQueryIntervalMs`, `RescanIntervalMs`,
`MaxNativeBacklog`, `MaxFrameMs` and `CooldownMs` from `[WorldResidency]`.
For less speculative work, set `[AssetPreload] Enabled=0` and
`[FrameStability] AllowSpeculativeAssetPreload=0`. Verified source warming can
be disabled together with `WarmObservedModLoaderSources=0`.

Alternative helper-first, RTX, stable and visual-test INIs retain their prior
settings. Their comments were shortened, ignored `CatalogStaticWorld` / legacy
`[D3D9] Mode` settings were removed, and escaped newlines hiding the Mod Loader
copy cap were repaired. Original profiles are backed up under
`.review-backups/2026-10-20-high-end/config/`. The unrelated
`config/SanFrameOptimizer.ini` was not edited.

Code changes reuse predictive/camera planner buffers and dynamic-candidate
storage, separate immutable ready-catalog status polling from fast cell hints,
and skip classification/model-selection work when the non-rotating native pin
cache is full. Pressure recovery, retry checks and instancing still run.
No tests, game launch or frame-time benchmarks were performed for this pass.
MSVC Release builds of the Win32 ASI and x64 helper passed. `build.bat fast`
packaged the matching binaries, ASI symbols and primary INI into `dist`;
the build log is `build-vs2026-win32/high-end-pass-build.log`.
