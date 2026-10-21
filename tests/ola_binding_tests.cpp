#include "asc/OlaBindings.h"
#include <cassert>
#include <cstdint>

int main() {
    constexpr uint8_t original[] = {0x8B,0x0C,0x85,0xC8,0xB0,0xA9,0x00};
    constexpr uint8_t relocated[] = {0x8B,0x0C,0x85,0x20,0x30,0x1B,0x07};
    constexpr uint8_t jmp[] = {0xE9,0xC8,0xB0,0xA9,0x00,0x90,0x90};
    constexpr uint8_t nop[] = {0x90,0x90,0x90,0xC8,0xB0,0xA9,0x00};
    static_assert(asc::ola::DecodeEntityModelTable(original,7)==0xA9B0C8u);
    static_assert(asc::ola::DecodeEntityModelTable(relocated,7)==0x071B3020u);
    static_assert(asc::ola::DecodeEntityModelTable(jmp,7)==0);
    static_assert(asc::ola::DecodeEntityModelTable(nop,7)==0);
    static_assert(asc::ola::DecodeEntityModelTable(original,5)==0);
    static_assert(asc::ola::ValidModelCount(50000));
    static_assert(!asc::ola::ValidModelCount(0));
    static_assert(!asc::ola::ValidModelCount(200000));
    static_assert(!asc::ola::ShouldManageNativeMemory(true,true));
    static_assert(asc::ola::ShouldManageNativeMemory(true,false));
    static_assert(asc::ola::ShouldManageNativeMemory(true,true,true));
    static_assert(!asc::ola::ShouldManageNativeMemory(false,true,true));
    assert(asc::ola::DecodeEntityModelTable(original,7) == 0xA9B0C8u);
}
