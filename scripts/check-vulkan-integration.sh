#!/usr/bin/env bash
# Host integration tests. Does not assert GPU or iPad runtime success.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$root/build/graphics-tests"
mkdir -p "$out"
export_flags=(-rdynamic)
if [ "$(uname -s)" = Darwin ]; then export_flags=(-Wl,-export_dynamic); fi
for mode in dynamic static; do
    flags=(-UMADEIRA_VK_STATIC_LINK)
    if [ "$mode" = static ]; then flags=(-DMADEIRA_VK_STATIC_LINK=1); fi
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror "${export_flags[@]}" "${flags[@]}" \
        "$root/tools/checks/test_moltenvk_loader.c" \
        "$root/upstreams/Madeira/build/win32u-unix/moltenvk_static_loader.c" \
        -ldl -o "$out/loader-$mode"
    "$out/loader-$mode"
done
python3 "$root/tools/checks/check_vulkan_integration.py"
