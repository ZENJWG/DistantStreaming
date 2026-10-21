#include "asc/HookEntryPolicy.h"
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace asc::hook_entry;
int main() {
    const uint8_t clean[]{0x55,0x8B,0xEC,0x83,0xEC,0x08,0,0};
    assert(!Inspect(clean,sizeof clean,0x554230).Patched());
    const uint8_t nearJump[]{0xE9,0x3B,0xBE,0x6D,0x1C,0x90,0x90,0x90};
    const auto near=Inspect(nearJump,sizeof nearJump,0x554230);
    assert(near.kind==Kind::RelativeJmp32 && near.directTarget==0x1CC30070);
    const uint8_t backwards[]{0xEB,0xF9};
    const auto shortJump=Inspect(backwards,sizeof backwards,0x1000);
    assert(shortJump.kind==Kind::RelativeJmp8 && shortJump.directTarget==0xFFB);
    const uint8_t indirect[]{0xFF,0x25,0x10,0x40,0x8E,0x00};
    const auto ind=Inspect(indirect,sizeof indirect,0x2000);
    assert(ind.kind==Kind::AbsoluteIndirectJmp && ind.pointerSlot==0x8E4010);
    const uint8_t pushRet[]{0x68,0x00,0x10,0x00,0x10,0xC3};
    assert(Inspect(pushRet,sizeof pushRet,0x5000).directTarget==0x10001000);
    const uint8_t movJmp[]{0xB8,0x70,0x00,0x30,0x1C,0xFF,0xE0};
    assert(Inspect(movJmp,sizeof movJmp,0x5000).directTarget==0x1C300070);
    assert(!Inspect(indirect,1,0x2000).Patched());
    const uint8_t trap[]{0xCC};
    assert(Inspect(trap,1,0x2000).kind==Kind::TrapOrReturn);
    std::cout<<"hook entry classification passed\n";
}
