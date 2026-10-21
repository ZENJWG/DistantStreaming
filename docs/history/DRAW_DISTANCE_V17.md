# SPEC-2 — GTA SA map draw distances (v17 source)

## Verified engine entry points

Reviewed on 2026-10-17:
- gta-reversed `source/game_sa/Renderer.cpp`:
  https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/Renderer.cpp
- gta-reversed `source/game_sa/Streaming.cpp`:
  https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/Streaming.cpp
- gta-reversed `source/game_sa/FileLoader.cpp`:
  https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/FileLoader.cpp
- Plugin-SDK `plugin_sa/game_sa/CBaseModelInfo.h`, `CModelInfo.h`, `CRenderer.h`,
  `CWorld.h`, `CKeyGen.h`, and `enums/eModelInfoType.h`:
  https://github.com/DK22Pac/plugin-sdk/tree/master/plugin_sa/game_sa
- gta-reversed `PlantMgr.cpp`, `GrassRenderer.cpp`, `WaterLevel.cpp`:
  https://github.com/gta-reversed/gta-reversed/tree/master/source/game_sa

| Original SA 1.0 US entry point or data | Role / limitation |
| --- | --- |
| `CBaseModelInfo::m_fDrawDistance` offset `+0x18` | Model's IDE draw-distance field. This implementation modifies it for map models only. |
| `CModelInfo::ms_modelInfoPtrs`, `CModelInfo::ms_modelInfoCount` | Live model table; OLA may relocate it. MUST verify binding. |
| `CRenderer::SetupMapEntityVisibility` `0x553F60` | Uses camera LOD multiplier * model draw distance, clamped to far clip + bounding radius. NO detour installed. |
| `CRenderer::SetupEntityVisibility` `0x554230` | General entity routing; pedestrians/vehicles use separate branches. NO detour installed. |
| `CRenderer::SetupBigBuildingVisibility` `0x554650` | LOD/big building entities. NO detour installed. |
| `CRenderer::ScanSectorList` `0x554840` and `ScanBigBuildingList` `0x554B10` | Visible map instance discovery/requests. Scanning/culling limits remain. |
| `CRenderer::ShouldModelBeStreamed` `0x554EB0` | Reuses model draw distance for requests. |
| `CStreaming` model removal / entity removal paths | Streaming eviction uses model distance and other references; this code does NOT pin models or alter native eviction. |
| `CVisibilityPlugins` fading paths | Fades also reference model draw distance; remaining renderer fading logic untouched. |
| `CKeyGen::GetUppercaseKey` `0x53CF30` | Produces `CBaseModelInfo::m_nKey` for model-name overrides. |
| `CWorld::ms_aSectors` (120x120), `ms_aLodPtrLists` (30x30) | Optional bounded world-sector classification; no entity pointers retained. |
| `CPlantMgr::Update` `0x5DCFA0`, `CGrassRenderer::SetCloseFarAlphaDist` `0x5DABE0` | Procedural grass separate from model table; NOT controlled by generic model multipliers. |
| `CWaterLevel::RenderWater` `0x6EF650` | Water renderer separate from model table; NOT controlled here. |

## Implemented map model categories

Implemented in `src/game/DrawDistanceWin.cpp` and `include/asc/DrawDistancePolicy.h`.

Precedence (highest to lowest): absolute per-model override, vegetation (wind flag or explicit),
road (`IsRoad()`), timed (`MODEL_INFO_TIME`), big building (sector entity flag),
LOD (`MODEL_INFO_LOD`, `bIsLod`, or world LOD list), sector-classified building,
clump (non-vehicle/ped only), general atomic object. Higher categories replace lower
ones: these are not independent additional multipliers.

