#pragma once
#include <cstdint>

namespace asc::archive_io {
// The original kernel OVERLAPPED completion path must always run.
// A cache hit may replace only validated, synchronous source-file reads.
enum class ReadMode : uint8_t { PassThrough, SynchronousCandidate, WarmOnlyOverlapped };
constexpr ReadMode Classify(uint32_t bytes,bool bufferPresent,
                            bool donePresent,bool overlapped,
                            uint32_t maximumBytes) noexcept {
    if(!bufferPresent||bytes==0||bytes>maximumBytes)
        return ReadMode::PassThrough;
    if(overlapped)return ReadMode::WarmOnlyOverlapped;
    return donePresent?ReadMode::SynchronousCandidate:ReadMode::PassThrough;
}
constexpr uint64_t OverlappedOffset(uint32_t low,uint32_t high) noexcept {
    return (uint64_t(high)<<32)|low;
}
}
