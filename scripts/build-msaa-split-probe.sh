#!/usr/bin/env bash
# Builds a synthetic offscreen experiment, not the AnyPS5 game renderer.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
platform="${1:-windows}"
out="${APS5_MSAA_PROBE_BUILD:-$root/build/msaa-split-probe/$platform}"
headers="${VULKAN_HEADERS:-$root/upstreams/AnyPS5/3rdparty/Vulkan-Headers/include}"
src="$root/tools/gpu-probe/msaa_split_probe.c"
mkdir -p "$out"
for stage in vert frag; do
    glslangValidator -V --target-env vulkan1.1 "$root/tools/gpu-probe/msaa_split.$stage" -o "$out/msaa_split.$stage.spv"
    spirv-val --target-env vulkan1.1 "$out/msaa_split.$stage.spv"
done
case "$platform" in
    windows)
        python3 - "$src" "$out/vulkan-1.def" <<'PY'
import pathlib, re, sys
names = sorted(set(re.findall(r'\b(vk[A-Z]\w+)\s*\(', pathlib.Path(sys.argv[1]).read_text())))
pathlib.Path(sys.argv[2]).write_text('LIBRARY vulkan-1.dll\nEXPORTS\n' + ''.join('  ' + n + '\n' for n in names))
PY
        "${DLLTOOL_WINDOWS:-x86_64-w64-mingw32-dlltool}" -m i386:x86-64 -d "$out/vulkan-1.def" -l "$out/libvulkan-1.a"
        "${CC_WINDOWS:-x86_64-w64-mingw32-gcc}" -std=c11 -Wall -Wextra -Werror -O2 \
            -I"$headers" "$src" "$out/libvulkan-1.a" -o "$out/probe.exe"
        # The pinned iPad CRT handles the renamed section; retain unwind bytes.
        "${OBJCOPY_WINDOWS:-x86_64-w64-mingw32-objcopy}" --rename-section .eh_frame=.ehfram "$out/probe.exe"
        ;;
    macos)
        : "${MOLTENVK_LIB:?Set MOLTENVK_LIB to the pinned built libMoltenVK.dylib}"
        xcrun clang -std=c11 -Wall -Wextra -Werror -O2 -I"$headers" "$src" \
            "$MOLTENVK_LIB" -Wl,-rpath,"$(dirname "$MOLTENVK_LIB")" -o "$out/probe"
        ;;
    *) echo 'usage: build-msaa-split-probe.sh windows|macos' >&2; exit 2 ;;
esac
python3 - "$root" "$out" <<'PY'
import hashlib, json, pathlib, subprocess, sys
root, out = map(pathlib.Path, sys.argv[1:])
hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file() and p.name != 'manifest.json'}
commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
(out / 'manifest.json').write_text(json.dumps({'schema': 1, 'commit': commit, 'files': hashes,
    'scope': 'synthetic split coverage and center interpolation only'}, indent=2) + '\n')
PY
printf 'Built MSAA experiment in %s; run from that directory so shaders resolve.\n' "$out"
