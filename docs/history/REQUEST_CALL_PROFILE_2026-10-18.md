# Native RequestModel call-site profiling (2026-10-18)

## Goal
Identify high-volume GTA-originated `RequestModel` callers and model IDs without
changing native request dispatch, streaming completion, draw-distance scanning,
or Cache64 behavior.

## Implementation
- The existing main-thread RequestModel detour samples **1 in 128 native calls**.
  Each sample captures the immediate return address, model ID and request tick.
  Plugin-originated calls and off-main-thread native calls remain counted in
  `Streaming flow`, but are excluded from the detailed sample.
- Instrumented samples separately measure time spent in the detour *before*
  calling original `RequestModel` and inside that original call. Sampling
  instrumentation itself contributes to these measurements; the values must
  not be multiplied into a precise per-frame cost.
- The hook uses one fixed **1,024-sample** interval buffer (drops are counted),
  no memory allocation, no symbol or stack unwinding and no engine-model-state
  reads. Per-interval caller heavy hitters use 24 bounded counters; model
  heavy hitters use 32 counters. Error bounds indicate approximate counts for
  addresses/IDs replacing a prior low-frequency entry.
- The scheduler interprets model state **later**, during the one-second report.
  The resulting classification may differ from state at the actual request.
  The top six return addresses and top six sampled model IDs are logged.
  Addresses need a disassembler and exact executable/mod module map to assign
  function names. An address alone does not identify a culprit or prove that
  its call was redundant.
- Only pending sampled models enter a **128-slot** observer. It polls verified
  model-state entries on controller ticks, counting first-observed loaded,
  first-observed not-loaded, and 30-second timeouts. This is an observation of
  sampled state *transitions*, not precise native load-completion timing.
  Duplicate pending samples do not restart a model's timer.

## New DistantStreaming.log records
- `Request caller samples`: sample count, dropped count, delayed model-state
  breakdown, hook-only and original-call average/peak microseconds.
- `Request top callers`: up to six return addresses and sampled counts with
  approximate error bounds.
- `Request top models`: up to six model IDs with total approximate sample count
  and retained loaded/pending/notLoaded counts. Per-model state counts can be
  incomplete when a low-frequency slot is replaced.
- `Request sampled load latency`: observed loaded, returned-to-notLoaded,
  timeout, capacity misses, active tracking and average/peak milliseconds.
- `Streaming scheduler CPU us` now includes `requestProfile`, covering the
  once-per-second classification, sorting and report preparation.

## Safety and interpretation
- Always invoke native `RequestModel` with the exact original arguments.
  Nothing is skipped, coalesced or intercepted for acceleration.
- Do not use sampled caller counts to suppress calls until native call-site
  semantics and loaded/requested model state are independently verified.
- No Load trampoline or Cache64 hit substitution was enabled.
- Win32 HOODLUM/MinHook compilation and GTA runtime tests are still required.

## Local validation
Portable Debug test suite exercises bounded caller/model top-K, duplicate
requests, observed loading/cancellation, tick-count wraparound and timeouts.

## Resource class, module and precise-time follow-up
- Standard SA 1.0 diagnostic boundaries: DFF 0..19999, TXD 20000..24999,
  COL 25000..25254, IPL 25255..25510, DAT 25511..25574, IFP
  25575..25754, RRR 25755..26229, SCM 26230..26311. Internal
  streaming-list sentinels are 26312..26315. Values above that are
  **extension/unclassified**, not automatically invalid.
- TXD and other non-DFF IDs were previously miscounted as `invalid/unbound`
  because the state lookup stopped at DFF ID 19999. The profiler now records
  a resource family independently of state, and attempts a *read-only*
  delayed state sample for standard resource entries after validating the
  specific `CStreamingInfo` entry. Inaccessible entries are counted as
  `state unavailable`; they are not treated as invalid native requests.
- Top caller addresses now include Windows-loaded module name + relative
  virtual address (or committed memory region if no module owns the address).
  This happens only at report time, not inside the RequestModel detour.
- Hook timing accumulates raw high-resolution QPC ticks and converts averages
  and peaks to **nanoseconds** only when logging; QPC granularity and sampling
  overhead limit accuracy. Those timings **include** instrumentation, so do
  not interpret them as net overhead of the detour.
- The new `Streaming memory correlation` line includes free process VA,
  cached largest VA hole, private bytes, current stream use/budget/headroom,
  and game backlog at report time. The largest hole can be up to 10 seconds
  stale. This is diagnostic only.
- Streaming requests, streaming/LOD rendering, x64 cache read substitution
  and the Load trampoline are unchanged. Run Win32 GTA to validate.
