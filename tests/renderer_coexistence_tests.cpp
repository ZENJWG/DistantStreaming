#include "asc/RendererCoexistence.h"
#include <cassert>

int main() {
    using asc::renderer_coexistence::enableDistantFrameProbe;
    static_assert(enableDistantFrameProbe(true, false));
    static_assert(!enableDistantFrameProbe(true, true));
    static_assert(!enableDistantFrameProbe(false, false));
    static_assert(!enableDistantFrameProbe(false, true));
    assert(enableDistantFrameProbe(true, false));
}
