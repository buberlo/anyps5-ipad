#!/usr/bin/env bash
# M0: configure and build AnyPS5 on x86-64 Linux.
# Does not fetch game dumps, firmware, or keys. The nested submodules are
# source dependencies (SDL, Vulkan-Headers, glslang, FreeType, ...).
# FFmpeg prebuilt binaries are downloaded by AnyPS5's own CMake unless
# FFMPEG_PREBUILT_DIR is set. That download is upstream's, not a game dump.
#
# SDL2's X11 check needs development headers. On Debian/Ubuntu that is
# libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev
# libxfixes-dev libxss-dev libxrender-dev libxxf86vm-dev libdrm-dev
# libudev-dev libdbus-1-dev libgl1-mesa-dev libegl1-mesa-dev. This script
# does not install packages. libSceAgcDriver is EXCLUDE_FROM_ALL; after
# this script, build it with:
#   cmake --build "$root/build/anyps5" --target libSceAgcDriver
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$root/scripts/apply-patches.sh"

if [ "$(uname -m)" != "x86_64" ]; then
    echo "AnyPS5's CMake target is x86-64 (this host is $(uname -m))" >&2
    exit 1
fi

git -C "$root/upstreams/AnyPS5" submodule update --init --depth 1
# APS5_SLIM=1 loads vulkan-1.dll / libvulkan.so.1 with LoadLibrary/dlopen
# and does not link SDL2 into libSceAgcDriver. libSceVideoOut and libfmod
# still use SDL. Default (unset) keeps the upstream SDL loader.
slim_flag=OFF
if [ "${APS5_SLIM:-}" = 1 ]; then
    slim_flag=ON
fi
cmake -S "$root/upstreams/AnyPS5" -B "$root/build/anyps5" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ \
    -DBUILD_TESTING="${BUILD_TESTING:-OFF}" \
    -DAPS5_SLIM="$slim_flag"
cmake --build "$root/build/anyps5" --parallel "${JOBS:-$(nproc)}"
if [ "$slim_flag" = ON ]; then
    cmake --build "$root/build/anyps5" --target libSceAgcDriver --parallel "${JOBS:-$(nproc)}"
fi
echo "M0 build tree: $root/build/anyps5 (APS5_SLIM=$slim_flag)"
