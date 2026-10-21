#pragma once
// Pure GTA SA IMG VER2 directory validation and observed-read extent resolution.
// A filename is a *dependency hint*; the live Mod Loader handle + exact read
// are the source authority. This file performs no filesystem or game access.
#include "asc/NativeMemoryProtocol.h"
#include "asc/ObservedDependencyWarmPolicy.h"
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

namespace asc::img_member {
constexpr uint64_t SectorBytes=2048;
constexpr uint32_t MaxEntries=150000;
constexpr uint64_t MaxDirectoryBytes=8ull+uint64_t(MaxEntries)*32ull;
constexpr uint64_t MaxExtentBytes=512ull*1024*1024;

struct Extent {
    std::string filename;
    uint64_t offset=0;
    uint64_t bytes=0;
};
inline uint16_t U16(const uint8_t* x) noexcept {
    return uint16_t(x[0]) | uint16_t(uint16_t(x[1])<<8);
}
inline uint32_t U32(const uint8_t* x) noexcept {
    return uint32_t(x[0]) | (uint32_t(x[1])<<8) |
           (uint32_t(x[2])<<16) | (uint32_t(x[3])<<24);
}
inline bool DirectorySize(std::span<const uint8_t> header,uint64_t fileSize,
                          size_t& size) noexcept {
    size=0;
    if(header.size()<8 || fileSize<8 || header[0]!='V' ||
       header[1]!='E' || header[2]!='R' || header[3]!='2')
        return false;
    const auto count=U32(header.data()+4);
    const uint64_t bytes=8ull+uint64_t(count)*32ull;
    if(!count || count>MaxEntries || bytes>fileSize)return false;
    size=static_cast<size_t>(bytes);
    return true;
}
inline bool Parse(std::span<const uint8_t> directory,uint64_t fileSize,
                  std::vector<Extent>& output) {
    output.clear();
    size_t required=0;
    if(!DirectorySize(directory,fileSize,required)||
       directory.size()!=required)return false;
    const uint32_t count=U32(directory.data()+4);
    // Never publish a partially validated directory.
    std::vector<Extent> candidates;
    candidates.reserve(count);
    for(uint32_t i=0;i<count;++i){
        const auto* r=directory.data()+8+size_t(i)*32;
        const uint64_t begin=uint64_t(U32(r))*SectorBytes;
        const uint64_t sectors=U16(r+6)?U16(r+6):U16(r+4);
        const uint64_t bytes=sectors*SectorBytes;
        if(!bytes || begin<required || begin>fileSize || bytes>fileSize-begin)
            return false;
        std::string filename;
        for(size_t j=0;j<24 && r[8+j];++j){
            const unsigned char c=r[8+j];
            if(c<' ' || c>126 || c=='/' || c=='\\' || c==':')return false;
            filename.push_back(char(c));
        }
        while(!filename.empty()&&filename.back()==' ')filename.pop_back();
        if(filename.empty())return false;
        candidates.push_back({std::move(filename),begin,bytes});
    }
    std::sort(candidates.begin(),candidates.end(),[](const Extent& a,const Extent& b){
        return a.offset<b.offset;
    });
    // Non-overlapping extents are required. Two IMG entries sharing physical
    // sectors cannot be safely associated with a single observed read.
    for(size_t i=1;i<candidates.size();++i)
        if(candidates[i-1].offset+candidates[i-1].bytes>
           candidates[i].offset)return false;
    output=std::move(candidates);
    return true;
}
enum class Decision : uint8_t { Match=0, NotSelected=1, Ambiguous=2, Invalid=3 };
// Exact read must be wholly within one and ONLY ONE IMG entry. Additionally
// that entry must match a validated per-cell DFF/TXD dependency hint.
inline Decision FindObserved(std::span<const Extent> members,
                             uint64_t readOffset,uint32_t readBytes,
                             const asc::native::CatalogCellReply& hints,
                             Extent& selected) noexcept {
    if(!readBytes || hints.modelCount==0 || hints.modelCount>64 ||
       readOffset>UINT64_MAX-readBytes)return Decision::Invalid;
    const uint64_t readEnd=readOffset+readBytes;
    // Parse returns strictly ordered, non-overlapping extents.
    const auto upper=std::upper_bound(members.begin(),members.end(),readOffset,
        [](uint64_t offset,const Extent& m){return offset<m.offset;});
    if(upper==members.begin())return Decision::NotSelected;
    const auto* candidate=&*std::prev(upper);
    if(readOffset>=candidate->offset+candidate->bytes)return Decision::NotSelected;
    if(readEnd>candidate->offset+candidate->bytes)return Decision::Ambiguous;
    if(candidate->bytes>MaxExtentBytes)return Decision::Invalid;
    if(asc::dependency_warm::MatchLooseFile(candidate->filename,hints)!=
       asc::dependency_warm::Decision::Match)
        return Decision::NotSelected;
    selected=*candidate;
    return Decision::Match;
}
} // namespace asc::img_member
