#include "TargetValidation.h"
#include <windows.h>
#include <sstream>
#include <iomanip>
namespace {
bool executableAddress(uintptr_t p) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<const void*>(p), &mbi, sizeof(mbi))) return false;
    const DWORD prot=mbi.Protect & 0xFF;
    return mbi.State==MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD|PAGE_NOACCESS)) &&
        (prot==PAGE_EXECUTE || prot==PAGE_EXECUTE_READ ||
         prot==PAGE_EXECUTE_READWRITE || prot==PAGE_EXECUTE_WRITECOPY);
}
bool validAddress(uintptr_t p) {
    if (!executableAddress(p)) return false;
    const auto op=*reinterpret_cast<const unsigned char*>(p);
    return op!=0xE9 && op!=0xEB && op!=0xCC && op!=0xC3;
}
}
TargetValidation ValidateGameTarget(bool /*checkArchive*/) {
    TargetValidation v;
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if(base!=0x400000){v.reason="Unexpected executable image base";return v;}
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<64||dos->e_lfanew>4096){
        v.reason="Invalid DOS header";return v;
    }
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE ||
       nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
       nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
       nt->OptionalHeader.ImageBase!=0x400000 ||
       nt->OptionalHeader.SizeOfImage<0x500000){
        v.reason="Unsupported 32-bit image";return v;
    }
    // Hook checks are independent: Hooks::Install reports individual failures.
    // A failing optional hook must not reject the entire executable.
    v.ok=true;
    v.reason="PE image accepted; individual hook checks deferred to installer";
    return v;
}
bool IsArchiveHookAvailable(){return validAddress(0x408E20);}
