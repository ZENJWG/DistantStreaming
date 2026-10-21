> Updated behavior: see [startup ownership and protection](STREAMING_STARTUP_OWNERSHIP.md). The frame-only enforcement described below is historical; native reads now use an ASC-owned budget from startup.

# Native streaming budget: configurable, no 1024 MiB clamp (v4)

The 1024 MiB hard-coded native streaming limit has been removed.
`StreamingMemoryLimitMiB=2000` now requests up to 2000 MiB, subject to
actual free process VA, configured `AddressSpaceReserveMiB`, executable LAA,
Windows process address-space limit, and the GTA streaming byte counter's
unsigned 32-bit representability. `0` means no *user* limit, not unlimited
32-bit process memory. This change does not affect the independent x64 backing
cache or HD texture cache limits.

The default bundled INI requests 2000 MiB and reserves 384 MiB of VA.
This changes only the streaming budget scalar, not immediate allocation.
Other GTA mods, fragmentation, allocator behavior and high live usage can
still cause errors even if the arithmetic safely fits in the field.
Game-runtime validation is still required.

---

# LAA-aware streaming memory budget

## Configuration

```ini
[Memory]
ManageStreamingMemory=1
StreamingMemoryLimitMiB=0
AddressSpaceReserveMiB=256
MaxWorkingSetMiB=0
MaxPrivateMiB=0
MinPhysicalAvailableMiB=256
```

- `StreamingMemoryLimitMiB=0`: unlimited by user configuration. Runtime
  process/address-space limits still apply. Positive values from 64 through
  4096 impose a cap in MiB, e.g. `1024` requests at most 1 GiB for streaming.
- `AddressSpaceReserveMiB`: free virtual address space retained for transient
  allocations and other game/mod work; allowed range 64–1024, default 256.
  This is a budget calculation, not an actual address-space reservation.
- `MaxWorkingSetMiB` / `MaxPrivateMiB`: optional fixed guards for this plugin's
  speculative requests. Zero disables those fixed caps; virtual-address and
  physical-memory checks remain. Explicit limits in an older INI are honored,
  including the old 1400/1600 values. They do not directly limit vanilla loading.
- `ManageStreamingMemory=0`: leave the native budget unchanged. Restart the
  game after changing settings; live INI reload is not implemented.

The updated default INI enables the manager with a 2000 MiB user cap and
disables the old fixed process guards. Invalid memory settings disable the
plugin, consistently with other invalid configuration.

## Calculation and cost

Read `IMAGE_FILE_LARGE_ADDRESS_AWARE` from the loaded main EXE's PE header
once, after existing target validation. Do not inspect the ASI's own flag.
Use `GlobalMemoryStatusEx.ullTotalVirtual` for the actual OS-imposed process
limit, clamped to 2 GiB without LAA or 4 GiB with LAA. This also handles LAA
on 32-bit Windows where the available user address range may be smaller.

Once per second, compute using 64-bit arithmetic:

```text
process ceiling = min(Windows total process VA, LAA ? 4 GiB : 2 GiB)
address ceiling = process ceiling - configured reserve
growth ceiling  = current streamed bytes + free process VA - configured reserve
user ceiling    = configured MiB, or address ceiling when configured as 0
stream budget   = min(address ceiling, growth ceiling, user ceiling, UINT32 safe ceiling)
```

Subtractions saturate. Clamp the result to at least 64 MiB, then write a
64 KiB-aligned nonzero unsigned byte count. GTA's field cannot represent
exactly 4 GiB: converting that byte count to 32 bits would produce zero.
The configured reserve and other allocations normally produce a substantially
smaller budget. For example, 100 MiB streamed plus 3500 MiB free VA with the
default reserve produces at most 3344 MiB, before any explicit user cap.

At severe address exhaustion, keep a 64 MiB minimum native budget rather than
collapsing it toward zero: vanilla subtracts incoming file sizes from this
unsigned budget. The minimum is a functional floor, not an allocation or a
guarantee of available memory. Speculative requests are stopped by the VA
guard in this condition. Individual resources larger than a configured budget
and fragmented heaps still need appropriate game/mod configuration.

The budget may shrink as other allocations grow. The manager does not force
an immediate unload; vanilla streaming reacts to the adjusted budget during
its normal work. Fixed caps are also budgets, not hard instantaneous limits
on live allocations or total process memory. Streaming byte accounting is
an approximation of allocation cost; fragmented heaps and large contiguous
allocations can still fail before the reported free VA reaches zero.

Reuse the scheduler's existing Windows memory sample. The added hot-path
cost is constant arithmetic, with no heap allocations, per-page walks,
extra worker, or extra memory-system query. Native budget writes occur only
when the calculated value differs, at most once per second. Low-address-space
admission checks use each scheduler sample. If the Windows query fails, leave
the current native budget intact and suspend this plugin's speculation until
measurements recover. After streaming shutdown/reinitialization, recalculate
the budget on the next initialized tick.

Do not enable another mod's budget controller at the same time: this manager
reasserts its calculated budget on its next update. Management is active only
after the existing target checks and mandatory hook installation succeed.
The manager does not change the game's model-ID limits or allocator design.

## Visibility and verification

The log records EXE LAA status, configured cap and reserve, and the first
applied budget and OS address limit after streaming initialization. CSV
`stream_budget_mib` reports the current native budget. Four appended columns
report `exe_laa`, `virtual_total_mib`, `virtual_available_mib`, and
`address_space_guard`. Existing column names retain their meanings.

An earlier release passed Windows tests; v4 has not been validated in-game or built on Windows. The new
suite covers LAA/non-LAA ceilings, 2/3/4 GiB OS limits, explicit and unlimited
caps, exhausted address space, 32-bit overflow prevention, invalid settings,
budget invariants over a range of inputs, and speculative admission with the
old fixed caps disabled. `tools/audit_separation.py` permits native budget
writes only in `StreamingMemoryWin.cpp`; other original separation checks
remain active. Neither game-runtime stability nor an FPS gain has been tested.

A read-only inspection of `h:\GTASAtz2\gta_sa.exe` found its LAA flag set.
Runtime detection independently checks the loaded executable on every launch.

In game, compare LAA and non-LAA startup logs, positive and zero caps, high
memory use, loading/save transitions, and long travel sessions. Keep an eye
on free VA as well as private memory. The initial budget log may differ from
later CSV budgets because the amount of available address space changes.

Windows references:

- [Process memory limits](https://learn.microsoft.com/en-us/windows/win32/memory/memory-limits-for-windows-releases)
- [MEMORYSTATUSEX virtual-memory fields](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/ns-sysinfoapi-memorystatusex)
