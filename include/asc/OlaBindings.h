#pragma once
#include <cstdint>
#include <cstddef>
namespace asc::ola {
// A GTA SA 1.0 US CEntity::GetModelInfo instruction:
// mov ecx, dword ptr [eax*4 + absolute_table_base]
// This independent reference is used rather than Plugin-SDK's eager
// 0x40CD67 immediate which may have become a jump/patch byte.
constexpr uintptr_t DecodeEntityModelTable(const uint8_t* code, size_t size) {
    if (!code || size < 7 || code[0] != 0x8B || code[1] != 0x0C ||
        code[2] != 0x85) return 0;
    uintptr_t p = 0;
    for (unsigned i=0;i<4;++i)
        p |= uintptr_t(code[3+i]) << (i*8);
    return p;
}
constexpr bool ValidModelCount(int count) {
    return count >= 1000 && count <= 100000;
}
// Ownership is explicitly configurable. OLA still manages model/table
// relocation; ASC may own only the native streaming budget.
constexpr bool ShouldManageNativeMemory(bool enabled, bool olaLoaded,
                                        bool ascOwnsBudget=false) {
    return enabled && (!olaLoaded || ascOwnsBudget);
}
}
