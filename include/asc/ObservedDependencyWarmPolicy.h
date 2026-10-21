#pragma once
// V5: read-only dependency matching for a source selected through Mod Loader's
// own ReadFile hook. Never treats gta.dat, IMG directories, or filenames on disk
// as proof of effective Mod Loader precedence.
#include "asc/NativeMemoryProtocol.h"
#include <cctype>
#include <cstddef>
#include <string_view>

namespace asc::dependency_warm {
enum class Decision : unsigned char {
    Match=0, NoHints=1, Unsupported=2, NoMatch=3
};
inline std::string_view Bounded(const char* p,size_t n) noexcept {
    size_t length=0;
    while(length<n && p[length])++length;
    return std::string_view(p,length);
}
inline bool SameAscii(std::string_view a,std::string_view b) noexcept {
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i){
        const unsigned char x=static_cast<unsigned char>(a[i]);
        const unsigned char y=static_cast<unsigned char>(b[i]);
        if(x>127 || y>127 ||
           std::tolower(x)!=std::tolower(y))return false;
    }
    return true;
}
inline bool MatchesStem(std::string_view stem,const char* p,size_t n) noexcept {
    const auto name=Bounded(p,n);
    return !name.empty() && name.size()<n && SameAscii(stem,name);
}
// filename must be the basename of a *live, actually read* Mod Loader
// file HANDLE. This matcher is NOT sufficient to authorize opening a file.
inline Decision MatchLooseFile(std::string_view filename,
                               const asc::native::CatalogCellReply& hints) noexcept {
    if(!hints.modelCount || hints.modelCount>64)return Decision::NoHints;
    if(filename.empty() || filename.size()>260)return Decision::Unsupported;
    for(unsigned char c:filename)
        if(c>127 || c=='/' || c=='\\' || c==':' || c<32)
            return Decision::Unsupported;
    if(filename.size()<5)return Decision::Unsupported;
    const std::string_view suffix=filename.substr(filename.size()-4);
    const auto stem=filename.substr(0,filename.size()-4);
    const bool dff=SameAscii(suffix,".dff");
    const bool txd=SameAscii(suffix,".txd");
    if(!dff && !txd)return Decision::Unsupported;
    for(uint32_t i=0;i<hints.modelCount;++i){
        if(dff && MatchesStem(stem,hints.modelNames[i],24))
            return Decision::Match;
        if(txd && MatchesStem(stem,hints.txdNames[i],24))
            return Decision::Match;
    }
    return Decision::NoMatch;
}
} // namespace asc::dependency_warm
