#pragma once
#include <cstddef>

namespace asc::archive_io {
// Whitelist a live GTA CdStream HANDLE, never every process-wide ReadFile.
// The caller validates that this array belongs to the supported GTA image.
template<class Handle>
constexpr bool IsStreamHandle(Handle candidate,const Handle* handles,
                              std::size_t count) noexcept {
    if(!candidate||!handles||count!=32)return false;
    for(std::size_t i=0;i<count;++i)
        if(handles[i]==candidate)return true;
    return false;
}
} // namespace asc::archive_io
