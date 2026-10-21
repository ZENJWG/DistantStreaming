#pragma once
#include <cstdint>
namespace asc::hd {
constexpr uint32_t kMagic=0x54534148; // HAST
constexpr uint16_t kVersion=2;
enum class Command : uint16_t { Ping=1, List=2, Get=3, Error=4, Pong=5, Asset=6 };
#pragma pack(push,1)
struct Header {
    uint32_t magic=kMagic;
    uint16_t version=kVersion;
    uint16_t command=0;
    uint64_t requestId=0;
    uint32_t payloadBytes=0;
};
struct Asset {
    uint32_t width=0,height=0,mipCount=0,fourCC=0;
    uint64_t byteCount=0;
};
#pragma pack(pop)
static_assert(sizeof(Header)==20);
static_assert(sizeof(Asset)==24);
constexpr uint32_t kMaxMessage=65536;
constexpr uint64_t kMaxPipeAssetBytes=64ull*1024*1024; // bounded synchronous transfer
constexpr uint64_t kMaxAssetBytes=256ull*1024*1024;
}
