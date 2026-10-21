# v25: inspect actual native fading and live OLA arrays

The user reports no visible improvement with v24. Its submission and list counts
do not contradict that observation and must not be described as a visual fix.

Review of the installed OLA configuration found `VisibleEntityPtrs=unlimited`
and `VisibleLodPtrs=unlimited`. OLA relocates those arrays and patches game-code
operands whenever they grow. Plugin-SDK's array pointers remain at the old stock
addresses. v24's final-list observer read those old arrays and truncated counts
to 1000. Its reported opaque membership and absent counts were unreliable.

v25 resolves each current array from three independently agreeing native code
operands, checks readability for the full reported count, and rejects the stock
base if the count exceeds its stock capacity. It performs read-only observation;
no renderer array is replaced. If resolution fails, telemetry says the opaque
arrays are unverified. `not observed` must not be interpreted as confirmed loss
when either array could not be verified.

OLA pointer references used:
- Visible entities: 0x553529, 0x553944, 0x553B03.
- Visible LODs: 0x5534F5, 0x553923, 0x553CB3.

Reference:
https://github.com/GTAmodding/III.VC.SA.LimitAdjuster/blob/master/src/limits/EntityPtrs/InVisibleEntityPtrs.cpp

New optional native observers:
- CalculateFadingAtomicAlpha (0x732500): captures the unchanged native result
  for modified map models beyond 300 units, counts zero alpha, and records up
  to eight distinct model IDs with original/applied distances and model alpha.
- RenderFadingAtomic (0x732610) and RenderFadingClump (0x732680): count actual
  native fading-draw routine entries and entries receiving positive alpha.

These hooks preserve the original rendering functions, arguments and return
values. They do not force alpha, bypass LOD/time/occlusion checks, or overwrite
an existing foreign jump. Failure to install a probe leaves the core renderer
active and reports that the measurement is unavailable.

This is a diagnostic build for the unresolved visual issue, not a claim that
the visual issue is fixed. Positive-alpha routine entries still do not prove
GPU pixels appeared. The next log's `Map draw distances FADE` lines distinguish
native fading rejection from list insertion alone and supply concrete model IDs.

Only distantstreaming was edited. MSVC Win32 Release build completed; no tests
or in-game validation were run. Game files were not changed.
