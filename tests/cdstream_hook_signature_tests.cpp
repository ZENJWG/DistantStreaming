#include "asc/CdStreamHookSignature.h"
#include <array>
#include <cassert>
int main(){
    using namespace asc::archive_io;
    auto stub=HoodlumCdReadEntry;
    auto body=HoodlumCdReadBodyPrefix;
    assert(IsKnownHoodlumCdRead(stub.data(),body.data()));
    assert(!IsKnownHoodlumCdRead(nullptr,body.data()));
    assert(!IsKnownHoodlumCdRead(stub.data(),nullptr));
    stub[1]^=0x01;
    assert(!IsKnownHoodlumCdRead(stub.data(),body.data()));
    stub=HoodlumCdReadEntry;
    body[0]^=0x01;
    assert(!IsKnownHoodlumCdRead(stub.data(),body.data()));
    body=HoodlumCdReadBodyPrefix;
    body[14]^=0x01;
    assert(!IsKnownHoodlumCdRead(stub.data(),body.data()));
}
