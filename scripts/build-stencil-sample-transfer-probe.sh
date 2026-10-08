#!/usr/bin/env bash
# Actual StencilSampleTransfer + original offscreen DS owner and independent depth readback.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
platform="${1:-windows}"
out="${APS5_STENCIL_SAMPLE_PROBE_BUILD:-$root/build/stencil-sample-transfer-probe/$platform}"
headers="${VULKAN_HEADERS:-$root/upstreams/AnyPS5/3rdparty/Vulkan-Headers/include}"
graphics="$root/upstreams/AnyPS5/core/libs/prx/libSceAgcDriver/Graphics"
sources=("$root/tools/gpu-probe/stencil_sample_transfer_probe.cpp" "$graphics/src/StencilSampleTransfer.cpp")
includes=(-I"$headers" -I"$root/upstreams/AnyPS5/core/libs" -I"$root/upstreams/AnyPS5/core/shader/recompiler" -I"$out")
mkdir -p "$out"
python3 "$root/scripts/check-stencil-sample-transfer.py" --shaders-only
glslangValidator -V --target-env vulkan1.1 "$root/tools/gpu-probe/stencil_sample_depth_readback.comp" -o "$out/depth-probe.spv"
spirv-val --target-env vulkan1.1 "$out/depth-probe.spv"
python3 - "$out" <<'PY'
import pathlib,struct,sys
out=pathlib.Path(sys.argv[1]);data=(out/'depth-probe.spv').read_bytes();words=struct.unpack('<'+'I'*(len(data)//4),data)
text=['#include <cstdint>','inline constexpr std::uint32_t STENCIL_DEPTH_PROBE_SPV[] = {']
text += ['    '+','.join(f'0x{x:08x}' for x in words[i:i+8])+',' for i in range(0,len(words),8)]
text += ['};',''];(out/'StencilSampleDepthProbe_spv.h').write_text('\n'.join(text))
PY
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
    *) echo 'usage: build-stencil-sample-transfer-probe.sh windows|macos' >&2; exit 2 ;;
esac
python3 - "$root" "$out" "${sources[@]}" <<'PY'
import hashlib,json,pathlib,subprocess,sys
root,out=map(pathlib.Path,sys.argv[1:3]);paths=list(map(pathlib.Path,sys.argv[3:]));graphics=root/'upstreams/AnyPS5/core/libs/prx/libSceAgcDriver/Graphics'
paths += [graphics/'include'/name for name in ('StencilSampleTransfer.hpp','ColorSampleTransfer.hpp','Context.hpp','State.hpp')]
paths += list(graphics.glob('shaders/StencilSample*'))+[root/'tools/gpu-probe/stencil_sample_depth_readback.comp']
hashfile=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
data={'schema':1,'commit':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),
 'files':{p.name:hashfile(p) for p in out.iterdir() if p.is_file() and p.name!='manifest.json'},
 'sources':{str(p.relative_to(root)):hashfile(p) for p in paths},
 'scope':'actual production S8 import/export for every sample, preserved combined D32S8 depth; original offscreen synthetic data; not guest rendering',
 'cases':6,'width':17,'height':19,'sample_bytes':9044,'depth_samples':9044}
(out/'manifest.json').write_text(json.dumps(data,indent=2)+'\n')
PY
printf 'Built production stencil sample transfer probe in %s\n' "$out"
