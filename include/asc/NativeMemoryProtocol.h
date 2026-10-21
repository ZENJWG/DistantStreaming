#pragma once
#include <cstdint>
#include <string>

namespace asc::native {
constexpr uint32_t Magic=0x4D4E5341; // ASNM
constexpr uint32_t Version=10; // Verified observed IMG-member extent warming; both binaries must match
constexpr uint64_t MiB=1024*1024;
constexpr uint64_t MaxResource=256*MiB;
constexpr uint32_t TransferBytes=1024*1024;
// Shared timing policy; these constants do not change the wire layout.
constexpr uint32_t RpcTimeoutMs=5000;
constexpr uint32_t AssetIoTimeoutMs=1500;
static_assert(AssetIoTimeoutMs<RpcTimeoutMs);
enum class Command : uint32_t { Allocate=1, Release=2, Stats=3, Stop=4,
    OpenSource=5,ReadSource=6,CloseSource=7,AssetStats=8,
    OpenWarmSource=9,CacheCoverage=10,
    CatalogConfigure=11,CatalogStatus=12,CatalogQueryCell=13,
    OpenWarmImgMember=14 };
#pragma pack(push,1)
struct Request {
    uint32_t magic=Magic,version=Version;
    Command command=Command::Stats;
    uint64_t id=0,bytes=0;
};
struct Reply {
    uint32_t magic=Magic,status=1;
    uint64_t used=0,resources=0;
};
// Catalog addresses are spatial *hints*, never CEntity addresses or
// OLA-validated native model IDs. Version 5 ASI never requests them directly.
struct CatalogSummary {
    uint64_t generation=0,manifestHash=0;
    uint64_t sources=0,ideDefinitions=0,placements=0,resolved=0,unresolved=0;
    uint64_t unsupported=0,rejected=0,cells=0;
    uint64_t binarySources=0,binaryPlacements=0,imgArchives=0,ambiguousModels=0;
    // Filename-only IMG inventory: not verified effective Mod Loader sources.
    uint64_t archiveAssetCandidates=0,ambiguousAssetNames=0;
    uint64_t modelDependencyEdges=0,modelsWithArchiveCandidates=0;
    uint32_t ready=0,complete=0,scanning=0,loadedFromCache=0;
};
struct CatalogCellReply {
    uint32_t placementCount=0,modelCount=0;
    int32_t cellX=0,cellY=0;
    int32_t modelIds[64]{};
    char modelNames[64][24]{}; // only a hint until game-thread CModelInfo name+ID check
    char txdNames[64][24]{}; // non-authoritative IDE TXD dependency name
};
// An actual Mod Loader-selected IMG handle, the exact read that was observed,
// and non-authoritative current cell hints. The helper verifies IMG membership.
struct ObservedImgMemberWarm {
    uint64_t observedOffset=0;
    uint64_t remainingQuotaBytes=0; // session's remaining observed-source quota
    uint32_t observedBytes=0;
    uint32_t maxMemberBytes=0;
    CatalogCellReply hints{};
};
struct AssetRead {uint64_t destination=0,offset=0;};
struct AssetSummary {
    uint64_t residentBytes=0,hits=0,misses=0,diskBytes=0,gameBytes=0;
};
struct CacheCoverage {
    uint64_t archives=0,sourceBytes=0,expectedPages=0,residentPages=0;
    uint64_t inFlightPages=0,waits=0,deferred=0,failures=0;
    // pendingBytes is reservation size, not additional live allocation.
    // allocatedBytes includes indexed, loading and reader-retained pages.
    uint64_t pendingBytes=0,allocatedBytes=0,capacityBytes=0;
};
#pragma pack(pop)
static_assert(sizeof(Request)==28 && sizeof(Reply)==24);
static_assert(sizeof(CacheCoverage)==88);
static_assert(sizeof(ObservedImgMemberWarm)==3368);
static_assert(sizeof(CatalogSummary)==160 && sizeof(CatalogCellReply)==3344);
inline std::wstring PipeName(const std::wstring& session) {
    return L"\\\\.\\pipe\\DistantStreaming.NativeMemory."+session;
}
inline std::wstring MappingName(const std::wstring& session,uint64_t id) {
    return L"Local\\DistantStreaming.NativeMemory."+session+L"."+std::to_wstring(id);
}
}
