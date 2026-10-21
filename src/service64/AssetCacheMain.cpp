#include "NativeMemoryService.h"
// Self-contained x64 CPU asset cache. No D3D9 device, rendering hooks,
// game pointers or fixed 32-bit address-space assumptions.
int wmain(int argc,wchar_t** argv){
    return NativeMemoryService(argc,argv);
}
