#include "asc/NativeMemoryProtocol.h"
#include <cassert>
#include <string>
int main() {
    const auto p = asc::native::PipeName(L"test");
    const auto m = asc::native::MappingName(L"test", 42);
    assert(p == L"\\\\.\\pipe\\DistantStreaming.NativeMemory.test");
    assert(m == L"Local\\DistantStreaming.NativeMemory.test.42");
    static_assert(asc::native::Version == 10);
}
