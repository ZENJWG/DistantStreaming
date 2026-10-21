#include "asc/NativeResidencyPolicy.h"
#include <cassert>
#include <cstdint>

int main(){
    using namespace asc::native_residency;
    Limits l{};
    const auto mib=[](uint64_t x){return x*MiB;};
    assert(!Update(false,l,false,mib(800),mib(1700),mib(1800),0));
    assert(!Update(false,l,true,mib(63),mib(1700),mib(1800),0));
    assert(!Update(false,l,true,mib(64),mib(1300),mib(1800),0));
    assert( Update(false,l,true,mib(64),mib(1700),mib(1800),0));
    assert( Update(true,l,true,mib(64),mib(1400),mib(1800),0)); // 400 < stop 512
    assert(!Update(true,l,true,mib(64),mib(1200),mib(1800),0)); // 600 >= stop
    assert(!Update(true,l,true,mib(64),mib(1750),mib(1800),25));
    assert(!Update(true,l,true,mib(0),mib(1750),mib(1800),0));
    assert( Update(false,l,true,mib(100),mib(1850),mib(1800),0)); // saturated
    assert(!Update(false,l,true,mib(100),mib(1),0,0));
    assert(UnloadCandidate(0,true,0,false,10,4096));
    assert(!UnloadCandidate(1,true,0,false,10,4096));
    assert(!UnloadCandidate(0,false,0,false,10,4096));
    assert(!UnloadCandidate(0,true,8,false,10,4096));
    assert(!UnloadCandidate(0,true,0,true,10,4096));
    assert(!UnloadCandidate(0,true,0,false,0,4096));
    assert(!UnloadCandidate(0,true,0,false,4097,4096));
    assert(!Stable(0,10000,5000));
    assert(!Stable(500,5499,5000));
    assert(Stable(500,5500,5000));
    assert(Stable(UINT32_MAX-100u,5000u,5000)); // wrapping clock
    return 0;
}
