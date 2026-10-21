#pragma once
#include <cstdint>
#include <limits>

namespace asc::hd {
// Policy only: never owns or releases D3D resources.
// An actively loaded TXD may have textures that haven't been rendered
// recently. Release those HD replacements after the grace window to make
// room for currently visible textures; vanilla resources are untouched.
inline bool EligibleForEviction(bool sourceIsLive, uint32_t now,
                                uint32_t lastBound, uint32_t graceMs) {
    return !sourceIsLive || (lastBound != 0 &&
           uint32_t(now - lastBound) >= graceMs);
}
inline bool HasCapacity(uint64_t used, uint64_t incoming,
                        uint64_t budget) {
    return incoming <= budget && used <= budget - incoming;
}
inline bool RetryReady(uint32_t now, uint32_t after) {
    return after == 0 || int32_t(now - after) >= 0;
}
}
