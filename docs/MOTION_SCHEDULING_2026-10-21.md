# Motion-sensitive scheduling — 2026-10-21

The primary profile now has three predictive scheduling tiers:

| Situation | Nominal predictive interval | Camera request scan |
| --- | --- | --- |
| Stationary, unchanged scene | 250 ms idle scan interval | Existing 100 ms interval |
| Ordinary motion/demand | Existing 40 ms controller gate | Existing 100 ms interval |
| Fast travel | 20 ms controller gate | Existing interval unless the view also changes rapidly |
| Rapid turn or camera translation relative to player | 20 ms controller gate | 40 ms |

These are minimum intervals, not timers that create additional game frames.
Actual work runs on the next eligible callback and remains limited by
`General/UpdateIntervalMs` (20 ms in the primary profile), frame rate and
existing slow-work backoff. Slower custom profiles retain their scheduler limit.

The detector triggers at 35 world units/second for player movement, 120
degrees/second for camera turns, or 60 world units/second for camera translation
relative to the player. Each trigger holds its boost for 400 ms; continued
motion refreshes the hold. Ordinary camera follow movement does not turn fast
travel into a separate camera boost. Teleports beyond the existing threshold,
invalid samples, pauses, interiors, loading and sample gaps over 500 ms reset
the detector. Camera cuts receive a bounded new-view boost without being used
as player travel prediction.

Fast travel visits the predicted endpoint sector before the player's current
sector. A concurrent camera boost keeps the current sector first and weights
visible-facing model candidates more heavily. Nearby urgent geometry retains
its existing priority. Sector/node counts, candidate limits, prediction clamps
and memory guards are unchanged. No game pointers or entity lists are cached.

Frame-time, native backlog, native/physical/process/GPU memory guards and slow
controller/camera work disable the boost immediately. Slow camera work inhibits
boosts for the configured `FrameStability/SlowControllerRetryMs` interval;
normal camera readiness retains its existing safety checks. Faster camera
request scans do not accelerate the separate entity-instancing pass, which
retains the configured normal camera interval and existing time/count limits.

All direct DistantStreaming predictive, dynamic-readiness, catalog and camera
DFF/TXD requests share a main-thread budget. Defaults are 240 requests in a
rolling 1000 ms window and 12 burst credits, replenished at 240/second. Both
constraints apply in every tier, so faster scans cannot double the request
ceiling. Camera batches use at most four direct requests (two during fast
travel without a view boost), leaving additional credits for other paths.
When exhausted, predictive collection is skipped while expiry/accounting
continues; the catalog retains its unconsumed hint cursor. Existing outstanding,
per-tick, priority, backlog and memory caps continue to apply.

This budget counts direct plugin calls, including promotions and explicit TXD
requests. GTA's recursive dependency loads, vanilla demand and other mods are
outside it. The budget resets when gameplay/streaming becomes unavailable, so
the rolling ceiling applies to continuous gameplay sessions. It is not a disk
throughput or total native-queue guarantee.

Controls are under `[MotionScheduling]` in `config/DistantStreaming.ini`:

| Key | Default | Accepted range |
| --- | --- | --- |
| `Enabled` | 1 | 0 disables motion boosts; shared request limiting remains active |
| `FastControllerIntervalMs` | 20 | 20–100 |
| `FastCameraIntervalMs` | 40 | 40–100 |
| `BoostHoldMs` | 400 | 100–1000 |
| `FastTravelUnitsPerSecond` | 35 | 5–200 |
| `FastCameraUnitsPerSecond` | 60 | 10–400, relative to player |
| `FastTurnDegreesPerSecond` | 120 | 30–720 |
| `RequestsPerSecond` | 240 | 1–256 |
| `RequestBurst` | 12 | 1–16 |

The existing one-second log now reports active travel/view boosts, available
request credits and cumulative issued/rejected budget consumptions. Thresholds
and rate limits are starting defaults rather than measured optimal settings.
Under dense new demand, the shared cap can defer optional work; raise it only
when native backlog and frame-time measurements justify doing so.

Original sources and packaged artifacts are under
`.review-backups/2026-10-21-motion-tier/`. Release ASI/helper compilation is the
validation for this pass. No tests, benchmark or game launch were performed.
