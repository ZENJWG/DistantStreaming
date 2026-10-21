# v22: model range controls scan coverage

The supplied v21 session used a 600-unit normal scan, an 1100-unit camera target,
and a 1024 MiB free-VA cutoff. Those settings restricted the practical effect of
the enlarged model distances even though the child-before-LOD order was active.

Changes:

- Normal scan range is the greater of `WorldScanRangeUnits` and
  `MaxDistanceUnits * TheCamera.m_fLODDistMultiplier`, bounded to 10000 units.
  The former is now a range floor, not a ceiling on the model-distance policy.
- Camera extension covers that range and, if enabled, the far-LOD range.
  A legacy `CameraFarClipUnits=1100` no longer truncates a 3000-unit model scan.
  The scan still obeys the actual live RW far plane.
- Normal scan, camera and far-LOD configuration now support 10000 units instead
  of the former 1200/2500 clamps. The LOD planner accepts the same range.
- Multipliers accept up to 100, so 10.20 is no longer silently reduced to 10.
  `MaxDistanceUnits` remains the explicit cap on modified model distances.
- Free VA below `ExtraScanMinFreeVaMiB` reduces the extra request budget to one
  quarter (minimum one request), instead of blocking requests and instantiation.
  A zero threshold disables this optional throttle. Defaults and project INIs
  use zero. Native streaming memory accounting and eviction remain in charge.
- The project INIs use 3000-unit scan/camera/LOD targets. Existing game INIs
  with smaller scan/camera settings also benefit from automatic model coverage;
  an existing nonzero VA threshold becomes a soft throttle.

The v21 child-before-LOD ordering is retained. Render-list headroom, per-frame
entity/request/submission budgets, request cooldowns, time-of-day, occlusion,
and model residency checks remain. These changes do not enlarge GTA's 32-bit
address space or its render arrays; greater coverage can increase memory use.

Logs identify `ACTIVE v22`, `effective normal scan range`,
`VA-pressure throttled frames` and `extra request budget`.

Validation: MSVC Win32 Release build only. No tests or in-game run.
Only the distantstreaming project was edited; game files are untouched.
