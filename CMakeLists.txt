cmake_minimum_required(VERSION 3.24)
project(ern_relics VERSION 1.0.0 LANGUAGES C CXX) # x-release-please-version

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
    add_compile_options(/fsanitize=address /Zi)
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

# Project targets.
add_library(ern_relic_model STATIC
    src/relics/effect_catalog.cpp
    src/relics/relic_catalog.cpp
    src/relics/relic_validator.cpp
    src/relics/relic_drafts.cpp
    src/relics/preset.cpp)
target_include_directories(ern_relic_model PUBLIC src)

# Keep explicit build inputs while rejecting forgotten or stale source registrations.
# Collect sources before tests add integration/dll_fixture.cpp to ern_relics.
file(GLOB_RECURSE project_sources CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "src/*.cpp")
set(registered_sources)
foreach(target ern_relic_model)
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
