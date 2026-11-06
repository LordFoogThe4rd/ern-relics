cmake_minimum_required(VERSION 3.25)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
project(ern_relics VERSION 1.0.1 LANGUAGES C CXX) # x-release-please-version

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "ERN Relics requires 64-bit Windows.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT MSVC)
    message(FATAL_ERROR "Use the MSVC toolchain from Visual Studio C++ Build Tools.")
endif()
if(MSVC_VERSION LESS 1944)
    message(FATAL_ERROR "Use MSVC 19.44 or newer from Visual Studio 2022 17.14 or Visual Studio 2026.")
endif()
option(ERN_ENABLE_ASAN "Instrument local tests and the DX12 host with AddressSanitizer" OFF)
if(ERN_ENABLE_ASAN)
    add_compile_options(/fsanitize=address)
    add_link_options(/INCREMENTAL:NO)
    get_filename_component(compiler_directory "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_file(asan_runtime NAMES clang_rt.asan_dynamic-x86_64.dll
        PATHS "${compiler_directory}" NO_DEFAULT_PATH REQUIRED)
    configure_file("${asan_runtime}" "${CMAKE_BINARY_DIR}/clang_rt.asan_dynamic-x86_64.dll" COPYONLY)
endif()

# Common project target settings.
function(ern_configure_target target)
    target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /utf-8)
    set_target_properties(${target} PROPERTIES COMPILE_WARNING_AS_ERROR ON)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endfunction()

# Generated game keys and third-party dependencies.
file(READ "${CMAKE_SOURCE_DIR}/tools/assets/nightreign-keys.json" GAME_KEYS)
string(JSON REGULATION_HEX GET "${GAME_KEYS}" regulation)
string(REGEX REPLACE "(..)" "0x\\1," REGULATION_BYTES "${REGULATION_HEX}")
foreach(archive DATA0 DATA1 DATA2 DATA3 DLC01)
    string(TOLOWER "${archive}" archive_name)
    string(JSON ${archive}_KEY GET "${GAME_KEYS}" archives "${archive_name}")
endforeach()
configure_file(src/data/game_keys.hpp.in generated/game_keys.hpp @ONLY)
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/third_party/imgui")
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/bcdec/bcdec.h")
    message(FATAL_ERROR "Run tools/bootstrap.ps1 to download the pinned BC texture decoder.")
endif()
if(NOT EXISTS "${IMGUI_DIR}/imgui.cpp")
    message(FATAL_ERROR "Run tools/bootstrap.ps1 before configuring.")
endif()

add_subdirectory(third_party/minhook EXCLUDE_FROM_ALL)
set(ZSTD_DIR "${CMAKE_SOURCE_DIR}/third_party/zstd/lib")
if(NOT EXISTS "${ZSTD_DIR}/zstd.h")
    message(FATAL_ERROR "Run tools/bootstrap.ps1 to download the pinned Zstandard decoder.")
endif()
add_library(zstd_decoder STATIC
    ${ZSTD_DIR}/common/debug.c
    ${ZSTD_DIR}/common/entropy_common.c
    ${ZSTD_DIR}/common/error_private.c
    ${ZSTD_DIR}/common/fse_decompress.c
    ${ZSTD_DIR}/common/xxhash.c
    ${ZSTD_DIR}/common/zstd_common.c
    ${ZSTD_DIR}/decompress/huf_decompress.c
    ${ZSTD_DIR}/decompress/zstd_ddict.c
    ${ZSTD_DIR}/decompress/zstd_decompress.c
    ${ZSTD_DIR}/decompress/zstd_decompress_block.c)
target_include_directories(zstd_decoder SYSTEM PUBLIC ${ZSTD_DIR})
target_compile_definitions(zstd_decoder PRIVATE ZSTD_DISABLE_ASM)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx12.cpp)
target_include_directories(imgui SYSTEM PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(imgui PUBLIC d3d12 dxgi d3dcompiler dwmapi)

# Project targets.
add_library(ern_relic_model STATIC
    src/relics/effect_catalog.cpp
    src/relics/relic_catalog.cpp
    src/relics/relic_validator.cpp
    src/relics/relic_drafts.cpp
    src/relics/preset.cpp)
target_include_directories(ern_relic_model PUBLIC src)

add_library(ern_game_memory STATIC
    src/game/pattern.cpp
    src/game/relic_reader.cpp
    src/game/edit_location.cpp
    src/game/relic_equipment.cpp
    src/game/relic_writer.cpp)
target_link_libraries(ern_game_memory PUBLIC ern_relic_model)

add_library(ern_game_data STATIC
    src/data/fingerprint.cpp
    src/data/regulation.cpp
    src/data/game_files.cpp
    src/data/game_assets.cpp
    src/data/catalog_presentation.cpp
    src/data/preset_store.cpp
    src/data/edit_journal.cpp
    src/data/rotating_file.cpp
    src/data/system_fonts.cpp)
target_link_libraries(ern_game_data PUBLIC ern_relic_model PRIVATE bcrypt crypt32 xmllite shlwapi zstd_decoder)
target_include_directories(ern_game_data PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(ern_game_data SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/third_party/bcdec")

add_library(ern_application STATIC
    src/app/active_regulation.cpp
    src/app/editor_session.cpp
    src/app/relic_edit_queue.cpp
    src/app/native_edit_diagnostics.cpp
    src/app/native_callback_hook.cpp
    src/app/process_guard.cpp
    src/app/runtime.cpp
    src/app/editor_application.cpp
    src/app/event_log.cpp)
target_link_libraries(ern_application PUBLIC ern_game_memory ern_game_data)
target_compile_definitions(ern_application PRIVATE ERN_VERSION="${PROJECT_VERSION}")

add_library(ern_ui STATIC
    src/ui/ui.cpp
    src/ui/editor.cpp
    src/ui/effect_choices.cpp
    src/ui/presets.cpp
    src/ui/diagnostics.cpp
    src/ui/widgets.cpp)
target_link_libraries(ern_ui PUBLIC ern_game_data imgui)

# Compile DLL code once, then link production and integration-test variants.
add_library(ern_runtime OBJECT
    src/app/dllmain.cpp
    src/app/bootstrap.cpp
    src/app/native_relic_equip.cpp
    src/app/overlay.cpp
    src/app/dx12_renderer.cpp
    src/app/input.cpp)
target_link_libraries(ern_runtime PUBLIC ern_application ern_ui minhook version dinput8 dxguid)
target_compile_definitions(ern_runtime PRIVATE ERN_VERSION="${PROJECT_VERSION}")

add_library(ern_relics SHARED)
target_link_libraries(ern_relics PRIVATE ern_runtime)
set_target_properties(ern_relics PROPERTIES OUTPUT_NAME "ern-relics"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/runtime"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/runtime"
    PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/runtime")
ern_configure_target(ern_relics)

# Keep explicit build inputs while rejecting forgotten or stale source registrations.
file(GLOB_RECURSE project_sources CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "src/*.cpp")
set(registered_sources)
foreach(target ern_relic_model ern_game_memory ern_game_data ern_application ern_ui ern_runtime)
    ern_configure_target(${target})
    get_target_property(target_sources ${target} SOURCES)
    list(APPEND registered_sources ${target_sources})
endforeach()
list(SORT project_sources)
list(SORT registered_sources)
if(NOT "${project_sources}" STREQUAL "${registered_sources}")
    message(FATAL_ERROR "src/*.cpp and project target source lists differ. Update CMakeLists.txt.")
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
