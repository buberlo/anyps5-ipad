#!/usr/bin/env bash
# Build just the portable host relinker and NID patcher on Apple Silicon.
# No x86 runtime libraries, SDL, FFmpeg or guest binary execution required.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${APS5_HOST_TOOLS_BUILD:-$root/build/host-tools}"
tests="${APS5_HOST_TOOLS_TESTS:-OFF}"
case "$tests" in ON|OFF) ;; *) echo "APS5_HOST_TOOLS_TESTS must be ON or OFF" >&2; exit 2 ;; esac
mkdir -p "$out/source"
cat > "$out/source/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.20)
project(AnyPS5HostTools LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(BUILD_TESTING "Build portable relinker regression tests" OFF)
if(BUILD_TESTING)
    enable_testing()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()
function(add_test_executable target)
    add_executable(${target} ${ARGN})
endfunction()
function(configure_windows_unwind target)
    # Match the full project's fixture helper. No Windows unwind section exists
    # in the native Mach-O/ELF host-tools build.
    if(MINGW)
        if(NOT EXISTS "${CMAKE_OBJCOPY}")
            message(FATAL_ERROR "Windows DWARF unwinding requires objcopy")
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_OBJCOPY}" --rename-section .eh_frame=.ehfram "$<TARGET_FILE:${target}>"
            VERBATIM)
    endif()
endfunction()
# The full project publishes this catalog from core/libs before configuring
# the relinker. Derive the same directory catalog without building guest HLE
# or its SDL/FFmpeg dependencies in this native host-tools project.
set(ANYPS5_REPLACEMENT_MODULES libc.prx)
file(GLOB module_dirs LIST_DIRECTORIES true "${ANYPS5_SOURCE}/core/libs/prx/*")
foreach(module_dir IN LISTS module_dirs)
    if(IS_DIRECTORY "${module_dir}")
        get_filename_component(module_name "${module_dir}" NAME)
        if(NOT module_name STREQUAL "libc")
            list(APPEND ANYPS5_REPLACEMENT_MODULES "${module_name}.prx")
        endif()
    endif()
endforeach()
add_subdirectory("${ANYPS5_SOURCE}/core/relinker" relinker)
file(GLOB nid_sources "${ANYPS5_SOURCE}/core/libs/nid/src/*.cpp")
add_executable(nid_patcher ${nid_sources})
target_include_directories(nid_patcher PRIVATE "${ANYPS5_SOURCE}/core/libs/nid/include")
CMAKE
cmake -S "$out/source" -B "$out/build" -G Ninja \
    -DANYPS5_SOURCE="${APS5_ANYPS5_SOURCE:-$root/upstreams/AnyPS5}" -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING="$tests"
if [ "$tests" = ON ]; then
    cmake --build "$out/build" --parallel "${JOBS:-4}"
    ctest --test-dir "$out/build" --output-on-failure --timeout 30
else
    cmake --build "$out/build" --target relinker nid_patcher --parallel "${JOBS:-4}"
fi
echo "Host tools: $out/build/relinker/relinker and $out/build/nid_patcher"
