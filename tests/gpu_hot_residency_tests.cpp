#include "asc/GpuHotResidencyPolicy.h"
#include <cassert>
#include <cstdint>
int main() {
    using namespace asc::gpu_hot_residency;
    assert(!ShouldSampleBind(0,0));
    assert(!ShouldSampleBind(127,128));
    assert(ShouldSampleBind(128,128));
    assert(!ShouldSampleBind(129,128));
    const uint64_t M=1024ull*1024ull;
    auto a=Decide({uint64_t(900)*M,uint64_t(11231)*M,uint64_t(10240)*M,uint64_t(1536)*M,true});
    assert(a.allowSpeculativeUpload);
    assert(a.effectiveTargetBytes==uint64_t(8704)*M);
    assert(a.freeToTargetBytes==uint64_t(7804)*M);
    auto b=Decide({uint64_t(9000)*M,uint64_t(11231)*M,uint64_t(10240)*M,uint64_t(1536)*M,true});
    assert(!b.allowSpeculativeUpload && b.freeToTargetBytes==0);
    auto c=Decide({uint64_t(8000)*M,uint64_t(8192)*M,uint64_t(10240)*M,uint64_t(1536)*M,true});
    assert(!c.allowSpeculativeUpload && c.effectiveTargetBytes==uint64_t(6656)*M);
    auto d=Decide({0,0,uint64_t(10240)*M,uint64_t(1536)*M,false});
    assert(!d.allowSpeculativeUpload);
    auto e=Decide({0,uint64_t(1024)*M,uint64_t(10240)*M,uint64_t(1536)*M,true});
    assert(!e.allowSpeculativeUpload && e.effectiveTargetBytes==0);
    return 0;
}
