# Forced camera catch-up — 2026-10-15

This revision implements the requested stronger camera-visible loading path.
It deliberately extends the previous asynchronous-only policy. It does not
add `MISSION_REQUIRED` or clear another system's mission flags.

## Source check

Checked the installed Plugin-SDK declarations and the GTA reversed
[streaming implementation](https://raw.githubusercontent.com/gta-reversed/gta-reversed/master/source/game_sa/Streaming.cpp),
[renderer](https://raw.githubusercontent.com/gta-reversed/gta-reversed/master/source/game_sa/Renderer.cpp),
and [entity implementation](https://raw.githubusercontent.com/gta-reversed/gta-reversed/master/source/game_sa/Entity/Entity.cpp).

- Mission-required flags retain models; they do not provide a load-completion
  barrier. Mission release can affect a model's texture dependency too.
- Priority requests can promote queued models. An initially unloaded model
  requests its texture dependencies; an already-queued model takes a different
  path, so this module also promotes its pending texture dependencies.
- `LoadAllRequestedModels(true)` is a synchronous priority-load barrier that
  also flushes shared streaming channels. It is not a time-bounded API.
- Loaded model data and an entity's render object are distinct. Native
  `CreateRwObject` performs normal instance setup and reference registration.

## Active defaults

```ini
[CameraReadiness]
Enabled=1
BlockingLoad=1
InstanceLoaded=1
IntervalMs=100
RangeUnits=300
HalfAngleDegrees=70
MaxModelsPerBatch=8
MaxInstancesPerBatch=32
MaxBatchSectors=16384
```

The shipped default **enables blocking catch-up**, as requested. The loader
can pause gameplay while it reads/converts textures and models. A batch-size
limit is not a duration limit: active shared channels, other priority work,
large TXDs and their parents can make a single flush take much longer than
100 ms. Neither a constant frame rate nor literal instantaneous I/O is
guaranteed. Use `BlockingLoad=0` for priority requests without waiting, or
`Enabled=0` to disable this entire path. Restart GTA after changing settings.

The 100 ms interval is measured from completion, so a slow batch does not
immediately trigger another batch. The range is in game units, and 70 degrees
is the cone half-angle. A native bounding-volume screen test further narrows
selection to the view. This is a view/frustum test, not proof of unobstructed
visibility or an instruction to bypass renderer occlusion.

## Runtime behavior

1. Check normal gameplay, valid camera/player, exterior area, stream-budget
   occupancy, process limits, available physical memory, and VA headroom.
   Blocking mode intentionally permits catch-up despite an existing backlog
   and slow-frame feedback; otherwise the queued visible work can remain stuck.
   Nonblocking mode respects the ordinary backlog cap.
2. Scan a camera-forward sector capsule and local LOD lists. Front-facing
   cells are visited first, with the camera's own cell retained for close walls.
   One scan visits at most 3,072 building/dummy nodes and 1,024 LOD nodes.
3. Keep visible, current-time static building/dummy entities in the cone and
   native screen bounds. Pedestrians, vehicles, interiors, explicitly hidden
   entities and do-not-stream entities are excluded. Already-instanced entities
   do not need catch-up work.
4. Select up to eight nearest distinct missing world models. Existing queued
   models participate; they are not rejected by the prefetch-only CanRequest
   rule or a stale speculative cooldown. Keep ordinary flags and add priority.
5. Promote pending TXDs and parent TXDs for those selected models. The explicit
   parent walk checks pool bounds and uses a bounded, cycle-detecting walk of
   up to eight vanilla TXD slots per selected model. These dependencies are
   additional to the world-model selection count. The plugin still depends on
   valid native game resource tables and native dependency processing.
6. In blocking mode, call the native priority loader once when selected
   missing work exists, including when its priority flag was already set.
7. Re-scan after loading, retaining no entity pointers across the loader.
   Create at most 32 render objects for loaded eligible map entities. No new
   world entity is allocated/spawned, renderer LOD rules are not overridden,
   and native creation handles model references and streaming registration.

`MaxBatchSectors` is an archive-sector cost hint (2,048 bytes per sector).
It excludes texture dependencies and is neither RAM accounting nor a time
budget. One oversized nearest model may proceed alone instead of being
starved forever. The priority loader can complete work beyond the selected
world batch because its channels and priority queue are global.

The module runs on the scheduler/game thread with a reentry guard. Buffers
are reserved once, and no entity pointers are retained between ticks.
Native loader/instancing entry pages must be executable; startup logs their
availability. An unavailable loader falls back to asynchronous promotion;
an unavailable instance entry disables explicit instancing. These page checks
are not an independent executable-version authentication mechanism.

The source separation audit now permits the synchronous loader only in
`CameraReadinessWin.cpp`, reflecting this explicit feature request. Budget
writes remain confined to the memory manager; other separation checks remain.

## Telemetry and validation

The startup log records `Camera catch-up`, the active blocking switch and
function availability. Five appended CSV columns report interval totals:

- `camera_requests`: model and explicit texture priority request calls;
- `camera_flushes`: native blocking loader calls;
- `camera_block_total_us`: total measured native loader duration;
- `camera_block_peak_us`: peak native loader duration in the interval;
- `camera_instances`: successfully created render objects.

The analysis script summarizes these values. They complement frame-time
statistics; a new run can show the actual stall/readiness tradeoff.

VS 2026 Win32 Release compiles against the installed Plugin-SDK. All eight
CTest suites and the source audit pass. New portable tests cover cone geometry,
camera bounds, nearest/deduplicated/cost-limited batches, queued priority
catch-up, texture-parent traversal bounds/cycles, blocking-call sequencing,
the asynchronous switch, cadence rollover, and gameplay/memory admission.
These do not execute GTA's loader, native instancing, or GPU code. Gameplay
stability, appearance latency and hitch duration remain unverified.

Install the rebuilt ASI and INI while GTA is closed. Compare the same route
with `BlockingLoad=1` and `0`, including stationary camera turns and movement.
Keep the new CSV so loading stalls and actual catch-up work can be assessed.
