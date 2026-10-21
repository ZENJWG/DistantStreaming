# AssetCache64 performance implementation (historical, 2026-10-18)

The previous renderer traversal extensions from this optimization pass have
been removed from DistantStreaming; Project2DFX owns draw distances and LOD
visibility. The following cache changes are retained in the current project.

## Helper capacity and accounting

The helper initializes an authoritative page-cache capacity and updates it
when shared backing is allocated or released. `AssetCache::Load` checks the
current limit under the metadata mutex both before starting a read and before
publishing it. A finishing read cannot restore indexed pages above the latest
capacity captured by `SetCapacity`. A one-off physical-pressure trim remains
separate from the shared-backing capacity limit.

Diagnostics distinguish:

- `helperCachedMiB`: indexed page allocation size.
- `helperLivePageMiB`: actual page allocations, including loading pages and
  pages still pinned by readers after eviction.
- `helperPendingReservationMiB`: reserved in-flight page sizes, which can
  overlap the live allocation count and must not be added to it.
- `helperPageCapacityMiB`: the current indexed-page capacity.

The capacity is an indexed-cache limit. It is not a strict total process-RAM
cap: readers, pending allocations, mappings and metadata require memory too.
Allocation accounting remains alive with page ownership and is decremented
when the page is actually freed. Completed source-read bytes are counted even
when a changed identity or reduced capacity prevents publication.

The coverage payload grew from 64 to 88 bytes. Native helper protocol version
is now **4**; install the new ASI and helper EXE together. Older counterparts
reject the request version rather than decoding the different payload layout.
Existing protocol size/version assertions were updated; tests were not run.

## Cache value and interception cost

`Lookup` pins immutable data under a nonblocking metadata lock, updates LRU,
then unlocks before copying. Worker eviction skips buffers pinned by copying
hooks. It makes one bounded pass through the LRU and drops a new publication
if pinned buffers leave insufficient room (`pinnedPublicationSkips`), keeping
the local indexed-byte cap and normal eviction frees on the worker.

One in 64 executions samples elapsed time for metadata queries, lookup, copy,
queueing, each detour, and native `ReadFile` invocation. Reports give
`avg/peak/samples/calls` in nanoseconds. Lookup includes copy; detours include
native calls, so these values overlap and must not be summed or subtracted
as an exact CPU-overhead estimate. They are CPU-side elapsed measurements,
including waits, not GPU time or thread CPU accounting. Async native timing
measures call submission/return, not later I/O completion. Sampled peaks can
miss unsampled stalls. Metadata measurements include the worker's publication
validation as well as hook queries.

Reports also expose lookup hits/attempts, lock misses, copied bytes, queued
source bytes, fetched source bytes, and per-report served/queued/helper-read
byte deltas. Helper disk deltas are comparable only when
`reportHelperDiskComparable=1`; the first report after a helper change establishes
a new baseline. Helper statistics are polled every five seconds, so interval
bytes are approximate across polling boundaries. `helperDiskMiB` measures bytes
returned by source file reads, which Windows may satisfy from its own cache;
it does not measure physical storage traffic. Copies are not necessarily
successful substitutions, so `bytesServed` remains the useful savings counter.

Source metadata is still checked on each candidate read, preserving handle
reuse and changed-source detection. No automatic low-hit-rate bypass is enabled
without runtime evidence; these measurements support deciding whether a source
or cache route actually pays for its overhead.

## Build and delivery

Release Win32 ASI and x64 helper builds succeeded with the existing toolchain.
No tests, game launch, FPS measurement or gameplay validation were performed.
Existing Plugin-SDK/compiler/link notices remain. Matching ASI, helper and ASI
symbols are packaged in `dist`. Existing INI tuning is preserved; the new keys
use their defaults when absent.
