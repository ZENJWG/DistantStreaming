#include "asc/ObservedSourceWarmPolicy.h"
#include <cassert>
#include <cstdint>
#include <limits>

using namespace asc::observed_warm;
int main() {
    constexpr uint64_t MiB=1048576ull;
    Limits limit{512*MiB,2048*MiB,64};
    assert(!Admit(Kind::Unsupported,MiB,limit,{}));
    assert(!Admit(Kind::Img,0,limit,{}));
    assert(!Admit(Kind::Img,513*MiB,limit,{}));
    assert(Admit(Kind::Img,512*MiB,limit,{}));
    assert(Admit(Kind::Loose,8*MiB,limit,{512*MiB,1}));
    assert(Admit(Kind::Img,512*MiB,limit,{1536*MiB,63}));
    assert(!Admit(Kind::Img,1,limit,{2048*MiB,12}));
    assert(!Admit(Kind::Img,1,limit,{1,64}));
    assert(!Admit(Kind::Loose,256*MiB,limit,{2000*MiB,1}));
    assert(!Admit(Kind::Img,MiB,limit,{UINT64_MAX,1}));
    assert(!Admit(Kind::Img,MiB,Limits{0,2048*MiB,10},{}));
    assert(!Admit(Kind::Img,MiB,Limits{MiB,0,10},{}));
    assert(!Admit(Kind::Img,MiB,Limits{MiB,MiB,0},{}));
}
