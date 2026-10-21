#pragma once
#include <cstddef>
#include <cstdint>

namespace asc::archive_io {
// Source IDs and Win32 HANDLE values are ephemeral. Archive residency keys
// must remain stable across reopened handles, while invalidating on write.
struct FileIdentity {
    uint32_t volume=0,indexHigh=0,indexLow=0,sizeHigh=0,sizeLow=0;
    uint32_t writeHigh=0,writeLow=0;
    bool operator==(const FileIdentity&) const = default;
};
struct PageKey {
    FileIdentity file{};
    uint64_t index=0;
    bool operator==(const PageKey&) const = default;
};
struct PageHash {
    size_t operator()(const PageKey& k) const noexcept {
        uint64_t h=1469598103934665603ull;
        const auto mix=[&](uint64_t x){h^=x;h*=1099511628211ull;};
        mix(k.file.volume);mix(k.file.indexHigh);mix(k.file.indexLow);
        mix(k.file.sizeHigh);mix(k.file.sizeLow);
        mix(k.file.writeHigh);mix(k.file.writeLow);mix(k.index);
        return size_t(h);
    }
};
struct IdentityHash {
    size_t operator()(const FileIdentity& f) const noexcept {
        return PageHash{}(PageKey{f,0});
    }
};
}
