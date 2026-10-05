#!/usr/bin/env bash
# What this x86-64 Linux VM can actually check.
# Applies the patch series, builds the capability tool, runs it on lavapipe
# when that ICD is installed, compile-checks the MoltenVK loader and the
# Windows GuestArena translation unit, then prints a status block.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$root/scripts/apply-patches.sh"

echo "== vk-requirements =="
make -C "$root/tools/vk-requirements" clean all
icd=""
for candidate in \
    /usr/share/vulkan/icd.d/lvp_icd.json \
    /usr/share/vulkan/icd.d/lvp_icd.x86_64.json
do
    if [ -f "$candidate" ]; then icd="$candidate"; break; fi
done
if [ -n "$icd" ]; then
    set +e
    VK_DRIVER_FILES="$icd" VK_LOADER_LAYERS_DISABLE='~all~' \
        "$root/tools/vk-requirements/vk-requirements" --present
    vk_status=$?
    set -e
    echo "vk-requirements exit=$vk_status (0 = hard requirements met, 1 = reported a miss, 2/3 = loader failure)"
    if [ "$vk_status" -ge 2 ]; then
        echo "capability tool failed to run" >&2
        exit 1
    fi
else
    echo "no lavapipe ICD found; tool was only compiled" >&2
    exit 1
fi

echo "== moltenvk static loader =="
cc -std=c11 -Wall -Wextra -Werror -rdynamic -o "$root/tools/checks/test_moltenvk_loader" \
    "$root/tools/checks/test_moltenvk_loader.c" \
    "$root/upstreams/Madeira/build/win32u-unix/moltenvk_static_loader.c" \
    -ldl
"$root/tools/checks/test_moltenvk_loader"

mkdir -p "$root/build"
echo "== GuestArena.cpp (Linux TU, arena stays unavailable) =="
g++ -std=c++20 -Wall -Wextra -Wno-unused-function -c \
    -I "$root/upstreams/AnyPS5/core/libs" \
    "$root/upstreams/AnyPS5/core/libs/prx/libc/src/GuestArena.cpp" \
    -o "$root/build/GuestArena-linux.o"

echo "== GuestArena.cpp (MinGW Windows TU, includes the lazy path) =="
# Ubuntu 22.04's mingw-w64 headers omit MEM_RESERVE_PLACEHOLDER and
# its libstdc++ does not provide std::mutex after windows.h. The
# ubuntu-24.04 job still compiles this TU. Skipping here keeps the
# capability result from failing the older image.
mingw_probe="$root/build/guest-arena-mingw-probe.cpp"
cat > "$mingw_probe" << 'EOF'
#include <windows.h>
#include <mutex>
#ifndef MEM_RESERVE_PLACEHOLDER
#error missing MEM_RESERVE_PLACEHOLDER
#endif
std::mutex guest_arena_mingw_probe;
EOF
if x86_64-w64-mingw32-g++ -std=c++20 -Wall -Wextra -c "$mingw_probe" -o "$root/build/guest-arena-mingw-probe.o"; then
    x86_64-w64-mingw32-g++ -std=c++20 -Wall -Wextra -c \
        -I "$root/upstreams/AnyPS5/core/libs" \
        "$root/upstreams/AnyPS5/core/libs/prx/libc/src/GuestArena.cpp" \
        -o "$root/build/GuestArena-mingw.o"
else
    echo "skipped MinGW GuestArena.cpp: this mingw-w64 cannot compile the placeholder path"
fi

echo "linux checks finished"
