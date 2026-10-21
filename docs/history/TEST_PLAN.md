# VS 2026 Win32 and GTA San Andreas test plan

## 1. Build and identity
- [ ] `python -S tools/audit_separation.py` passes.
- [ ] CMake >= 4.2; VS 2026 32-bit target, v145.
- [ ] Plugin-SDK 32-bit library and MinHook compile/link.
- [ ] `ctest --test-dir build-vs2026-win32 -C Release --output-on-failure`.
- [ ] Verify vanilla exe by independent reliable source (CRC alone is insufficient).
- [ ] Empty/wrong exe CRC fails closed.
- [ ] Unmodified hook code address checks pass.
- [ ] Archive selection profiling off by default.
- [ ] ASI shutdown cleanly flushes buffered data.

## 2. Functional gameplay
- [ ] Telemetry-only mode: no speculative requests.
- [ ] Los Santos, San Fierro, Las Venturas, rural roads, highway.
- [ ] High speed car/bike/aircraft, camera 180 degree spins, stationary turns.
- [ ] Interior enter/exit, cutscenes, missions, save/load/restart.
- [ ] Teleport and invalid/NaN data fail safely.
- [ ] No forced removal/eviction or mission flag changes; managed budget
      respects configured cap, EXE LAA flag, actual OS limit, and VA reserve.
- [ ] Test zero/positive streaming caps, non-LAA/LAA EXEs, and memory query failure.
- [ ] Streaming shutdown/reinitialization reapplies the configured budget.
- [ ] Stable 60-minute session with no uncontrolled private memory growth.
- [ ] Models loaded for timed/night features are correct at transitions.
- [ ] Nearby missing buildings receive bounded asynchronous priority, yielding
      to existing native priority requests; validate with priority disabled too.
- [ ] Local LOD placeholders and detailed buildings appear on the same route.
- [ ] A wanted model can reload after unloading without a completed-load cooldown.
- [ ] Compare camera catch-up with `BlockingLoad=1` and `0`; measure loading stalls.
- [ ] Pending visible models/TXD dependencies catch up and loaded map entities
      receive render objects, without altering mission flags or hidden/time models.
- [ ] Experimental local ped/vehicle readiness tested *separately*.

## 3. Performance
- [ ] Record at least 3 repeat runs per condition and route.
- [ ] Baseline: vanilla with plugin absent; placebo: telemetry-only.
- [ ] Enabled static prediction; separately compare optional dynamic readiness.
- [ ] Independent frame time tool for accurate p99 and 1% low.
- [ ] CSV `lost_events` and `dropped_log_lines` remain zero.
- [ ] Startup reports per-model coalescing; no observation-overflow reset spam.
- [ ] Repeated traffic increments `coalesced_requests`, preserving learning
      and promotions. Unsupported resource IDs increment their separate count.
- [ ] Request-to-LOADED p50/p95 only indicates readiness, not spawning.
- [ ] Monitor CPU spike cost from optional archive-chooser hook.
- [ ] Check memory with both stream budget ratio and process private memory.

## 4. Rollback
- [ ] Remove `DistantStreaming.asi` to disable all behavior.
- [ ] Never attempt live hot-unload while hook callbacks are active.
- [ ] Disable optional features individually on the first error.
