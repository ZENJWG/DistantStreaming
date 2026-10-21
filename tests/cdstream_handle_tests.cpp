#include "asc/CdStreamHandleGate.h"
#include <array>
#include <cassert>
#include <cstdint>
int main(){
    std::array<std::uintptr_t,32> sources{};
    for(std::size_t i=0;i<sources.size();++i)sources[i]=0x1000+i*4;
    assert(asc::archive_io::IsStreamHandle<std::uintptr_t>(
        sources[0],sources.data(),sources.size()));
    assert(asc::archive_io::IsStreamHandle<std::uintptr_t>(
        sources[31],sources.data(),sources.size()));
    assert(!asc::archive_io::IsStreamHandle<std::uintptr_t>(
        0,sources.data(),sources.size()));
    assert(!asc::archive_io::IsStreamHandle<std::uintptr_t>(
        0x9999,sources.data(),sources.size()));
    assert(!asc::archive_io::IsStreamHandle<std::uintptr_t>(
        sources[0],sources.data(),31));
    assert(!asc::archive_io::IsStreamHandle<std::uintptr_t>(
        sources[0],nullptr,32));
}
