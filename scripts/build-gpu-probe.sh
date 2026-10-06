#!/usr/bin/env bash
# Build the same offscreen execution probe for macOS or Windows.
# MOLTENVK_LIB must name the pinned built dylib for a macOS run.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
platform="${1:-macos}"
out="${APS5_GPU_PROBE_BUILD:-$root/build/gpu-probe/$platform}"
headers="${VULKAN_HEADERS:-$root/upstreams/AnyPS5/3rdparty/Vulkan-Headers/include}"
if [ ! -f "$headers/vulkan/vulkan.h" ]; then
    headers="$root/upstreams/MoltenVK/External/Vulkan-Headers/include"
fi
mkdir -p "$out"
for shader in bda_bytes bc_sample; do
    glslangValidator -V --target-env vulkan1.1 "$root/tools/gpu-probe/$shader.comp" -o "$out/$shader.spv"
    spirv-val --target-env vulkan1.1 "$out/$shader.spv"
done
case "$platform" in
    macos)
        : "${MOLTENVK_LIB:?Set MOLTENVK_LIB to the pinned built libMoltenVK.dylib}"
        export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
        xcrun clang -std=c11 -Wall -Wextra -Werror -O2 -DAPS5_GPU_PROBE_CLI \
            -I"$headers" "$root/tools/gpu-probe/gpu_probe.c" "$MOLTENVK_LIB" \
            -Wl,-rpath,"$(dirname "$MOLTENVK_LIB")" -o "$out/gpu-probe"
        ;;
    windows)
        # MinGW's import library provides the entry points from vulkan-1.dll.
        : "${VULKAN_IMPORT_LIB:?Set VULKAN_IMPORT_LIB to a Windows x64 Vulkan import library}"
        "${CC_WINDOWS:-x86_64-w64-mingw32-gcc}" -std=c11 -Wall -Wextra -Werror -O2 \
            -DAPS5_GPU_PROBE_CLI -I"$headers" "$root/tools/gpu-probe/gpu_probe.c" \
            "$VULKAN_IMPORT_LIB" -o "$out/gpu-probe.exe"
        ;;
    *) echo "usage: $0 macos|windows" >&2; exit 2 ;;
esac
python3 - "$root" "$out" "$platform" <<'PY'
import hashlib, json, pathlib, subprocess, sys
root, out, platform = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), sys.argv[3]
files = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file() and p.name != "manifest.json"}
commit = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
(out / "manifest.json").write_text(json.dumps({"schema": 1, "commit": commit, "platform": platform, "files": files}, indent=2) + "\n")
PY
printf 'Built execution probe: %s\n' "$out"
