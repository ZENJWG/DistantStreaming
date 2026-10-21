#pragma once
// Only ACTUAL Mod Loader ReadFile handles are eligible for opt-in warming.
// This is an observation, NOT a guess at Mod Loader override precedence.
#include <algorithm>
#include <cstdint>
#include <limits>

namespace asc::observed_warm {
enum class Kind : uint8_t { Unsupported=0, Img=1, Loose=2 };

struct Limits {
    uint64_t maxSingleBytes=512ull*1024*1024;
    uint64_t maxAggregateBytes=2048ull*1024*1024;
    uint32_t maxSources=64;
};

struct Usage { uint64_t admittedBytes=0; uint32_t admittedSources=0; };

constexpr bool Admit(Kind kind, uint64_t fileBytes, Limits limits,
                     Usage current) noexcept {
    if(kind==Kind::Unsupported || !fileBytes || !limits.maxSingleBytes ||
       !limits.maxAggregateBytes || !limits.maxSources ||
       fileBytes>limits.maxSingleBytes ||
       fileBytes>limits.maxAggregateBytes ||
       current.admittedSources>=limits.maxSources ||
       current.admittedBytes>limits.maxAggregateBytes-fileBytes)
        return false;
    return true;
}
} // namespace asc::observed_warm
