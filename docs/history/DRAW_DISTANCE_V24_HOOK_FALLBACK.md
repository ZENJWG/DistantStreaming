# v24: optional output correction must not disable the extension

The supplied v23 log contains:

`Extended world scan DISABLED: native renderer entry or polygon scanner not verified as executable, expected game image.`

There is no ACTIVE line or final-list telemetry. The renderer extension never
started in this run. v23 had made the newly added SetupEntityVisibility entry
(0x554230) mandatory, and rejected the whole extension if any target failed.
The generic rejection did not identify the offending address. The earlier v22
session successfully installed the original three required hooks.

v24 restores those three as the required set. SetupEntityVisibility output
correction is optional; a second optional AddEntityToRenderList correction
repairs the distance at the insertion boundary when that entry is available.
Supplemental normal scanning also repairs its own returned distance directly,
so its correction works even when both optional entries belong to another ASI.
An unavailable optional target no longer disables discovery and LOD ordering.
Existing third-party jumps are still not overwritten or chased for hooking.

Rejected targets now log their individual address, memory protection, leading
bytes and (for an E9 jump) destination/module. This distinguishes a non-executable
page from an existing renderer hook, instead of guessing from a generic message.
Optional hooks are removed on shutdown only if this manager installed them.

The latest session also used GlobalMultiplier=1.2, versus 10.2 in v22. It is a
much smaller model-distance change. Camera/scan coverage of 5400 does not force
short-range models to draw at that distance; their model multipliers still apply.

Validation: MSVC Win32 Release build only; no tests or in-game run. Only the
distantstreaming project was edited. No game files were changed.
