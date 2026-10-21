# VS 2026 / Plugin-SDK C++23 compilation fix

Your build log completed:
- Plugin-SDK Release -> `output/lib/Plugin.lib`
- vcpkg `minhook:x86-windows-static` installation
- CMake configure

The failure occurred while compiling the `DistantStreaming` game target:
- `injector/injector.hpp`: `valid_pointer_type` is a C++20 concept.
- `safetyhook/safetyhook.hpp`: `std::expected` requires C++23.

Changes:
- Root `CMakeLists.txt`: C++ language standard raised from 17 to 23.
- ASI target explicitly requires `cxx_std_23`.
- `tools/bootstrap-deps.ps1`: passes `-DCMAKE_CXX_STANDARD=23`.

No Plugin-SDK or LoadWholeMap sources were modified.

Run `build.bat` again from the project root. If the existing CMake cache
contains conflicting settings or compilation still displays `/std:c++17`:

    rmdir /s /q build-vs2026-win32
    set ASC_SKIP_UPGRADES=1
    build.bat

The `rmdir` command only removes the generated build directory, not `.deps`.
`ASC_SKIP_UPGRADES=1` avoids re-downloading/updating dependency versions.

Validated locally:
- GCC 14.2 C++23 portable build: success.
- CTest `core_logic`: passed.
- CTest `hook_ring_stress`: passed.
- LoadWholeMap separation audit: passed.

Not validated: Windows MSVC x86 ASI compilation, final linker and GTA SA runtime.
