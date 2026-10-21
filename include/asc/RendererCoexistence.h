#pragma once

namespace asc::renderer_coexistence {

// SanFrame already owns device Present/EndScene diagnostics and pacing.
// Avoid adding DistantStreaming's optional probe to the same COM call chain.
// GPU allocation tracking / managed-priority hooks are unaffected.
constexpr bool enableDistantFrameProbe(bool requested,
                                        bool sanFrameLoaded) noexcept {
    return requested && !sanFrameLoaded;
}

} // namespace asc::renderer_coexistence
