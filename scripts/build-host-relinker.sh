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
