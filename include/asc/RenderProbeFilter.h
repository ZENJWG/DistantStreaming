#pragma once
// Allocation-free prefilter for renderer probes. False positives are okay:
// every positive must still be checked against the exact entity pointer list.
#include <array>
#include <cstdint>
#include <utility>
namespace asc::render_probe {
class PointerFilter {
public:
    void Clear() noexcept { bits_.fill(0); }
    void Insert(const void* ptr) noexcept {
        const auto [a,b]=Hashes(ptr);
        bits_[a>>6] |= uint64_t(1) << (a&63);
        bits_[b>>6] |= uint64_t(1) << (b&63);
    }
    bool MightContain(const void* ptr) const noexcept {
        const auto [a,b]=Hashes(ptr);
        return (bits_[a>>6] & (uint64_t(1) << (a&63))) &&
               (bits_[b>>6] & (uint64_t(1) << (b&63)));
    }
private:
    static std::pair<uint32_t,uint32_t> Hashes(const void* ptr) noexcept {
        uint32_t x=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ptr)>>3);
        x ^= x>>16; x *= 0x7feb352dU; x ^= x>>15; x *= 0x846ca68bU; x ^= x>>16;
        return {x & 4095U, (x>>12) & 4095U};
    }
    std::array<uint64_t,64> bits_{};
};
} // namespace asc::render_probe
