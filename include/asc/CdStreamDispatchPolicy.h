#pragma once
#include <cstdint>
#include <cstddef>

namespace asc::archive_io {
constexpr uint32_t kGtaCdSectorSize = 2048;
constexpr uint32_t kGtaCdFileIdShift = 24;
constexpr uint32_t kGtaCdSectorMask = 0x00ffffffu;
constexpr uint32_t kGtaCdMaxFileCount = 32;

struct CdRead {
    uint32_t fileId=0;
    uint32_t sectorOffset=0;
    uint64_t byteOffset=0;
    uint32_t byteCount=0;
};

constexpr bool DecodeCdRead(uint32_t packedPos, int sectors, uint32_t maxBytes,
                            CdRead& result) noexcept {
    result = {};
    const uint32_t id = packedPos >> kGtaCdFileIdShift;
    const uint32_t offset = packedPos & kGtaCdSectorMask;
    if(id >= kGtaCdMaxFileCount || sectors<=0 ||
       static_cast<uint64_t>(sectors)*kGtaCdSectorSize > maxBytes)
        return false;
    result = {id, offset, uint64_t(offset)*kGtaCdSectorSize,
              uint32_t(sectors)*kGtaCdSectorSize};
    return true;
}
// Cache substitution is permitted ONLY when native per-stream completion
// state is idle. Never replace a queued read or spoof an OVERLAPPED event.
constexpr bool CanServeCdStream(bool initialized, bool channelIdle,
                                bool destinationWritable, bool archiveValid,
                                uint32_t byteCount, uint32_t maxCopyBytes) noexcept {
    return initialized && channelIdle && destinationWritable &&
           archiveValid && byteCount>0 && byteCount<=maxCopyBytes;
}
}
