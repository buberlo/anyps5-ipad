#!/usr/bin/env bash
# Builds a synthetic offscreen experiment, not the AnyPS5 game renderer.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
platform="${1:-windows}"
out="${APS5_MSAA_PROBE_BUILD:-$root/build/msaa-split-probe/$platform}"
headers="${VULKAN_HEADERS:-$root/upstreams/AnyPS5/3rdparty/Vulkan-Headers/include}"
src="$root/tools/gpu-probe/msaa_split_probe.c"
production="${APS5_MSAA_PRODUCTION_TARGET:-0}"
case "$production" in 0|1) ;; *) echo 'APS5_MSAA_PRODUCTION_TARGET must be 0 or 1' >&2; exit 2 ;; esac
production_flags=()
production_sources=()
production_scan=("$src")
if [[ "$production" == 1 ]]; then
    production_flags=(-DAPS5_PRODUCTION_RENDER_TARGET)
    production_sources=("$root/tools/gpu-probe/render_target_fixture.cpp" "$root/upstreams/AnyPS5/core/libs/prx/libSceAgcDriver/Graphics/src/ColorRenderTarget.cpp")
    production_scan+=("${production_sources[@]}")
fi
includes=(-I"$headers" -I"$root/upstreams/AnyPS5/core/libs" -I"$root/upstreams/AnyPS5/core/shader/recompiler")
mkdir -p "$out"
for stage in vert frag; do
    glslangValidator -V --target-env vulkan1.1 "$root/tools/gpu-probe/msaa_split.$stage" -o "$out/msaa_split.$stage.spv"
    spirv-val --target-env vulkan1.1 "$out/msaa_split.$stage.spv"
done
glslangValidator -V --target-env vulkan1.1 "$root/tools/gpu-probe/msaa_split_read.comp" -o "$out/msaa_split_read.comp.spv"
spirv-val --target-env vulkan1.1 "$out/msaa_split_read.comp.spv"
case "$platform" in
    windows)
        python3 - "$out/vulkan-1.def" "${production_scan[@]}" <<'PY'
import pathlib, re, sys
names = sorted(set(re.findall(r'\b(vk[A-Z]\w*)\b', '\n'.join(pathlib.Path(p).read_text() for p in sys.argv[2:]))))
pathlib.Path(sys.argv[1]).write_text('LIBRARY vulkan-1.dll\nEXPORTS\n' + ''.join('  ' + n + '\n' for n in names))
PY
        "${DLLTOOL_WINDOWS:-x86_64-w64-mingw32-dlltool}" -m i386:x86-64 -d "$out/vulkan-1.def" -l "$out/libvulkan-1.a"
        if [[ "$production" == 1 ]]; then
            "${CC_WINDOWS:-x86_64-w64-mingw32-gcc}" -std=c11 -Wall -Wextra -Werror -O2 \
                "${includes[@]}" "${production_flags[@]}" -c "$src" -o "$out/probe.o"
            "${CXX_WINDOWS:-x86_64-w64-mingw32-g++}" -std=c++20 -O2 -Wall -Wextra \
                -static -static-libgcc -static-libstdc++ "${includes[@]}" \
                "$out/probe.o" "${production_sources[@]}" "$out/libvulkan-1.a" -o "$out/probe.exe"
        else
            "${CC_WINDOWS:-x86_64-w64-mingw32-gcc}" -std=c11 -Wall -Wextra -Werror -O2 \
                -I"$headers" "$src" "$out/libvulkan-1.a" -o "$out/probe.exe"
        fi
        # The pinned iPad CRT handles the renamed section; retain unwind bytes.
        "${OBJCOPY_WINDOWS:-x86_64-w64-mingw32-objcopy}" --rename-section .eh_frame=.ehfram "$out/probe.exe"
        ;;
    macos)
        : "${MOLTENVK_LIB:?Set MOLTENVK_LIB to the pinned built libMoltenVK.dylib}"
        if [[ "$production" == 1 ]]; then
            xcrun clang -std=c11 -Wall -Wextra -Werror -O2 "${includes[@]}" "${production_flags[@]}" -c "$src" -o "$out/probe.o"
            xcrun clang++ -std=c++20 -O2 -Wall -Wextra "${includes[@]}" "$out/probe.o" "${production_sources[@]}" \
                "$MOLTENVK_LIB" -Wl,-rpath,"$(dirname "$MOLTENVK_LIB")" -o "$out/probe"
        else
            xcrun clang -std=c11 -Wall -Wextra -Werror -O2 -I"$headers" "$src" \
                "$MOLTENVK_LIB" -Wl,-rpath,"$(dirname "$MOLTENVK_LIB")" -o "$out/probe"
        fi
        ;;
    *) echo 'usage: build-msaa-split-probe.sh windows|macos' >&2; exit 2 ;;
esac
python3 - "$root" "$out" "$production" <<'PY'
import hashlib, json, pathlib, subprocess, sys
root, out = map(pathlib.Path, sys.argv[1:3])
production = sys.argv[3] == "1"
hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file() and p.name != 'manifest.json'}
sources = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
    for p in (root / 'tools/gpu-probe').glob('msaa_split*')
    if p.suffix in ('.c', '.vert', '.frag', '.comp')}
if production:
    for relative in ('upstreams/AnyPS5/core/libs/prx/libSceAgcDriver/Graphics/src/ColorRenderTarget.cpp',
                     'upstreams/AnyPS5/core/libs/prx/libSceAgcDriver/Graphics/include/Resources.hpp',
                     'upstreams/AnyPS5/core/libs/prx/libSceAgcDriver/Graphics/include/Context.hpp',
                     'upstreams/AnyPS5/core/libs/prx/libSceAgcDriver/Graphics/include/State.hpp',
                     'tools/gpu-probe/render_target_fixture.cpp', 'tools/gpu-probe/render_target_fixture.h'):
        sources[relative] = hashlib.sha256((root / relative).read_bytes()).hexdigest()
commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
(out / 'manifest.json').write_text(json.dumps({'schema': 1, 'commit': commit, 'files': hashes, 'sources': sources,
    'production_render_target': production,
    'scope': 'synthetic split coverage; --array adds individual sample preservation and GPU resolve; production option tests actual RenderTarget ownership, not guest rendering'}, indent=2) + '\n')
PY
printf 'Built MSAA experiment in %s; run from that directory so shaders resolve.\n' "$out"
