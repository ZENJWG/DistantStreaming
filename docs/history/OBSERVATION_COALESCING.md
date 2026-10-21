# Request observation overload fix — 2026-10-15

The reported run contained 863 samples across 910.86 seconds, 14,187,232
hook calls, 5,762,860 dropped observations, and 8,192 overflow-driven resets.
Some reporting intervals contained 110,721 requests. Only 187 speculative
requests and zero dynamic warming requests were recorded. These are baseline
measurements from the old deployed build, not results for this revision.

## Change

Replace the 1,024-event request ring with fixed per-model atomic mailboxes:

- IDs 0–19,999 have separate main-thread and worker-thread pending records.
  This matches the plugin's existing supported model domain. TXDs and other
  resource IDs outside it are forwarded to GTA normally but not observed for
  model ownership or learning; they are counted separately.
- Repeated requests for an already-pending model merge the game's byte-wide
  flags with OR and preserve the first request timestamp. Mission/pinned flags
  therefore remain visible to the dynamic-demand eligibility filter.
- Main/worker records remain separate. Worker traffic cannot turn into a
  main-thread learning observation or replace its timestamp/flags.
- A dirty bitmap identifies pending records. The scheduler drains at most
  1,024 records per tick, preserving deferred records. A rotating scan prevents
  continuously active low IDs from starving higher IDs. Chronological order
  across distinct IDs is not retained or required by the controller.
- The full supported domain can be pending concurrently without losing an
  observation to a full event queue. Repeated calls cannot occupy additional
  slots. No overflow-driven controller/tracker reset or repeated log write is
  needed. Legitimate safety resets for unsafe game transitions still apply.
- Vanilla receives every original call with its original ID and flags,
  exactly once. Coalescing applies only to the plugin's observations.

Storage is approximately 318 KiB for payloads and bitmap. Hook observations
use lock-free atomic operations with no allocation, file I/O, or mutex. When
a repeated call adds no flags, its mailbox requires only a load; diagnostic
call/coalescing counters still use atomic increments. A drain inspects at most
1,250 bitmap words and emits at most 1,024 records. The loaded game/model
tables are accessed only later on the scheduler thread.

`lost_events` is retained as a CSV compatibility field and is zero for this
mailbox path. New `coalesced_requests` counts successfully merged repeats;
`ignored_resource_requests` counts IDs outside the supported observation
domain. A high coalesced count is expected and is not a dropped-event count.
`hook_requests` continues to count raw detour calls, including plugin calls.
CSV analysis supports the new columns and older files without them.

## Validation

VS 2026 Win32 Release ASI compilation and six CTest suites pass. New tests:

- 14 million repeated observations produce 128 retained model observations.
- All 20,000 models in both lanes (40,000 records) drain without capacity loss
  or duplicates, while respecting the per-tick bound.
- Flag merges, main/worker independence, zero timestamp, timestamp rollover,
  invalid IDs, and scan fairness are exercised.
- Four concurrent producers and a bounded consumer exercise merge/drain/
  republish races and verify all producer flags survive for every model/lane.
- Repeated 110,721-call bursts across controller ticks still qualify local
  dynamic demand and result in one bounded warmup request.
- The original controller, memory policy, archive sampling, and legacy ring
  component tests pass. The ring is no longer used by the game request hook.
- The source separation audit passes.

The tests model request pressure; they do not replay the original request IDs
or timing, which the CSV does not contain. Game runtime and performance gains
remain unverified. The startup log identifies this revision with:

```text
Observations: per-model coalescing active; bounded fair drain; no overflow resets
```

For the next game run, verify that line, no overflow-reset spam, zero
`lost_events`, and nonzero `coalesced_requests` under traffic. Dynamic issuance
still depends on genuine repeated eligible demand, model unload state,
cooldowns, and normal admission guards; it is not guaranteed to be nonzero.
Readiness still excludes models already loaded when observations are drained.

The run's 75% throttle / 70% resume behavior is a separate admission policy.
This revision preserves those thresholds. A streaming ratio near 78% can
still suspend speculation after entering the throttled state, even when the
Windows address-space guard is clear.
