#include "asc/HdDds.h"
#include <cassert>
#include <array>
#include <cstring>
#include <iostream>

static void Set32(std::array<unsigned char,128>& h,size_t at,uint32_t value){
    std::memcpy(h.data()+at,&value,4);
}
int main(){
    using namespace asc::hd;
    Asset d{};
    uint64_t bytes=0;
    d={4096,4096,13,0x35545844,22369648};
    assert(ValidBCLayout(d,bytes)&&bytes==22369648); // full BC3 mip chain
    d.byteCount--;
    assert(!ValidBCLayout(d,bytes));
    d={4096,4096,13,0x31545844,11184824};
    assert(ValidBCLayout(d,bytes));
    d={1,1,2,0x31545844,16};
    assert(!ValidBCLayout(d,bytes)); // excessive mip count
    d={8192,8192,14,0x35545844,89478512};
    assert(!ValidBCLayout(d,bytes)); // exceeds 64 MiB pipe transfer
    std::array<unsigned char,128> raw{};
    std::memcpy(raw.data(),"DDS ",4);
    Set32(raw,4,124);Set32(raw,8,0x1007);Set32(raw,76,32);Set32(raw,80,4);
    Set32(raw,108,0x1000);
    Set32(raw,16,4);Set32(raw,12,4);Set32(raw,28,1);
    Set32(raw,84,0x31545844);
    assert(ParseDDS(raw.data(),136,d));
    assert(d.byteCount==8 && d.width==4);
    assert(!ParseDDS(raw.data(),135,d));
    Set32(raw,112,0x200); // cubemap flag
    assert(!ParseDDS(raw.data(),136,d));
    Set32(raw,112,0);
    Set32(raw,24,4); // volume texture
    assert(!ParseDDS(raw.data(),136,d));
    Set32(raw,24,0);
    Set32(raw,108,0); // missing DDSCAPS_TEXTURE
    assert(!ParseDDS(raw.data(),136,d));
    Set32(raw,108,0x1000);
    raw[0]='X';
    assert(!ParseDDS(raw.data(),136,d));
    std::cout<<"HD DDS layout tests passed\n";
}