| INI key | Applies to |
| --- | --- |
| `GlobalMultiplier` | ALL eligible map models before applying category multiplier |
| `ObjectsMultiplier` | Unclassified generic atomic props, fences, foliage meshes etc. |
| `BuildingsMultiplier` | Buildings and world-sector dummy representations discovered in the world lists |
| `RoadsMultiplier` | Road and pavement mesh models carrying engine `IsRoad` flag |
| `TimedMultiplier` | Time-limited models (time-of-day visibility is NOT overridden) |
| `LodMultiplier` | LOD model types or LOD-marked world models |
| `BigBuildingsMultiplier` | World models with `bIsBIGBuilding` |
| `VegetationMultiplier` | Swaying models and exact IDs/names marked as vegetation |
| `ClumpsMultiplier` | Non-vehicle/non-ped clump model metadata (map decorations) |
| `[DrawDistanceModelOverrides]` | Absolute distance by numeric model ID |
| `[DrawDistanceNamedModelOverrides]` | Absolute distance by IDE model name (GTA key hash) |
| `[DrawDistanceVegetationModels]` | Assign vegetation group by numeric model ID |
| `[DrawDistanceVegetationNames]` | Assign vegetation group by model name |

Vehicles, pedestrians and weapon types are excluded *before* numeric/named overrides.
Native model time-of-day activation, collisions, physics, dynamic gameplay, and interior area flags are untouched.

### Setup

1. Build Windows source with `build.bat` or `verify-windows.ps1` and install the produced
   `DistantStreaming.asi`. Retain the matching helper binary if using the external cache.
2. Edit `DistantStreaming.ini` next to the ASI.
3. `[DrawDistance] Enabled=1`. Set, for example:

```ini
[DrawDistance]
Enabled=1
GlobalMultiplier=1.0
BuildingsMultiplier=1.5
RoadsMultiplier=1.3
ObjectsMultiplier=1.2
TimedMultiplier=1.2
LodMultiplier=1.6
BigBuildingsMultiplier=1.8
VegetationMultiplier=1.4
ClumpsMultiplier=1.0
MaxDistanceUnits=3000
ModelsPerFrame=128
WorldSectorsPerFrame=24
WorldRescanMs=60000

[DrawDistanceNamedModelOverrides]
aw_streettree1=450

[DrawDistanceVegetationNames]
aw_streettree2=1
```

Unspecified multipliers are 1x; in an unmodified INI all distances remain vanilla.
If several categories match a model, only the highest-priority category multiplier is chosen.

4. Read `DistantStreaming.log` for `Map draw distances:` and verify that model writes are recorded.
5. Test with `AssetPreload=0` and `OffscreenCleanup=0` if those subsystems previously caused problems.

### Lifecycle and compatibility

- No new GTA code hook or `RenderWare` custom renderer. Neither `CRenderer` nor the D3D9
  creation paths are detoured by this feature.
- Maintains a copy of the original draw-distance float per `(model slot, model pointer, key)`.
  Rebinds through existing OLA-safe table verification. Changes to model metadata are
  re-detected; after three conflicting edits, a slot is left alone.
- Game-shutdown restoration only touches still-bound model records with the same pointer,
  name hash and last value written by the plugin. A relocated or freed pointer is NEVER
  dereferenced for restoration.
- Full world-sector classification is incremental (120x120 + 30x30 sectors) and later
  rescanned for streamed-in binary IPL placements.
- Gameplay limits do NOT disappear: camera far clip, scanning radius, render-list capacity,
  model loading, and 32-bit virtual-address budgets can prevent objects from appearing
  at the configured distance. More distance means more native model requests and higher risk
  of memory pressure on a heavily modded game. Prefer increases of 1.1x to 1.5x first.
- Setting `MaxDistanceUnits` does not reduce original distances when *all* relevant multipliers are 1.
- This is **per-model**, not per-instance, distance. If the same model ID is used by several
  entity types, they share the model's chosen category distance.
- Procedural CGrassRenderer/PlantMgr, water, clouds, corona/2DFX, particle emission and
  effects cannot correctly be controlled simply by this field and are not implemented.
  Dedicated verified subsystem hooks/patches would be a separate, riskier feature.

### Tests and limitations

`asc_draw_distance_tests`: policy, category precedence, exclusion of ped/vehicle/weapon even with
explicit overrides, absolute target clamping, default vanilla behavior.

`asc_config_tests` (MSVC Windows): float multiplier parsing, clamping, numeric model and
vegetation selectors; stale legacy config test references removed.

Portable CMake/CTest pass does not mean the Windows 32-bit ASI compiled or ran in GTA.
Test the exact HOODLUM 1.0 US exe with OLA and Mod Loader and record model count, 32-bit VA,
CRenderer behavior, and loading/eviction stability before deploying permanently.
