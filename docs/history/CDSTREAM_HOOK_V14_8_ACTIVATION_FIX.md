# DistantStreaming v14.8 — CdStream executable-page preflight activation

## Source diagnosis
v14.7 defined `VerifyCdReadHookPage()` but `PatchCdStreamRead()` never invoked it.
The log still reported `CdStreamRead hook creation failed: MH_ERROR_NOT_EXECUTABLE (7)`,
and `cdCalls=0` because no hook was installed.

## Change
- Run guarded executable-page verification/recovery before `MH_CreateHook`.
- Verify ASI detour code is executable (MinHook checks both addresses).
- Preserve fail-closed behavior for unknown executable variants or modified code.
- Do not change the supplied INI, original `build.bat`, native streaming semantics, or SanFrame.

## Diagnostic expectations
On a verified but non-executable HOODLUM entry:
`CdStreamRead target addr=... protect=...`,
then `verified HOODLUM entry recovered RW -> RX`, then (if MinHook succeeds)
`native GTA CdStreamRead hook active`.

If the signature, host executable section, or detour is not verified,
the hook is **not** installed and GTA's original loader remains intact.

## Limits
- Neither the x64 external asset cache nor the CdStream hook raises
  `gta_sa.exe`'s 32-bit virtual-address-space ceiling.
- Helper cache use is not proven until `cdCalls`, `cdEligible`, `cdHits`,
  and `bytesServed` increase in a Windows runtime log.
- Windows MSVC compilation and in-game behavior are not verified here.
