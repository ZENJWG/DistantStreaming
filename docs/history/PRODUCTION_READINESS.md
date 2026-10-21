# Production readiness assessment — ArchiveSelection and DynamicReadiness

## Scope

This revision implements both features and enables them in the sample INI.
**Implementation completeness and portable tests are not equivalent to
production certification.** Windows 32-bit ASI compilation and real game tests
must precede any public "production ready" designation.

## Ownership and engine behavior

| Concern | Policy |
|---|---|
| Vanilla request ordering | Unmodified |
| `GetNextFileOnCd` result and arguments | Unmodified; exactly one original call |
| Profiler hook creation failure | Degrade gracefully; report disabled |
| Profiler overhead | Per-thread 1-in-N sampling |
| Dynamic learning source | Native observed ped/vehicle request, local player position |
| Confidence | At least two nearby requests within history window |
| Dynamic admission | Shared memory/frame/backlog quota, plus per-tick slot |
| Unload / despawn | Vanilla only |
| Model residency / streaming limit | Vanilla only |
| Mission assets and required flags | Never speculatively pinned; filtered from learning |
| LoadWholeMap implementation | Entirely separate |

## Win32 functional test matrix

1. Original GTA SA 1.0 US EXE / ASI loader / startup: no startup crash, no wrong-address hook.
2. Profiler-only operation for 20 min: archive calls, archive samples, bins record. No changes in selected models or loading correctness.
3. Fast highway, air travel, camera rotations: compare no-ASI vs ASI p95/p99 frame time, pop-in, request-to-ready latency, private RAM.
4. Dense traffic: verify dynamic warming on nearby observed models; all admitted models use DEFAULT request flags.
5. Teleport, interiors, cutscenes, missions, death/reload: no residual warming or blocked native requests.
6. Long drive / 60 min endurance: telemetry continues, dropped ring events normally zero, memory stable.
7. Optional archive hook intentionally unavailable (controlled patched/unsupported executable in a development sandbox): archive profiler disabled with log reason; mandatory hook validation still fails closed if incompatible.
8. Shut down from menu: telemetry file flushed, no worker left running or detached during DLL unload.
9. Compare profiling samples 1/16 versus 1/64: difference in profiler overhead and stability.
10. Check repeated demand and per-model cooldown by telemetry dynamic issued and deferred counts.

## Performance gates (proposed; need measurement)

- Zero runtime crashes across the test suite.
- No missing mission assets or regression to native model-dependent scripts.
- No bulk model preloading or forced eviction. The optional LAA-aware
  streaming-budget manager is documented in [STREAMING_MEMORY.md](STREAMING_MEMORY.md).
- No sustained upward trend in process private bytes versus vanilla control.
- Minimal profiler overhead in p95 frame times (<1% target, measurement required).
- No increase in severe visible pop-in; any performance benefit must be benchmarked.
- Request-event ring overflow must reset speculative provenance; overflow requires investigating load / polling frequency.

## Tooling validation run in this environment

- Portable GCC 14 / C++23 core build and CTest.
- Ring stress test, archive sampler test, and dynamic-readiness tests.
- LoadWholeMap separation audit.
- Optional ASan/UBSan portable test run.
- **Not run:** MSVC Win32 game-target compilation, PE hook detouring, game startup, RenderWare and actual gameplay.

This document deliberately does not claim full in-game production readiness.
