#pragma once
#include <cstdint>
#include <cstddef>
#include <string_view>

namespace asc::modloader_cache {

// The std.stream worker reads both IMG sectors and loose replacements from
// ACTUAL Windows HANDLEs. Do not guess provenance from GTA's packed sector.
enum class SourceKind : uint8_t { Unsupported, Loose, Img };
inline bool EndsWith(std::wstring_view path,std::wstring_view ext) noexcept {
    if(path.size()<ext.size())return false;
    for(size_t i=0;i<ext.size();++i){
        const auto c=path[path.size()-ext.size()+i];
        const auto lower=(c>=L'A'&&c<=L'Z')?wchar_t(c+L'a'-L'A'):c;
        if(lower!=ext[i])return false;
    }
    return true;
}

inline bool LooseSourcePath(std::wstring_view path) noexcept {
    constexpr std::wstring_view folder = L"modloader";
    const auto lower = [](wchar_t c) noexcept {
        return (c >= L'A' && c <= L'Z') ? wchar_t(c + (L'a' - L'A')) : c;
    };
    const auto sep = [](wchar_t c) noexcept {return c == L'\\' || c == L'/';};
    bool directory = false;
    for (size_t i = 0; i + folder.size() + 1 < path.size(); ++i) {
        if (!sep(path[i])) continue;
        bool match = true;
        for (size_t j = 0; j < folder.size(); ++j) {
            if (lower(path[i+1+j]) != folder[j]) {match=false;break;}
        }
        if (match && sep(path[i+1+folder.size()])) {directory = true;break;}
    }
    if (!directory) return false;
    constexpr std::wstring_view suffixes[] = {L".dff",L".txd",L".col"};
    for (const auto ext:suffixes) {
        if (path.size() < ext.size()) continue;
        bool match = true;
        for (size_t j = 0; j < ext.size(); ++j) {
            if (lower(path[path.size()-ext.size()+j]) != ext[j]) {match=false;break;}
        }
        if (match) return true;
    }
    return false;
}

inline SourceKind ClassifySource(std::wstring_view path) noexcept {
    // IMG may reside in models/, anim/, or modloader/. The module-local
    // ReadFile hook proves the worker actually selected this HANDLE.
    if(EndsWith(path,L".img"))return SourceKind::Img;
    return LooseSourcePath(path)?SourceKind::Loose:SourceKind::Unsupported;
}
inline bool EligibleRead(SourceKind kind,uint64_t fileBytes,uint64_t offset,
                         uint32_t count,uint32_t maxBytes,bool overlapped,
                         bool writable) noexcept {
    if(!overlapped||!writable||!count||count>maxBytes||
       offset>fileBytes || count>fileBytes-offset)return false;
    if(kind==SourceKind::Loose)return offset==0 && count==fileBytes;
    // IMG sectors are 2048-byte aligned and must be read at the actual
    // offset in the selected physical archive.
    if(kind==SourceKind::Img)return (offset%2048)==0 && (count%2048)==0;
    return false;
}
// Only complete, exact loose-asset reads are cached. An arbitrary offset,
// short count, or missing OVERLAPPED is passed to the original OS function.
inline bool CompleteLooseRead(uint64_t fileBytes,uint64_t offset,uint32_t count,
                              uint32_t maxBytes,bool overlapped,bool writable) noexcept {
    return overlapped && writable && count != 0 && count <= maxBytes &&
           offset == 0 && uint64_t(count) == fileBytes;
}

} // namespace asc::modloader_cache
