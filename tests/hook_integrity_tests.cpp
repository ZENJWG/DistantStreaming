#include "asc/HookIntegrity.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

int main() {
    using namespace asc::hook_integrity;
    constexpr uintptr_t entry = 0x4087E0, detour = 0x65001000;
    const std::array<uint8_t,12> original{
        0x53,0x8B,0x5C,0x24,0x0C,0x55,0x8B,0x6C,0x24,0x0C,0x56,0x57
    };
    auto patch = original;
    patch[0] = 0xE9;
    int32_t displacement=static_cast<int32_t>(detour-entry-5);
    std::memcpy(patch.data()+1, &displacement, sizeof(displacement));
    assert(Classify(patch.data(),patch.size(),entry,detour,
                    original.data(),original.size()) == State::Owned);
    assert(Classify(original.data(),original.size(),entry,detour,
                    original.data(),original.size()) == State::Pristine);
    const uintptr_t foreign = 0x66004000;
    displacement = static_cast<int32_t>(foreign-entry-5);
    std::memcpy(patch.data()+1, &displacement, sizeof(displacement));
    assert(Classify(patch.data(),patch.size(),entry,detour,
                    original.data(),original.size()) == State::Foreign);
    assert(Classify(nullptr,0,entry,detour,
                    original.data(),original.size()) == State::Unreadable);
    auto corrupted=original;corrupted[5]=0x90;
    assert(Classify(corrupted.data(),corrupted.size(),entry,detour,
                    original.data(),original.size()) == State::Foreign);
}
