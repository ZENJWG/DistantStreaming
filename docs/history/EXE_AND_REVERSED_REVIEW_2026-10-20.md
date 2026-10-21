# GTA executable and gta-reversed follow-up

## Inputs and scope

Inspected `H:\GTASAtz2\gta_sa.exe` read-only. No executable patch, game launch,
test execution or game-installation deployment was performed. Project edits,
reference downloads, disassembly and release outputs remain under
`I:\ag32gf3g\distantstreaming`.

Reference: gta-reversed revision
`bba3990d59b9494bea52ba5638e5fef50bfe0cb3`, with selected files retained in
`.deps/gta-reversed-review/`. This matters because gta-reversed includes
refactorings and optional fixes; its current C++ signatures are not always
the original executable's ABI.

## Executable identity and checked locations

- Size: **14,383,616 bytes**.
- SHA-256: `F01A00CE950FA40CA1ED59DF0E789848C6EDCF6405456274965885D0929343AC`.
- PE32 / x86, image base `0x00400000`, image size `0x01177000`.
- The Large Address Aware flag is already set. This permits larger x86 virtual
  address space under an appropriate OS; it does not make GTA a 64-bit process.
- HOODLUM section/redirects are present. The following disk layout matches the
  project's expected variant; this is not blanket compatibility certification.

| Location | Inspection result |
| --- | --- |
| `RequestModel`, `0x4087E0` | Expected 12-byte entry prefix matches; DFF ID and flags are cdecl arguments |
| `GetNextFileOnCd`, `0x408E20` | Native entry exists; position argument is unsigned 32-bit |
| `LoadRequestedModels`, `0x40E3A0` | Expected redirect to `0x015670A0`; destination prefix matches |
| `CdStreamRead`, `0x406A20` | Expected redirect to `0x0156C2C0`; newly checked 136-byte body prefix matches exactly |
| `MakeSpaceFor`, `0x40E120` | Expected redirect to `0x4037EB`; stub preserves flags and resumes at `0x40E125` |
| Budget reads | Direct reads at `0x40E144` and `0x40E157` use `0x8A5A80`; consistent with current guarded patching |
| Startup budget, `0x5B8E64` | Expected immediate-store instruction, with original disk value 50 MiB |
| Model-bound operand, `0x40CD58` | Original disk value is 20,000 |
| Model-table operands, `0x40CD67` / `0x546683` | Both identify `0xA9B0C8`; runtime OLA rebinding must still resolve live operands |
| TXD request wrapper, `0x407100` | Adds 20,000 to the TXD slot before forwarding the request |
| `SetModelIsDeletable`, `0x409C10` | Original one-argument function clears bit `0x02`, checks `0x04` / `0x08`, and may call `RemoveModel` |
| Entity create/delete, `0x533D30` / `0x534030` | Original method locations agree with the SDK and reference |

The engine reference agrees with the project's CdStream channel size (`0x30`),
status/file/OVERLAPPED layout, packed 24-bit sector offset and 8-bit archive
index. The EXE's body also references the expected handle table at `0x8E4010`,
channel pointer at `0x8E3FFC`, initialized flag at `0x8E3FE4`, and last-position
global at `0x8E4898`. The plugin retains this EXE's packed last-position
arithmetic rather than adopting gta-reversed's optional behavior change.
See [CdStream definitions](https://github.com/gta-reversed/gta-reversed/blob/bba3990d59b9494bea52ba5638e5fef50bfe0cb3/source/game_sa/CdStreamInfo.h)
and [CdStream implementation](https://github.com/gta-reversed/gta-reversed/blob/bba3990d59b9494bea52ba5638e5fef50bfe0cb3/source/game_sa/CdStreamInfo.cpp).

## Additional fixes and optimizations

### Retain only the explicitly owned model

Native model requests propagate supplied flags into texture requests.
The optional preload path supplied `KEEP_IN_MEMORY` but tracked/released only
the model, allowing extra texture retention. It now requests normally and
sets its retention bit only on the model it tracks. Native dependency handling
continues normally. Reserve the tracking entry before requesting, so allocation
failure cannot install an untracked pin. Retries now skip mission/game/scene
ownership instead of replacing those flags on a new request.

The existing one-argument SDK call to `SetModelIsDeletable` is correct for this
EXE. Do not replace it with the reference project's refactored two-argument
signature. See [streaming behavior](https://github.com/gta-reversed/gta-reversed/blob/bba3990d59b9494bea52ba5638e5fef50bfe0cb3/source/game_sa/Streaming.cpp).

This change affects optional AssetPreload admissions/retries; the primary INI
keeps that feature disabled. It does not clear preexisting native/mod pins.

### Bound periodic address-space work

The previous probe walked the entire address space in one scheduler callback
roughly every ten seconds. It now advances at most 256 regions per update,
with a 500-microsecond soft budget checked every 16 regions. Completion
publishes the largest free span; later passes retain the previous completed
result until a new one finishes. Initial or failed measurement cannot authorize
speculative loading. Aggregate free-VA and physical-memory checks remain live.

The soft budget cannot preempt an individual OS call. A sweep spans multiple
ticks and remains an approximate fragmentation observation, not a reservation
or a guarantee that an allocation will succeed.

### Tighten CdStream layout validation and reduce duplicate probes

The former 15-byte fingerprint covered only the prologue. Extend it through
the handle-table lookup, channel stride, initialized test and idle-channel
setup. Recheck that body before interpreting native data on each connected
read; a later layout change latches native fallback and appears as
`cdLayoutRejected=1` in diagnostics. Restart/reinitialization is needed before
using that cache path again. Unknown layouts are not guessed.

Batch the five checks of neighboring global fields into one contiguous-range
check. Together with the added body check, this path uses two region queries
instead of five before checking the channel allocation. A synthetic hit also
requires writable channel state and last-position storage. Ready CdStream
substitution remains disabled in the primary INI.

### Stop completed camera traversal

The instancing callback previously returned when its count/time limit was
reached, but its outer traversal kept walking the remaining nodes. A stop
result now exits both normal and LOD scans. A zero instance quota skips the
instancing pass entirely. Entity creation/deletion remain native main-thread
operations; [entity methods](https://github.com/gta-reversed/gta-reversed/blob/bba3990d59b9494bea52ba5638e5fef50bfe0cb3/source/game_sa/Entity/Entity.cpp)
confirm the referenced locations.

The optional preload classification scan also uses a per-call readable-region
cache and waits for a non-null normal-sector base without advancing its cursor.
The archive profiler's position argument now explicitly uses `uint32_t`, in
agreement with the engine's interface and packed positions.

## Build, artifacts and limits

- MSVC **Release Win32 ASI build succeeded**. Only the ASI target was built;
  test targets were neither built nor executed for this follow-up.
- Fast packaging updated `dist/DistantStreaming.asi`, its PDB and map.
  The x64 helper was unchanged and correctly skipped; protocol remains v10.
- Primary/distribution INI contents are preserved. Previous source and release
  files are under `.review-backups/2026-10-20-exe-review/`.
- Inspection files: `build-review-x64/exe-review/layout.json` and the adjacent
  `dumpbin` disassemblies/headers. Compile log:
  `build-vs2026-win32/exe-review-build.log`; package log:
  `build-review-x64/exe-review/package.log`.

On-disk comparison cannot observe patches installed later by OLA, Mod Loader
or other ASIs, nor prove runtime page protections or every hook-chain behavior.
The existing runtime guards remain necessary. The previous pass's test results
describe its earlier build; no new test result or measured FPS gain is claimed
for this follow-up.
