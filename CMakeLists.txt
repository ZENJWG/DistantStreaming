cmake_minimum_required(VERSION 3.21)
project(DistantStreamingSA VERSION 0.1.0 LANGUAGES CXX)

# Plugin-SDK's premake Release library is built with the static MSVC CRT (/MT).
# Keep ASI, core tests and MinHook on the same CRT to avoid LNK2038 mismatches.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
# Current Plugin-SDK headers use C++20 concepts and C++23 std::expected.
# A C++17 ASI triggers hundreds of misleading errors in injector/safetyhook.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(ASC_BUILD_TESTS "Build portable algorithm tests" ON)
option(ASC_BUILD_ASI "Build game ASI plugin (MSVC Win32 only)" ${WIN32})

add_library(asc_core STATIC src/core/Core.cpp src/core/ArchivePreload.cpp src/core/WorldCatalog.cpp src/core/WorldCatalogCache.cpp)
target_include_directories(asc_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
if(MSVC)
    target_compile_options(asc_core PRIVATE /W4 /EHsc)
else()
    target_compile_options(asc_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# 64-bit asset-cache helper is built by the existing build.bat x64 configure.
# The ASI remains x86 and the helper does not link to Plugin-SDK.
if(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_executable(DistantStreaming_AssetCache64
        src/service64/AssetCacheMain.cpp
        src/service64/NativeMemoryService.cpp
        src/service64/AssetCache.cpp)
    target_include_directories(DistantStreaming_AssetCache64 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(DistantStreaming_AssetCache64 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(DistantStreaming_AssetCache64 PRIVATE /W4 /EHsc /MP)
    target_link_libraries(DistantStreaming_AssetCache64 PRIVATE asc_core)
endif()

if(ASC_BUILD_TESTS)
    enable_testing()
    add_executable(asc_tests tests/core_tests.cpp)
    target_link_libraries(asc_tests PRIVATE asc_core)
    add_test(NAME core_logic COMMAND asc_tests)
    find_package(Threads REQUIRED)
    add_executable(asc_world_catalog_persistence_tests tests/world_catalog_persistence_tests.cpp)
    target_link_libraries(asc_world_catalog_persistence_tests PRIVATE asc_core)
    add_test(NAME world_catalog_persistent_cache COMMAND asc_world_catalog_persistence_tests)
    add_executable(asc_catalog_native_prefetch_tests tests/catalog_native_prefetch_tests.cpp)
    target_include_directories(asc_catalog_native_prefetch_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME bounded_catalog_native_prefetch COMMAND asc_catalog_native_prefetch_tests)
    add_executable(asc_world_catalog_tests tests/world_catalog_tests.cpp)
    target_link_libraries(asc_world_catalog_tests PRIVATE asc_core)
    add_test(NAME world_catalog_static_inventory COMMAND asc_world_catalog_tests)
    add_executable(asc_world_cache_policy_tests tests/world_cache_policy_tests.cpp)
    target_include_directories(asc_world_cache_policy_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME world_cache_policy COMMAND asc_world_cache_policy_tests)
    add_executable(asc_readable_region_cache_tests tests/readable_region_cache_tests.cpp)
    target_include_directories(asc_readable_region_cache_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME renderer_region_cache_policy COMMAND asc_readable_region_cache_tests)
    add_executable(distant_renderer_coexistence_tests tests/renderer_coexistence_tests.cpp)
    target_include_directories(distant_renderer_coexistence_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME renderer_coexistence_policy COMMAND distant_renderer_coexistence_tests)
    add_executable(distant_streaming_name_tests tests/StreamingCoexistence.cpp)
    target_include_directories(distant_streaming_name_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME distant_streaming_module_names COMMAND distant_streaming_name_tests)
    add_executable(distant_streaming_protocol_tests tests/rebrand_protocol_tests.cpp)
    target_include_directories(distant_streaming_protocol_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME distant_streaming_protocol_prefix COMMAND distant_streaming_protocol_tests)
    add_executable(asc_img_member_extent_tests tests/img_member_extent_tests.cpp)
    target_include_directories(asc_img_member_extent_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME verified_img_member_extent COMMAND asc_img_member_extent_tests)
    add_executable(asc_observed_dependency_warm_tests tests/observed_dependency_warm_tests.cpp)
    target_include_directories(asc_observed_dependency_warm_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME observed_modloader_dependency_warm_policy COMMAND asc_observed_dependency_warm_tests)
    add_executable(asc_observed_source_warm_tests tests/observed_source_warm_tests.cpp)
    target_include_directories(asc_observed_source_warm_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME observed_modloader_source_warm_policy COMMAND asc_observed_source_warm_tests)
    add_executable(asc_world_warm_policy_tests tests/world_warm_policy_tests.cpp)
    target_include_directories(asc_world_warm_policy_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME world_warm_policy COMMAND asc_world_warm_policy_tests)
    if(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(asc_asset_cache_windows_tests
            tests/asset_cache_windows_tests.cpp src/service64/AssetCache.cpp)
        target_include_directories(asc_asset_cache_windows_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/src/service64")
        target_compile_definitions(asc_asset_cache_windows_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        add_test(NAME asset_cache_windows COMMAND asc_asset_cache_windows_tests)
    endif()
    add_executable(asc_ring_tests tests/ring_tests.cpp)
    target_include_directories(asc_ring_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(asc_ring_tests PRIVATE Threads::Threads)
    add_test(NAME hook_ring_stress COMMAND asc_ring_tests)
    add_executable(asc_archive_preload_tests tests/archive_preload_tests.cpp)
    target_link_libraries(asc_archive_preload_tests PRIVATE asc_core Threads::Threads)
    add_test(NAME archive_binary_ipl_cache COMMAND asc_archive_preload_tests)
    add_executable(asc_archive_io_tests tests/archive_read_policy_tests.cpp)
    target_include_directories(asc_archive_io_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME archive_io_modes COMMAND asc_archive_io_tests)
    add_executable(asc_cdstream_dispatch_tests tests/cdstream_dispatch_tests.cpp)
    target_include_directories(asc_cdstream_dispatch_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME gta_cdstream_dispatch COMMAND asc_cdstream_dispatch_tests)
    add_executable(asc_modloader_read_gate_tests tests/modloader_read_gate_tests.cpp)
    target_include_directories(asc_modloader_read_gate_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME modloader_read_gate COMMAND asc_modloader_read_gate_tests)
    add_executable(asc_cdstream_handle_tests tests/cdstream_handle_tests.cpp)
    target_include_directories(asc_cdstream_handle_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME gta_cdstream_handle_gate COMMAND asc_cdstream_handle_tests)
    add_executable(asc_cdstream_signature_tests tests/cdstream_hook_signature_tests.cpp)
    target_include_directories(asc_cdstream_signature_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME gta_cdstream_hoodlum_fingerprint COMMAND asc_cdstream_signature_tests)
    add_executable(asc_archive_tests tests/archive_sampling_tests.cpp)
    target_include_directories(asc_archive_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME archive_sampling COMMAND asc_archive_tests)
    add_executable(asc_dynamic_tests tests/dynamic_tests.cpp)
    target_link_libraries(asc_dynamic_tests PRIVATE asc_core)
    add_test(NAME dynamic_readiness COMMAND asc_dynamic_tests)
    add_executable(asc_memory_tests tests/memory_tests.cpp)
    target_link_libraries(asc_memory_tests PRIVATE asc_core)
    add_test(NAME streaming_memory COMMAND asc_memory_tests)
    add_executable(asc_frame_stability_tests tests/frame_stability_policy_tests.cpp)
    target_link_libraries(asc_frame_stability_tests PRIVATE asc_core)
    add_test(NAME frame_stability_speculative_pacing COMMAND asc_frame_stability_tests)
    add_executable(asc_frame_pacing_probe_tests tests/frame_pacing_probe_tests.cpp)
    target_include_directories(asc_frame_pacing_probe_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME cpu_frame_pacing_probe COMMAND asc_frame_pacing_probe_tests)
    add_executable(asc_render_cost_probe_tests tests/render_cost_probe_tests.cpp)
    target_include_directories(asc_render_cost_probe_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME cpu_render_stage_and_draw_sampling COMMAND asc_render_cost_probe_tests)
    add_executable(asc_request_call_profile_tests tests/request_call_profile_tests.cpp)
    target_include_directories(asc_request_call_profile_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME sampled_request_call_profile COMMAND asc_request_call_profile_tests)
    add_executable(asc_stream_flow_probe_tests tests/stream_flow_probe_tests.cpp)
    target_link_libraries(asc_stream_flow_probe_tests PRIVATE asc_core)
    add_test(NAME passive_stream_flow_probe COMMAND asc_stream_flow_probe_tests)
    add_executable(asc_observation_tests tests/observation_tests.cpp)
    target_link_libraries(asc_observation_tests PRIVATE asc_core Threads::Threads)
    add_test(NAME request_observations COMMAND asc_observation_tests)
    add_executable(asc_world_tests tests/world_readiness_tests.cpp)
    target_link_libraries(asc_world_tests PRIVATE asc_core)
    add_test(NAME world_readiness COMMAND asc_world_tests)
    add_executable(asc_camera_tests tests/camera_readiness_tests.cpp)
    target_link_libraries(asc_camera_tests PRIVATE asc_core)
    add_test(NAME camera_readiness COMMAND asc_camera_tests)
    add_executable(asc_native_reclaim_tests tests/native_residency_policy_tests.cpp)
    target_include_directories(asc_native_reclaim_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME helper_first_native_reclaim_policy COMMAND asc_native_reclaim_tests)
    add_executable(asc_cleanup_tests tests/offscreen_cleanup_tests.cpp)
    target_link_libraries(asc_cleanup_tests PRIVATE asc_core)
    add_test(NAME offscreen_cleanup COMMAND asc_cleanup_tests)
    add_executable(asc_hd_dds_tests tests/hd_dds_tests.cpp)
    target_include_directories(asc_hd_dds_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME hd_dds_layout COMMAND asc_hd_dds_tests)
    add_executable(asc_gpu_hot_residency_tests tests/gpu_hot_residency_tests.cpp)
    target_include_directories(asc_gpu_hot_residency_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME rtx4070super_gpu_residency_policy COMMAND asc_gpu_hot_residency_tests)
    add_executable(asc_hd_residency_tests tests/hd_residency_tests.cpp)
    target_include_directories(asc_hd_residency_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME hd_residency_policy COMMAND asc_hd_residency_tests)
    add_executable(asc_hook_entry_policy_tests tests/hook_entry_policy_tests.cpp)
    target_include_directories(asc_hook_entry_policy_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME foreign_renderer_hook_entry_detection COMMAND asc_hook_entry_policy_tests)
    add_executable(asc_hook_integrity_tests tests/hook_integrity_tests.cpp)
    target_include_directories(asc_hook_integrity_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME hook_integrity_policy COMMAND asc_hook_integrity_tests)
    add_executable(asc_ola_binding_tests tests/ola_binding_tests.cpp)
    target_include_directories(asc_ola_binding_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME ola_binding_decode COMMAND asc_ola_binding_tests)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME readiness_crash_source_guard
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_readiness_crash_guard.py")
        add_test(NAME active_ola_ini_bounds
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_active_ini_profile.py")
        add_test(NAME no_drawdistance_ownership
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_no_drawdistance.py")
        add_test(NAME world_source_manifest
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_world_source_manifest.py")
    endif()
    if(WIN32)
        find_program(ASC_POWERSHELL_EXECUTABLE NAMES powershell pwsh)
        if(ASC_POWERSHELL_EXECUTABLE)
            add_test(NAME cache64_build_dependency_tracking
                COMMAND "${ASC_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/cache64_build_state_tests.ps1"
                    -WorkDirectory "${CMAKE_CURRENT_BINARY_DIR}")
        endif()
        add_executable(asc_world_region_windows_tests tests/world_region_windows_tests.cpp)
        target_include_directories(asc_world_region_windows_tests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/src/game")
        target_compile_definitions(asc_world_region_windows_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        add_test(NAME windows_world_region_validation COMMAND asc_world_region_windows_tests)
        add_executable(asc_config_tests tests/config_tests.cpp src/game/ConfigWin.cpp)
        target_include_directories(asc_config_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/game")
        target_compile_definitions(asc_config_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(asc_config_tests PRIVATE asc_core)
        add_test(NAME windows_config COMMAND asc_config_tests)
    endif()
    # CMake Release adds NDEBUG; retain assertions in existing portable tests.
    # Otherwise a Release CTest pass can be a no-op for assert-based suites.
    # Discover test targets so newly added suites cannot silently lose their
    # assertions because a second, manually maintained list was not updated.
    get_property(_asc_directory_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_asc_test IN LISTS _asc_directory_targets)
        if(_asc_test MATCHES "_tests$")
            if(MSVC)
                target_compile_options(${_asc_test} PRIVATE /UNDEBUG)
            else()
                target_compile_options(${_asc_test} PRIVATE -UNDEBUG)
            endif()
        endif()
    endforeach()
endif()

if(ASC_BUILD_ASI)
    if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "ASI requires MSVC and -A Win32 (32-bit Windows)")
    endif()
    set(PLUGIN_SDK_ROOT "" CACHE PATH "DK22Pac/plugin-sdk checkout")
    set(PLUGIN_SDK_SA_LIB "" CACHE FILEPATH "Compiled Plugin-SDK SA Plugin.lib")
    set(PLUGIN_SDK_EXTRA_LIBS "" CACHE STRING "Any additional Plugin-SDK libraries")
    if(NOT EXISTS "${PLUGIN_SDK_ROOT}/plugin_sa/game_sa/CStreaming.h")
        message(FATAL_ERROR "PLUGIN_SDK_ROOT does not point to plugin-sdk checkout")
    endif()
    if(NOT EXISTS "${PLUGIN_SDK_SA_LIB}")
        message(FATAL_ERROR "Set PLUGIN_SDK_SA_LIB to the built Plugin.lib from Plugin-SDK")
    endif()
    # Plugin-SDK Premake game-SA projects require BOTH nested include folders.
    # Missing enums causes eWeaponType.h/eCrimeType.h/eEntityType.h errors.
    # Missing rw causes RenderWare.h/RwMatrix and related cascading errors.
    foreach(_sdk_required_dir
        "${PLUGIN_SDK_ROOT}/plugin_sa/game_sa/enums"
        "${PLUGIN_SDK_ROOT}/plugin_sa/game_sa/rw")
        if(NOT IS_DIRECTORY "${_sdk_required_dir}")
            message(FATAL_ERROR "Incomplete Plugin-SDK checkout: ${_sdk_required_dir}")
        endif()
    endforeach()
    find_package(minhook CONFIG REQUIRED)

    add_library(DistantStreaming SHARED
        src/game/main.cpp
        src/game/AsyncLog.cpp
        src/game/GameAdapter.cpp
        src/game/Hooks.cpp
        src/game/TargetValidation.cpp
        src/game/ConfigWin.cpp
        src/game/Telemetry.cpp
        src/game/Readiness.cpp)
    target_sources(DistantStreaming PRIVATE src/game/StreamingMemoryWin.cpp src/game/CameraReadinessWin.cpp src/game/OffscreenCleanupWin.cpp src/game/OlaBindingsWin.cpp src/game/GpuDirectWin.cpp src/game/GpuResidencyWin.cpp src/game/NativeBacking.cpp src/game/AsyncAssetCacheWin.cpp)
    target_include_directories(DistantStreaming PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/game"
        "${PLUGIN_SDK_ROOT}/plugin_sa"
        "${PLUGIN_SDK_ROOT}/plugin_sa/game_sa"
        "${PLUGIN_SDK_ROOT}/plugin_sa/game_sa/enums"
        "${PLUGIN_SDK_ROOT}/plugin_sa/game_sa/rw"
        "${PLUGIN_SDK_ROOT}/shared"
        "${PLUGIN_SDK_ROOT}/shared/game"
        "${PLUGIN_SDK_ROOT}/hooking"
        "${PLUGIN_SDK_ROOT}/injector")
    target_compile_definitions(DistantStreaming PRIVATE
        GTASA PLUGIN_SGV_10US RW WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_compile_features(DistantStreaming PRIVATE cxx_std_23)
    target_compile_options(DistantStreaming PRIVATE /W4 /EHsc /Zi)
    # Emit address-to-symbol maps for plugin crashes (e.g. ASI+0x1F794).
    target_link_options(DistantStreaming PRIVATE /DEBUG:FULL /MAP)
    target_link_libraries(DistantStreaming PRIVATE
        asc_core "${PLUGIN_SDK_SA_LIB}" minhook::minhook psapi d3d9 dxgi bcrypt
        ${PLUGIN_SDK_EXTRA_LIBS})
    set_target_properties(DistantStreaming PROPERTIES
        PREFIX "" OUTPUT_NAME "DistantStreaming" SUFFIX ".asi")
endif()

