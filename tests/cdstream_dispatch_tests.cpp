#include "asc/CdStreamDispatchPolicy.h"
#include <cassert>
#include <cstdint>
int main(){
    using namespace asc::archive_io;
    CdRead r{};
    assert(DecodeCdRead(0x02001000u,16,1048576,r));
    assert(r.fileId==2 && r.sectorOffset==4096);
    assert(r.byteOffset==8388608ull && r.byteCount==32768);
    assert(!DecodeCdRead(0x20001000u,16,1048576,r));
    assert(!DecodeCdRead(0x00000000u,0,1048576,r));
    assert(!DecodeCdRead(0x00000000u,-1,1048576,r));
    assert(!DecodeCdRead(0x00000000u,65536,1048576,r));
    assert(CanServeCdStream(true,true,true,true,32768,524288));
    assert(!CanServeCdStream(false,true,true,true,32768,524288));
    assert(!CanServeCdStream(true,false,true,true,32768,524288));
    assert(!CanServeCdStream(true,true,false,true,32768,524288));
    assert(!CanServeCdStream(true,true,true,false,32768,524288));
    assert(!CanServeCdStream(true,true,true,true,1048576,524288));
}
