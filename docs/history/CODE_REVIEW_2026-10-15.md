# Code review and fixes — 2026-10-15

Reviewed the portable controller, game adapter, hooks, runtime, telemetry,
configuration, tests, and build setup. Changes are in source and the rebuilt
Win32 Release ASI in `dist`. No files were installed into the game directory.
This review does not establish an FPS improvement or gameplay stability.

## Bugs fixed

| Priority | Finding | Change |
| --- | --- | --- |
| High | Stale ledger entries disappeared while their engine requests were still pending. New speculative work could exceed `MaxOutstanding`. | Keep pending speculative requests in the count until they complete, are cancelled, or are promoted to vanilla ownership. Expiry still never cancels engine requests. |
| Medium | Adding the future endpoint to a two-cell selection could overwrite the current endpoint. | Replace a cell other than the current endpoint. Added 625 trajectory cases. |
| Medium | Expiring dynamic learning confidence also erased the rewarm cooldown, permitting early repeat requests. | Preserve the warm timestamp when restarting confidence for an existing history entry. History remains bounded; eviction and explicit safety resets discard entries. |
| Medium | One-second CSV samples recorded only the latest tick's `issued`, `expired`, `denied`, and dynamic event counts; `promotions` was cumulative instead. The analysis tool summed these incompatible values. | Accumulate events over each reporting interval, publish promotions once, and retain latest-tick gauges. CSV columns remain unchanged. |
| Medium | Request observations had no timestamp or source-thread identity. Delayed draining shifted request times and worker requests could qualify as main-thread dynamic demand. | Record the request timestamp and whether it came from the installing game thread. Use request time for learning/readiness and restrict dynamic learning to that thread. Clamp concurrently arriving future timestamps to the sampling tick. |
| Medium | The hook ring subtracted signed positions after conversion; on Win32, crossing the signed counter boundary can overflow. | Subtract unsigned positions before converting the modular difference to the signed comparison type. |
| Medium | A continuously refilled observation ring could keep a scheduler drain running indefinitely. | Limit each drain to the ring's 1,024-record capacity. Remaining records wait for the next tick. |
| Low | Sector conversion cast arbitrarily large finite coordinates to `int` before clamping. | Clamp the sector coordinate before converting. |
| Low | Scheduler diagnostic messages contained literal `\\n` text. | Emit actual newlines. |

## Removed unnecessary work and code

- Calculate frame percentiles in the existing vector rather than copying it
  three times per CSV row. `nth_element` only reorders samples, so subsequent
  percentile selections still see the same values.
- Stop storing frame samples when the CSV worker is inactive; continue updating
  the frame-time moving average used by request admission.
- Remove unreferenced readiness `lastState` and configuration `baseDir` fields.
- Remove unused `Fingerprint.cpp`, `Fingerprint.h`, and `fingerprint_exe.py`.
  CMake did not build the old verifier, and the script advertised an INI key
  the runtime no longer reads. `TargetValidation` and hook checks remain active.

## Remaining findings and limits

1. **Hook authentication is incomplete.** Request and the recognized HOODLUM
   redirect use signatures. Ordinary Load and Archive entries still rely on
   executable-image checks and a short opcode exclusion list. This cannot
   authenticate every executable revision or detect every third-party patch.
   Additional verified signatures are needed before extending compatibility.
2. **Readiness is a sampled, biased measurement.** Requests already loaded by
   the time the queue drains are ignored. Channel transition times are sampled
   at scheduler cadence. Request timestamps improve timing for tracked loads,
   but CSV readiness totals and percentiles do not cover all vanilla requests.
3. **Runtime behavior remains unverified.** The inspected game CSV had only
   its header. Its adjacent log showed mandatory hooks active and archive
   profiling inactive, and lacked the diagnostic checkpoints present in this
   source. This is consistent with an older deployed build; it does not prove
   the current callback fallback solves the empty CSV.
4. **Write failures are silent.** The CSV worker breaks out of `writeAll` on a
   disk error without exposing a persistent error status. Shutdown also waits
   for the worker. Disk-failure handling and shutdown timing need dedicated
   runtime testing.
5. **Further allocation reduction needs profiling.** The adapter ranks and
   deduplicates candidates, then the controller repeats that work for its
   generic interface. Reusing scan buffers or tightening that interface could
   reduce allocations, but scan/admission costs should be measured first.
   No measured CPU/FPS gain is claimed for this review.
6. **Old CSV totals are not comparable to new totals.** Previously the `issued`
   and dynamic totals missed intervening ticks. `dynamic_deferred` now sums
   deferred candidate opportunities across ticks; it is not a unique-model
   count. `loaded`, `outstanding`, `candidates`, and `dynamic_eligible` remain
   latest-tick gauges.

## Validation

- Baseline: VS 2026 Win32 Release ASI linked; all four CTest suites passed.
- Patched: VS 2026 Win32 Release ASI linked against the installed Plugin-SDK
  and static MinHook; all four CTest suites passed with expanded controller
  regressions and interval-counter checks.
- Existing four-producer ring stress and archive sampling tests passed.
- `python -S tools/audit_separation.py` passed.
- Build warnings came from Plugin-SDK headers; the linker also reported an
  automatic `/LTCG` restart for the SDK library.
- No GTA launch, hook execution test, benchmark, or sanitizer run was performed.

For a game check, use the rebuilt ASI with the desired INI and inspect the log
for scheduler, streaming-ready, and first-sample checkpoints. Exercise long
loads, teleports, pause/resume, interiors, and shutdown before benchmarking
matching routes. The project's existing `docs/TEST_PLAN.md` describes broader
gameplay checks.
