#pragma once
#include <cstdint>
#include <algorithm>

namespace asc::gpu_hot_residency {

// Opportunistically reprioritize only resources already created as
// D3DPOOL_MANAGED. This policy never changes resource pools or ownership.
inline bool ShouldSampleBind(uint64_t callIndex, uint32_t sampleRate) noexcept {
    return sampleRate && callIndex % sampleRate == 0;
}

// WDDM budgets are smaller than physical VRAM on busy machines. Never use a
// physical VRAM figure as guaranteed committed process memory.
struct Budget {
    uint64_t usageBytes=0;
    uint64_t osBudgetBytes=0;
    uint64_t configuredTargetBytes=0;
    uint64_t reserveBytes=0;
    bool valid=false;
};
struct BudgetDecision {
    bool allowSpeculativeUpload=false;
    uint64_t effectiveTargetBytes=0;
    uint64_t freeToTargetBytes=0;
};
inline BudgetDecision Decide(const Budget& input) noexcept {
    if (!input.valid || !input.osBudgetBytes || !input.configuredTargetBytes)
        return {};
    const uint64_t cap=std::min(input.osBudgetBytes,input.configuredTargetBytes);
    const uint64_t safeLimit=cap>input.reserveBytes?cap-input.reserveBytes:0;
    const uint64_t available=input.usageBytes<safeLimit?safeLimit-input.usageBytes:0;
    return {available>0,safeLimit,available};
}

} // namespace asc::gpu_hot_residency
