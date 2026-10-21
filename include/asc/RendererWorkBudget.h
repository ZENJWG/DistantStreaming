#pragma once
// Read-only native-backlog admission control for speculative renderer requests.
// Never changes already-loaded visibility or GTA's own streaming queue.
#include <algorithm>
#include <cstdint>
namespace asc::renderer_work {
inline uint32_t SupplementalRequestLimit(uint32_t configured,
                                         uint32_t nativeBacklog,
                                         uint32_t start) noexcept {
    if (!configured || !start || nativeBacklog < start) return configured;
    const uint64_t level = nativeBacklog;
    const uint64_t threshold = start;
    if (level >= threshold * 4) return std::min(configured, 1u);
    if (level >= threshold * 2) return std::min(configured, 2u);
    return std::min(configured, 4u);
}
} // namespace asc::renderer_work
