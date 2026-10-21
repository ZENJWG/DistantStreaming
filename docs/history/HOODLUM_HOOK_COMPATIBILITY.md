# Hook compatibility test plan

This version targets **the uploaded GTA SA Classic 1.0 US HOODLUM executable**.

## Observations from 2026-10-15 logs

| Hook | Runtime VirtualQuery | Static bytes in uploaded exe |
| --- | --- | --- |
| Request 0x4087E0 | `MEM_COMMIT`, `PAGE_READWRITE`, `MEM_IMAGE` | `53 8B 5C 24 0C 55 8B 6C 24 0C 56 57` in executable `.text` |
| Load 0x40E3A0 | `MEM_COMMIT`, `PAGE_EXECUTE_WRITECOPY`, `MEM_IMAGE` | `E9 FB 8C 15 01`, jumping to `0x015670A0` in `.HOODLUM` |
| Archive 0x408E20 | not logged | optional |

Both mandatory hooks were skipped. `DistantStreaming.csv` contained only its
column header, proving nothing about performance because no scheduler samples ran.

## Runtime safeguards and expected messages

1. If Request code bytes are changed, log `SKIPPED (unknown Request entry bytes)`.
   Do not change page protection.
2. If Request page has unexpected protection, log `SKIPPED (unsupported page protection)`.
3. If Request is RW but has the verified signature in an executable PE section,
   log `RECOVERED (verified Request code; RW -> RX)` after `VirtualProtect` succeeds.
4. If Load entry has the exact HOODLUM E9 and its destination is a committed
   executable section of the same process image, log `eligible (verified HOODLUM E9...)`.
5. An unrecognized E9 must log `SKIPPED (unknown detour/invalid entry)`.
6. If MinHook fails on the known redirect, report `MH_CreateHook failed status=...`
   and leave core streaming disabled. Do not rewrite the `.HOODLUM` trampoline.
7. If both mandatory hooks install, expect `DEGRADED` (optional archive skipped)
   or `FULL` in `DistantStreaming.log`; `DistantStreaming.csv` should then gain
   data rows after gameplay has been active for a few seconds.
8. Repeat with other ASIs installed and verify there is no crash and that
   unrelated hooks are unaffected. The exact flags/runtime detours must be
   evaluated in each deployment; static inspection cannot guarantee them.

## Build and use

Run `build.bat` on Windows with Visual Studio 2026 Win32 dependencies.
Install `dist/DistantStreaming.asi` and `dist/DistantStreaming.ini`, replacing
the previous ASI. Remove or rename old `DistantStreaming.log` and `.csv`,
then start a gameplay session. Save both logs from the same run for triage.

## HOODLUM destination failure follow-up

A subsequent report showed `Request` recovered, but `Load` stopped at
`HOODLUM redirect destination invalid`. The new installer logs the destination
`VirtualQuery` state, protection and type.

For the supplied binary, `0x40E3A0` contains `E9 FB 8C 15 01`, targeting
`0x015670A0` in `.HOODLUM` (executable section). The first 16 disk bytes at
the target are `A0 58 4A 8E 00 84 C0 74 09 33 C0 A3 34 55 96 00`.
These exact bytes must match *live* memory before a destination page permission
recovery. Recovery is allowed only for a committed `MEM_IMAGE` page with
protection exactly `PAGE_READWRITE`. It is not allowed for unrelated mods,
unknown redirects or changes to executable code.

If still disabled, send the full new `DistantStreaming.log` so its detailed
destination diagnostics distinguish inaccessible memory, changed code,
unexpected protection, and MinHook failure.
