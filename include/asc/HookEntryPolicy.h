#pragma once
// Pure byte-level classifier for existing x86 entry-point detours.
// It never follows a pointer or writes to foreign code. An FF 25 target
// is a pointer *slot*, not an executable function address.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace asc::hook_entry {
enum class Kind : uint8_t {
    CleanOrUnknown, RelativeJmp32, RelativeJmp8,
    AbsoluteIndirectJmp, PushRet, MovEaxJmp, TrapOrReturn
};
struct Analysis {
    Kind kind{Kind::CleanOrUnknown};
    uintptr_t directTarget{}; // Only for relative/push/mov forms.
    uintptr_t pointerSlot{};  // Only for FF 25 absolute-indirect form.
    bool Patched() const noexcept { return kind != Kind::CleanOrUnknown; }
};
inline Analysis Inspect(const uint8_t* code, size_t size, uintptr_t entry) noexcept {
    if (!code || !size) return {};
    if (code[0] == 0xCC || code[0] == 0xC3 || code[0] == 0xC2)
        return {Kind::TrapOrReturn, 0, 0};
    if (code[0] == 0xE9 && size >= 5) {
        int32_t rel{};
        std::memcpy(&rel, code + 1, sizeof(rel));
        const int64_t target = static_cast<int64_t>(entry) + 5 + rel;
        return {Kind::RelativeJmp32,
                target >= 0 && static_cast<uint64_t>(target) <=
                    std::numeric_limits<uintptr_t>::max()
                    ? static_cast<uintptr_t>(target) : 0, 0};
    }
    if (code[0] == 0xEB && size >= 2) {
        const auto rel = static_cast<int8_t>(code[1]);
        const int64_t target = static_cast<int64_t>(entry) + 2 + rel;
        return {Kind::RelativeJmp8,
                target >= 0 && static_cast<uint64_t>(target) <=
                    std::numeric_limits<uintptr_t>::max()
                    ? static_cast<uintptr_t>(target) : 0, 0};
    }
    if (code[0] == 0xFF && size >= 6 && code[1] == 0x25) {
        uint32_t slot{};
        std::memcpy(&slot, code + 2, sizeof(slot));
        return {Kind::AbsoluteIndirectJmp, 0, slot};
    }
    if (code[0] == 0x68 && size >= 6 && code[5] == 0xC3) {
        uint32_t target{};
        std::memcpy(&target, code + 1, sizeof(target));
        return {Kind::PushRet, target, 0};
    }
    if (code[0] == 0xB8 && size >= 7 &&
        code[5] == 0xFF && code[6] == 0xE0) {
        uint32_t target{};
        std::memcpy(&target, code + 1, sizeof(target));
        return {Kind::MovEaxJmp, target, 0};
    }
    return {};
}
}
