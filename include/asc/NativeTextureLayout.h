#pragma once
#include "asc/NativeMemoryProtocol.h"
#include <array>
#include <algorithm>
namespace asc::native {
struct Level {uint32_t width=0,height=0,pitch=0,rows=0;uint64_t offset=0,bytes=0;};
struct Layout {std::array<Level,14> levels{};unsigned count=0,block=1,unit=0;uint64_t bytes=0;};
inline Layout TextureLayout(uint32_t width,uint32_t height,unsigned levels,uint32_t format){
    Layout out;
    if(!width||!height||width>8192||height>8192)return out;
    switch(format){
    case 0x31545844:out.block=4;out.unit=8;break;
    case 0x33545844:case 0x35545844:out.block=4;out.unit=16;break;
    case 21:case 22:case 32:case 33:out.unit=4;break; // 32-bit RGBA/XRGB
    case 20:out.unit=3;break;
    case 23:case 24:case 25:case 26:case 51:case 60:out.unit=2;break;
    case 28:case 50:out.unit=1;break;
    default:return out; // palette, depth, float and vendor formats use vanilla
    }
    unsigned full=1;for(unsigned n=std::max(width,height);n>1;n/=2)++full;
    if(!levels)levels=full;
    if(levels>full)return {};
    for(unsigned i=0;i<levels;++i){
        auto& l=out.levels[i];l.width=width;l.height=height;
        l.pitch=(((width+out.block-1)/out.block*out.unit)+3)&~3u;
        l.rows=(height+out.block-1)/out.block;
        l.offset=out.bytes;l.bytes=uint64_t(l.pitch)*l.rows;out.bytes+=l.bytes;
        width=std::max(1u,width/2);height=std::max(1u,height/2);
    }
    if(out.bytes>MaxResource)return {};
    out.count=levels;return out;
}
}
