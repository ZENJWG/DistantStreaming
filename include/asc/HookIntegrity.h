#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace asc::hook_integrity {
// MinHook normally writes E9 rel32 at these SA 1.0 US function entries.
// A hook overwritten by an unknown plugin must never be blindly restored.
enum class State : uint8_t { Owned, Pristine, Foreign, Unreadable };

inline bool JumpsTo(const uint8_t* code, std::size_t size,
                    uintptr_t entry, uintptr_t detour) noexcept {
    if (!code || size < 5 || code[0] != 0xE9) return false;
    int32_t displacement = 0;
    std::memcpy(&displacement, code + 1, sizeof(displacement));
    const auto destination = static_cast<int64_t>(entry) + 5 + displacement;
    return destination >= 0 && static_cast<uint64_t>(destination) == detour;
}

inline State Classify(const uint8_t* code, std::size_t size,
                     uintptr_t entry, uintptr_t detour,
                     const uint8_t* original,
                     std::size_t originalSize) noexcept {
    if (!code || !original || size < 5 || originalSize < 5)
        return State::Unreadable;
    if (JumpsTo(code, size, entry, detour)) return State::Owned;
    if (size >= originalSize &&
        std::memcmp(code, original, originalSize) == 0)
        return State::Pristine;
    return State::Foreign;
}
} // namespace asc::hook_integrity
