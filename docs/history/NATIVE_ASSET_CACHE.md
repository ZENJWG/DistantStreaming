# GTA asset data in x64, returned to GTA on demand

This layer addresses actual texture/model source data, separately from the
managed D3D texture/mesh-buffer backing layer.

## Data flow

1. GTA's native streaming worker requests archive/file bytes for an asset.
2. The ASI passes that already-open source handle to the x64 companion through
   the authenticated parent session. The helper duplicates and reopens the exact
   file independently; it does not guess a path or modify GTA's file position.
3. The helper reads missing 1 MiB source pages into its own memory and retains
   them in a bounded LRU cache. Hits reuse those retained bytes.
4. The requested range is copied into a shared 1 MiB transfer window. The ASI
   copies it into the existing GTA streaming buffer, reusing the window for
   larger requests. The whole cache is never mapped into GTA.
5. GTA performs its normal TXD/DFF conversion and rendering on the active asset.
   Supported managed graphics resources additionally use the x64 backing layer.
6. GTA can unload its active resource while source pages stay in the companion
   until evicted, reused by subsequent requests without another disk read.

The companion is now a producer of actual source bytes, not just storage for
CPU graphics copies produced inside GTA. TXD/DFF decoding, active RenderWare
objects, collision, animations and engine streaming accounting remain in GTA.
This does not implement a full out-of-process RenderWare engine.

## Configuration and memory ownership

```ini
[NativeMemory]
Enabled=1
AssetCache=1
Textures=1
MeshBuffers=1
MigrateLoadedTextures=1
BackingLimitMiB=6144
```

The helper starts when the ASI installs its hooks, independently of D3D device
availability. The asset cache and graphics backing use the same helper and
budget. Source pages can be evicted; live graphics backing cannot. Graphics
allocations trim cached source pages first. When there is no room for a cache
page or a read fails, the original game read handles that request.

The control protocol is version 2. Install matching ASI, INI and helper from
`dist`; older helper binaries cannot supply the new commands. No DDS directory
or manual helper launch is required.

## Interception scope and compatibility

The hook replaces only GTA's ReadFile import, leaving the Windows implementation
and other modules' imports alone. It serves recognized IMG/TXD/DFF/COL reads
only from the SA 1.0 US CdStream worker address range and thread. This preserves
the existing queue, channel state, conversion, dependency ordering and native
completion path. Early initialization reads and unrelated I/O use the original
reader. Synchronous reads advance the game file position; completed overlapped
reads provide the byte count/status/event and return TRUE to the worker.

An already redirected/foreign ReadFile reader is preserved, since bypassing it
could discard a Mod Loader asset overlay. Relocated/replaced CdStream code can
also bypass this cache. Therefore this update does not claim interception of
every modded loader or every loose-file loading path. These cases are logged
or retain original I/O rather than returning an incorrect asset.

Cached source data is invalidated when file size or last-write time changes.
The client identifies handle reuse with the volume/file identity. Files are
opened independently in the helper with compatible sharing and overlapped I/O;
the source handle does not share GTA's file-position state. Source registration
is capped at 4096 open identities. Source pages are bounded by the shared budget.

## Observability and current validation

The log distinguishes installation from actual use:

```
Native asset cache: ACTIVE; x64 source reads -> retained cache -> GTA streaming buffer
Native asset cache: resident bytes=..., hits=..., misses=..., read by x64 bytes=..., supplied to GTA bytes=..., fallback reads=...
```

The ACTIVE line means interception was installed. Increasing supplied-byte
counts show completed reads returned through this path. Increasing cache-hit
counts show reuse of retained source data. Graphics-backing counters alone do
not demonstrate asset-cache use.

The Win32 ASI and x64 helper compile. No tests were added or run for this update,
and neither updated executable was run, per the user's instruction. There is
no runtime or game compatibility result for this path yet. Earlier GPU/backing
results apply only to the earlier implementation.

Streaming-worker reference: [gta-reversed CdStreamInfo.cpp](https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/CdStreamInfo.cpp).
