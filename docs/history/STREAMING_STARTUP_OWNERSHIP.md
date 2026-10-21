# Streaming budget startup ownership

Memory management now starts from `initRwEvent.before`, before
`CStreaming::Init` and first-game asset loading. Only config, logging, target
validation and memory ownership move to that event. Other hooks and the
scheduler retain their frame-callback startup. A late-loaded ASI can initialize
from its first callback, but cannot affect loading that already happened.

Configuration immediately samples Windows memory, calculates the existing
LAA/VA-aware budget, and writes the public field at `0x8A5A80` and the startup
instruction's immediate at `0x5B8E64 + 6` (the application point used by OLA).
The startup instruction must match `C7 05 <0x8A5A80> imm32` before it is patched.
Initial application does not require initialized streaming, a player,
model-array bindings, or successful request-hook installation.

With `[Memory] ManageStreamingMemory=1` and
`DistantStreamingOwnsNativeBudget=1`, the budget reads in GTA's original
`MakeSpaceFor` use a separate ASC-owned scalar. Original eviction instructions
and control flow remain intact. Direct writes to the public field cannot
change the native eviction budget between callbacks. An OLA overwrite of the
startup immediate also cannot change that private budget. This mechanism
works independently of ASI names and the order of shared-field writes.

The public field stays writable for compatibility. ASC mirrors its budget there
at game/draw boundaries. No shared data page is made read-only, no exception
handler traps unrelated writes, and no other mod's binary is patched. A mod's
private decisions, including forced unloads, remain under that mod's control.

ASC snapshots, admission checks and world preload use the effective budget,
avoiding temporary foreign values in the public field. The calculation still
updates at most once per second, retaining the existing LAA, virtual address
reserve and 32-bit representation limits. Loading transitions retain the last
valid protected budget until the next calculation.

`DistantStreamingOwnsNativeBudget` defaults to `1` when omitted. Explicit `0`
retains the OLA ownership opt-out and does not install private read protection.
Disabled config, `ManageStreamingMemory=0`, and passive `HookMode=0` leave
memory management inactive.

## Compatibility and diagnostics

The read patch targets GTA SA 1.0 US `MakeSpaceFor` (`0x40E120..0x40E170`). It
accepts only absolute 32-bit MOV reads of the original budget address. It
validates every candidate before patching and refuses an existing entry detour
or unrecognized budget reference. This blocks shared-field overrides; another
plugin replacing native eviction or rewriting these instructions afterward
needs separate compatibility work.

Successful startup logs `ASC BUDGET PROTECTION: redirected ...` and
`Streaming memory applied (startup/loading/runtime): ... startup immediate=applied`.
An unsupported function logs `ASC BUDGET PROTECTION UNAVAILABLE`, meaning only
startup and frame-boundary enforcement remain. `startup immediate=unavailable`
separately reports an unsupported or unwritable initialization instruction.
A failed Windows query retains the last valid budget and retries through
normal updates. Shutdown restores operands and the startup immediate only
when their current values still belong to ASC.

## Upstream implementations inspected

- [OLA MemoryAvailable.cpp](https://github.com/GTAmodding/III.VC.SA.LimitAdjuster/blob/master/src/limits/Misc/MemoryAvailable.cpp):
  patches the initialization immediate at `0x5B8E64 + 6`.
- [Improved Streaming PC source, SAMPTools fork](https://github.com/SAMPTools/ImprovedStreaming/blob/main/PC/LoadWholeMap/LoadWholeMap.cpp):
  writes `CStreaming::ms_memoryAvailable` during script processing, then raises
  it to `streamMemoryForced` on subsequent script ticks.
- [MixSets.cpp](https://github.com/JuniorDjjr/SA-MixSets/blob/master/MixSets/MixSets.cpp)
  repeatedly writes `G_StreamMemory` to `0x8A5A80`;
  [ReadIni.cpp](https://github.com/JuniorDjjr/SA-MixSets/blob/master/MixSets/ReadIni.cpp)
  also writes it when loading configuration.
- [gta-reversed Streaming.cpp](https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/Streaming.cpp):
  native eviction reads the budget in `MakeSpaceFor`; initialization and
  stream.ini handling write the shared field.

A specific public repository for a mod named "Proper Streaming" was not
identified. Versions writing the same shared field are covered by the same
mechanism. Replaced native eviction implementations are outside its guarantee.

Win32 Release compilation was performed. No tests or in-game validation were run.
