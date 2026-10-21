> Updated behavior: see [startup ownership and protection](STREAMING_STARTUP_OWNERSHIP.md). The frame-only enforcement described below is historical; native reads now use an ASC-owned budget from startup.

# OLA and DistantStreaming native budget ownership — v3

**Scope:** Fix budget contention without disabling OLA, native texture interception,
mesh-buffer interception, HD rendering, or predictive streaming.

## What the original log proves

The initial log reports `ASC BUDGET OWNER ... MiB=2000, writes=123662` while
OLA is loaded. The log does **not** identify the source of the other writes.
They could be OLA, the game engine, or another ASI. The large count is not
proof OLA changed the budget on every frame.

## Complementary fix: align OLA's initial budget

OLA exposes `[SALIMITS] MemoryAvailable`, in **MiB** (or % when suffixed by `%`).
Set the number to the same native budget as ASC:

```ini
; III.VC.SA.LimitAdjuster.ini
[SALIMITS]
MemoryAvailable = 2000
```

Retain all other OLA limit and pool settings. This value is **not** the x64
native texture backing budget. Do not set it to 6144.

For an explicit, backed-up edit while GTA SA is CLOSED:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\sync-ola-budget.ps1 -GameDirectory "C:\GTA San Andreas" -BudgetMiB 2000
```

The script finds exact-name `III.VC.SA.LimitAdjuster.ini` files under the
game directory (including modloader), edits only `[SALIMITS] MemoryAvailable`,
and saves each original to `.asc-original.bak` on the first run. It does not
modify OLA binaries or disable OLA. Check the listed paths before restarting.

## Frame-boundary enforcement

`StreamingMemoryWin.cpp` continues to compute the safe budget once each
second. The new `Reassert()` compares a cached, validated target budget with
`CStreaming::ms_memoryAvailable` at *game process* and *draw* callbacks, and
restores it only when they differ. It does no costly memory query, no model
table access, no detours and no OLA code patching.

When it detects another write, it logs:

```
ASC BUDGET REASSERT: phase=..., observed MiB=..., target MiB=..., external changes=..., total writes=...
```

This is evidence that **something** changed the field, not proof that OLA
was the writer. A frame-boundary check cannot prevent a different plugin from
writing later in the same frame. Setting OLA's startup value equal to ASC's
avoids the most obvious initial disagreement without pretending to guarantee
exclusive process-wide ownership.

## Validate

1. Ensure ASC config has `ManageStreamingMemory=1`,
   `DistantStreamingOwnsNativeBudget=1`, `StreamingMemoryLimitMiB=2000`.
2. Synchronize OLA INI as above; restart GTA SA completely.
3. Build/install the Win32 `DistantStreaming.asi` and x64 helper using
   `build.bat`, avoiding multiple copies of the ASI.
4. Confirm budget logs report 2000 MiB when virtual memory headroom permits.
5. Observe whether `ASC BUDGET REASSERT` appears repeatedly. If so, another
   writer is still active; correlate with OLA/other streaming mods.

**Unverified:** No Windows/GTA runtime test has been executed here. This
source change does not resolve the pre-existing `native_d3d_resources`
segmentation fault; run the full CTest suite before shipping.


## v4 budget note

The previous 1024 MiB compiled clamp was removed from `Calculate()`.
The enforced target is the configured value constrained by VA headroom and
32-bit-counter representability. OLA table relocation and model limits are unchanged.
