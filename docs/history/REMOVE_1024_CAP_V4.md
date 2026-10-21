# v4 — remove hard-coded 1024 MiB native-stream budget ceiling

## Root cause
`include/asc/StreamingMemory.h` used `SafeNativeStreamingMax=1024*MiB`
inside `Calculate()`, ignoring higher `StreamingMemoryLimitMiB` values.
The user's log reported `configured MiB=2000` but `effective stream MiB=1024`.

## Fix
- Remove the fixed 1024 MiB clamp.
- Limit requested budget by configured cap (or no user cap when zero), LAA
  and Windows VA space, configured reserve, and uint32 byte-counter range.
- Align the budget down to a 64 KiB boundary.
- Shipped example INI now requests 2000 MiB (previously 896 MiB).
- Update OLA synchronization script range and default; preserve OLA binding
  and budget ownership behavior.
- Add regression for configured=2000, LAA=1, free VA=2841 MiB.
- Keep D3D9/native texture and mesh interception paths enabled.

## Local validation
Linux GCC portable CMake build passed; ctest 15/15 passed.
Windows Win32 ASI, D3D9 native-resource test and in-game behavior cannot be
validated in this environment.

## Independent finding from uploaded log
`Native memory: helper failed to become ready; using vanilla resources`:
not caused by the 1024 MiB stream-budget clamp; still requires its own
Windows helper investigation. The patch intentionally does not disable
native-resource features.
