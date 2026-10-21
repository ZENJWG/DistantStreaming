# v20 compilation and experiment

**Contents:** source only, no compiled ASI (x86 MSVC not available in authoring environment).

From the extracted root on a Windows PC with Visual Studio Build Tools:

```powershell
.\build.bat
```

The x86 game binary is `dist\DistantStreaming.asi` and the x64
asset helper is `dist\DistantStreaming_AssetCache64.exe`.
Do not load any previous `DistantStreaming.asi` or `AdaptiveStreaming.asi`
in parallel, including through Mod Loader.

**Test profile:** after backing up your INI, copy
`config\DistantStreaming_v20_VISUAL_TEST.ini` to
`DistantStreaming.ini` beside the ASI. Restart GTA SA.

Before measuring, keep the player at a fixed coordinate and angle.
Compare screenshots from a normal configuration and the test profile.
Wait for assets to stream into the native game; the x64 cache only holds
archive bytes, not decoded 32-bit RenderWare models.

Check for these log messages:
- `Extended world scan ACTIVE v20`: two core native hooks active
- `LOD sectors`, `LOD entities`, `LOD submitted`: far native LOD path
- `non-road render-pass entries` and `of those from extra scan`:
  an actual non-road *render function entry*, not GPU completion
- `render hook active=1`: optional render-probe success

Diagnostics readout:
- **LOD sectors = 0:** far list disabled, out of range, or gated by VA.
- **LOD sectors > 0 and LOD entities = 0:** no eligible currently
  instantiated big-building IPL entities in these native lists.
- **LOD submitted > 0 and render-pass extra = 0:** investigate visibility,
  fading/roads, list removal, render-list clipping and hook availability.
- **Render-pass extra > 0 but visual appears vanilla:** examine fog,
  occlusion, alpha, actual RwObject and D3D9 render calls.
- **VA-pressure skipped increases:** native 32-bit VA capacity is limiting.

The test config allows the scanner at 512 MiB free VA. This is
**not a safe production threshold**; your prior logs reported only
about 770 MiB free VA during gameplay. Stop testing if the game becomes
unstable. For stable play revert to 1024 MiB and lower scan budgets.

The implementation is a bounded map-only extension for existing IPL
placements and big-building LOD lists, not a whole-map binary IPL importer.
It has not been compiled with MSVC or observed working in-game.
