#pragma once
#include "asc/HdTextureProtocol.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace asc::hd {
inline uint32_t ReadU32(const unsigned char* p){
    uint32_t v=0;std::memcpy(&v,p,sizeof(v));return v;
}
inline bool ValidBCLayout(const Asset&a,uint64_t& expected){
    expected=0;
    if(!a.width||!a.height||a.width>8192||a.height>8192||
       !a.mipCount||a.mipCount>14||
       (a.fourCC!=0x31545844 && a.fourCC!=0x33545844 &&
        a.fourCC!=0x35545844))return false;
    uint32_t maxSize=std::max(a.width,a.height),maxMip=1;
    while(maxSize>1){maxSize/=2;++maxMip;}
    if(a.mipCount>maxMip)return false;
    uint32_t w=a.width,h=a.height;
    for(unsigned i=0;i<a.mipCount;++i){
        expected+=uint64_t((w+3)/4)*((h+3)/4)*
                  (a.fourCC==0x31545844?8:16);
        w=std::max(1u,w/2);h=std::max(1u,h/2);
    }
    return expected==a.byteCount && expected<=kMaxPipeAssetBytes;
}
inline bool ParseDDS(const unsigned char* data,size_t bytes,Asset&out){
    if(!data||bytes<128||std::memcmp(data,"DDS ",4)!=0||
       ReadU32(data+4)!=124||ReadU32(data+76)!=32||
       (ReadU32(data+80)&4)!=4 ||
       (ReadU32(data+8)&0x1007)!=0x1007 || // mandatory DDS header flags
       (ReadU32(data+108)&0x1000)==0 || // DDSCAPS_TEXTURE
       ReadU32(data+24)>1 || // reject volume textures
       ReadU32(data+112)!=0)return false; // reject cubemaps and arrays
    out={ReadU32(data+16),ReadU32(data+12),
         std::max(1u,ReadU32(data+28)),ReadU32(data+84),0};
    uint64_t expected=0;
    // Compute expected size independently of the descriptor's byte count.
    out.byteCount=0;
    if(!out.width||!out.height||out.width>8192||out.height>8192||
       out.mipCount>14)return false;
    uint32_t w=out.width,h=out.height;
    for(unsigned i=0;i<out.mipCount;++i){
        out.byteCount+=uint64_t((w+3)/4)*((h+3)/4)*
                       (out.fourCC==0x31545844?8:16);
        w=std::max(1u,w/2);h=std::max(1u,h/2);
    }
    return ValidBCLayout(out,expected) && bytes==128+expected;
}
}
