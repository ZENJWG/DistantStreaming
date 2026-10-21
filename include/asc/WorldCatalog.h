#pragma once
// Portable read-only logical world catalog. No GTA CEntity or RW pointers.
#include <cstdint>
#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace asc::world_catalog {
enum class Category : uint8_t { Unknown=0, Normal=1, Generic=2, Vegetation=3, Timed=4, Lod=5 };
struct Placement {
    uint64_t identity=0;
    int32_t modelId=-1;        // IDE hint ONLY; not validated against live OLA tables
    std::array<char,24> modelName{}; // required for game-thread identity validation
    float x=0,y=0,z=0;
    int16_t interior=0;
    Category category=Category::Unknown;
    int32_t cellX=0,cellY=0;
};
// Archive records are CANDIDATES, never selected Mod Loader override paths.
// No game request or background warmer may treat this index as authoritative.
enum class AssetType : uint8_t { Dff=1, Txd=2, Col=3 };
struct ArchiveCandidate {
    std::string archive;
    uint64_t offset=0,bytes=0;
    AssetType type=AssetType::Dff;
};
struct ModelDependency {
    int32_t id=-1;
    std::string modelName,txdName;
    uint32_t dffCandidates=0,txdCandidates=0;
};
struct Status {
    uint64_t generation=0, manifestHash=0;
    uint64_t sources=0, ideDefinitions=0, placements=0, resolved=0, unresolved=0;
    uint64_t unsupported=0, rejected=0, cells=0;
    uint64_t binarySources=0,binaryPlacements=0,imgArchives=0,ambiguousModels=0;
    uint64_t archiveAssetCandidates=0,ambiguousAssetNames=0;
    uint64_t modelDependencyEdges=0,modelsWithArchiveCandidates=0;
    bool loadedFromCache=false; // diagnostic only: validated snapshot was reused
    bool ready=false, complete=false; // complete only means supported files parsed cleanly
};
struct Hints {
    std::vector<int32_t> modelIds; // untrusted hints: not a request list!
    std::vector<std::array<char,24>> modelNames; // aligned by index; requires live GTA check
    std::vector<std::array<char,24>> txdNames; // IDE dependency hint; never source authority
    uint32_t placements=0;
};
// Build a bounded snapshot of registered text IDE/IPL and VER2 IMG binary IPL.
// Does NOT resolve Mod Loader precedence. Unknowns are reported.
class Catalog {
public:
    static Catalog Build(const std::filesystem::path& gameRoot,
                         int32_t cellUnits=256);
    // Persist only verified read-only inventory. Invalid/stale caches rebuild.
    static Catalog LoadOrBuild(const std::filesystem::path& gameRoot,
                               const std::filesystem::path& cacheFile,
                               int32_t cellUnits=256);
    const Status& GetStatus() const noexcept {return status_;}
    const std::vector<Placement>& Placements() const noexcept {return placements_;}
    // Optional placement cursor rotates bounded replies without mutating the catalog.
    Hints Query(int32_t cellX,int32_t cellY,size_t maxModels=64,
                size_t* placementCursor=nullptr) const;
    // Diagnostic inventory only; file name collisions are never resolved by
    // choosing scan order. Provenance must come from live Mod Loader HANDLEs.
    const std::vector<ArchiveCandidate>* CandidateAssets(
        const std::string& filename) const noexcept;
    const ModelDependency* Dependency(const std::string& modelName) const noexcept;
private:
    Status status_{};
    std::vector<Placement> placements_;
    std::unordered_map<uint64_t,std::vector<uint32_t>> cells_;
    std::unordered_map<std::string,std::vector<ArchiveCandidate>> assetCandidates_;
    std::unordered_map<std::string,ModelDependency> dependencies_;
};
uint64_t CellKey(int32_t x,int32_t y) noexcept;
} // namespace asc::world_catalog
