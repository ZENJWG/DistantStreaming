#pragma once
// Cache ONLY successful committed-readable VirtualQuery regions, for one game
// renderer frame. A hit avoids repeating the OS query, not entity validation.
// A new frame must reset it because game pools may be freed/remapped.
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace asc::read_cache {
template <std::size_t Capacity = 32>
class RegionCache {
    static_assert(Capacity > 0);
    struct Region { uintptr_t first{}, last{}; };
    std::array<Region, Capacity> regions_{};
    std::size_t next_{};
    uint64_t hits_{}, misses_{};
public:
    void Reset() noexcept {
        regions_ = {};
        next_ = 0;
        hits_ = misses_ = 0;
    }
    bool Covers(uintptr_t address, size_t bytes) noexcept {
        // A bad/overflowing pointer must not be accepted by an old entry.
        if (!address || !bytes || bytes > UINTPTR_MAX - address) {
            ++misses_;
            return false;
        }
        for (const auto& region : regions_) {
            if (address >= region.first && address < region.last &&
                bytes <= region.last - address) {
                ++hits_;
                return true;
            }
        }
        ++misses_;
        return false;
    }
    void Remember(uintptr_t first, size_t bytes) noexcept {
        if (!first || !bytes || bytes > UINTPTR_MAX - first) return;
        regions_[next_] = Region{first, first + bytes};
        next_ = (next_ + 1) % Capacity;
    }
    uint64_t Hits() const noexcept { return hits_; }
    uint64_t Misses() const noexcept { return misses_; }
};
} // namespace asc::read_cache
