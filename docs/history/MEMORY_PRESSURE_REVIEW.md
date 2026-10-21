# Memory-pressure and frame-time review

Scope: DistantStreaming's compiled game plugin, cache helper, and shared
scheduler/cache code. Changes are confined to this project. No automated tests
or game sessions were run. Release compilation of the Win32 ASI and x64 helper
succeeded; FPS gains and worst-frame latency have not been measured.

## Bugs and stall risks addressed

| Finding | Change |
| --- | --- |
| Full-model preloading still traversed sector/entity lists to classify models, although full-model selection never reads those classification bits. | Skip this traversal when `PreloadAllAssets=1`. Selective category preloading retains its classification scan. |
| The predictive controller collected and sorted world candidates even when memory, backlog or frame-time guards gave it a zero request quota. | Perform bounded ledger expiry and return before speculative scans/ranking. |
| Preloader retries checked the instantaneous high threshold rather than the latched high/resume state. | Pause both retries and new admissions until occupancy falls below the resume threshold. |
| Whole-map preload ignored frame/load time and kept adding background requests while frames were slow. | Pause on slow frames, slow loads, GPU/VA pressure or half-full native backlog; require 500 ms of recovery before resuming. |
| Request-count limits alone admitted batches of large replacement models. | Bound speculative admission using archive-byte hints and reserve budget headroom for native demand. This is not a bound on decompressed memory or texture dependencies. |
| Releasing an ASC pin during a temporary mission/game/scene takeover forgot ownership without clearing its keep-in-memory flag. | Defer that pin until the temporary owner releases it. Rotate the existing deque without allocating during recovery. |
| D3D9 managed `PreLoad()` hints could synchronously enter the driver near the native memory limit. | Disable these hints in the default/profile config. Explicit opt-in hints also pause on streaming/frame pressure and gradually drop queued COM references. |
| An explicit multi-job GPU preload configuration could bypass the intended interval within one callback. | Apply the interval between calls as well as between callbacks. |
| Cache diagnostics could wait behind worker publication/eviction, and archive lookups used a blocking lock. | Use try-locks on these game-thread paths; defer diagnostics or treat contention as an asynchronous cache miss. |
| The game queried memory before binding, again before scheduling, and again in world preload. Async camera submissions also reported themselves as completed allocation work. | Share the per-tick memory sample. Refresh after actual blocking loading/instancing, not mere asynchronous request submission. Rate-limit the controller-disabled memory path too. |
| Frame telemetry ignored stalls of two seconds or longer, so these could fail to trigger recovery backoff. | Record long frames and retain them in telemetry; cap only their smoothing weight. |
| A failed Windows memory query could leave speculative admission enabled when native budget management was disabled. | Mark the runtime snapshot unsafe independently of the budget manager's enable setting. |
| Background helper warming could evict useful demand pages just to read additional cold archive bytes. | Stop warming at cache capacity; warm hits do not promote pages ahead of actual game demand. Demand reads retain normal eviction. |
| Low physical RAM only paused helper warming while retaining its cache. | Return up to 32 MiB of cache pages per 100 ms recovery cycle, including after the warm queue empties. Refuse new helper-cache allocations below 512 MiB free physical RAM or insufficient commit. |
| Background warm reads competed with native disk access at ordinary thread priority. | Put only the warm thread in Windows background mode; demand IPC retains its existing servicing priority. |

The bundled profile now uses `PreloadManaged=0` and
`AssetPreload.PriorityRequests=0`. Background whole-map requests yield the
priority queue to visible demand. Nearby camera/predictive priority paths remain
available under their admission guards. Explicit custom settings are still read.
The unreachable high-occupancy admission branch and its obsolete
`HighBudgetIntervalMs` setting were removed; old INIs containing that key are
accepted and the key is ignored. ASC releases pins gradually without forcing a
synchronous bulk unload.

## Second review

- Replaced the runtime's text `ofstream` with an asynchronous stream. Gameplay
  flushes publish to a fixed queue of 64 chunks of 2048 bytes; a worker performs
  disk writes. Full queues drop chunks and report the loss. File/thread creation
  still happens at startup, and joining/flushing the writer happens only during
  shutdown. A crash can lose buffered log tails. The ostream has one game-thread
  producer; background subsystems continue using their own diagnostics.
- Predictive world collection now retains each model's best candidate while
  traversing entities. It partitions to the requested top K before sorting,
  reuses distance calculations, rejects non-finite scores before ranking, and
  handles zero-capacity collection and unavailable normal-sector arrays.
  The scratch model index is fixed-size rather than allocated every tick.
- Sector selection also partitions to top K before sorting. The generic
  controller avoids re-sorting candidates already ordered by the game adapter.
- A disconnected helper now takes an early native-I/O fallback. It no longer
  incurs cache-specific address/metadata probes or duplicate handles for fetches
  that cannot succeed. Cache-hit writeability checks use one region query and
  run only after cheap channel/size eligibility checks.
- The worker reuses its existing 1 MiB transfer-window mapping for the lifetime
  of a helper session, releasing it when the session ends. This trades a fixed
  mapped VA window for fewer map/unmap operations; it does not map the helper's
  entire cache into GTA.
- Optional readiness diagnostics now accept valid LAA streaming arrays above
  2 GiB. Pointer arithmetic is checked against the pointer width, with actual
  accessibility still checked by `ReadProcessMemory`.
- Archive-cache deduplication now includes the job actively being read, rather
  than forgetting it as soon as it leaves the queue. Queue/allocation failure
  paths roll back deduplication and eviction bookkeeping. Optional cache
  allocation failures return a miss instead of propagating out of `TryGet`.

The updated Win32 Release ASI compiled successfully. No tests or game benchmarks
were run for this pass. The x64 helper executable did not require further source
changes in this pass.

## Remaining limits

GTA streaming, RenderWare object creation/deletion, and D3D9 resource operations
remain on their required game/render threads. Moving these calls to arbitrary
workers would introduce races with engine state. Background file caching and
DXGI sampling already use workers.

At the native streaming limit, GTA can still synchronously evict resources,
decompress models and upload textures. A single engine/driver operation can
exceed a soft time budget. Exhausted/fragmented 32-bit address space, slow storage,
very large replacements, system paging, or another mod's synchronous work can
still stall or fail. These changes reduce ASC's contribution to that pressure;
they do not establish a no-stutter guarantee or increase GTA's address space.

For a deployed build, use the updated ASI and helper together and the updated
profile. The startup budget ownership work remains in place; see
[its compatibility boundaries](STREAMING_STARTUP_OWNERSHIP.md).
