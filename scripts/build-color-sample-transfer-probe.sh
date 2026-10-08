#!/usr/bin/env bash
# Compile an original probe using actual production RenderTarget and sample transfer code.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
platform="${1:-windows}"
out="${APS5_COLOR_SAMPLE_PROBE_BUILD:-$root/build/color-sample-transfer-probe/$platform}"
headers="${VULKAN_HEADERS:-$root/upstreams/AnyPS5/3rdparty/Vulkan-Headers/include}"
graphics="$root/upstreams/AnyPS5/core/libs/prx/libSceAgcDriver/Graphics"
sources=("$root/tools/gpu-probe/color_sample_transfer_probe.cpp" "$graphics/src/ColorRenderTarget.cpp" "$graphics/src/ColorSampleTransfer.cpp")
includes=(-I"$headers" -I"$root/upstreams/AnyPS5/core/libs" -I"$root/upstreams/AnyPS5/core/shader/recompiler")
mkdir -p "$out"
python3 "$root/scripts/check-color-sample-transfer.py" --shaders-only
case "$platform" in
    windows)
        python3 - "$out/vulkan-1.def" "${sources[@]}" <<'PY'
import pathlib, re, sys
names=sorted(set(re.findall(r'\b(vk[A-Z]\w*)\b','\n'.join(pathlib.Path(p).read_text() for p in sys.argv[2:]))))
pathlib.Path(sys.argv[1]).write_text('LIBRARY vulkan-1.dll\nEXPORTS\n'+''.join('  '+n+'\n' for n in names))
PY
        "${DLLTOOL_WINDOWS:-x86_64-w64-mingw32-dlltool}" -m i386:x86-64 -d "$out/vulkan-1.def" -l "$out/libvulkan-1.a"
        "${CXX_WINDOWS:-x86_64-w64-mingw32-g++}" -std=c++20 -O2 -Wall -Wextra -Wno-missing-field-initializers \
            -static -static-libgcc -static-libstdc++ "${includes[@]}" "${sources[@]}" "$out/libvulkan-1.a" -o "$out/probe.exe"
        "${OBJCOPY_WINDOWS:-x86_64-w64-mingw32-objcopy}" --rename-section .eh_frame=.ehfram "$out/probe.exe"
        ;;
    macos)
        : "${MOLTENVK_LIB:?Set MOLTENVK_LIB to a matching macOS libMoltenVK.dylib}"
        xcrun clang++ -std=c++20 -O2 -Wall -Wextra -Wno-missing-field-initializers "${includes[@]}" "${sources[@]}" \
            "$MOLTENVK_LIB" -Wl,-rpath,"$(dirname "$MOLTENVK_LIB")" -o "$out/probe"
        ;;
    *) echo 'usage: build-color-sample-transfer-probe.sh windows|macos' >&2; exit 2 ;;
esac
python3 - "$root" "$out" "${sources[@]}" <<'PY'
import hashlib,json,pathlib,subprocess,sys
root,out=map(pathlib.Path,sys.argv[1:3]); paths=list(map(pathlib.Path,sys.argv[3:]));graphics=root/'upstreams/AnyPS5/core/libs/prx/libSceAgcDriver/Graphics'
paths += [graphics/'include'/name for name in ('ColorSampleTransfer.hpp','Resources.hpp','Context.hpp','State.hpp')]
paths += list(graphics.glob('shaders/ColorSample*'))
hashfile=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
data={'schema':1,'commit':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),
 'files':{p.name:hashfile(p) for p in out.iterdir() if p.is_file() and p.name!='manifest.json'},
 'sources':{str(p.relative_to(root)):hashfile(p) for p in paths},
 'scope':'actual production RGBA8_UNORM import/export for every sample at 2/4/8 counts; original offscreen synthetic data; not guest rendering',
 'cases':6,'width':17,'height':19,'sample_words':9044}
(out/'manifest.json').write_text(json.dumps(data,indent=2)+'\n')
PY
printf 'Built production color sample transfer probe in %s\n' "$out"
