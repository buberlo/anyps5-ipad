#!/usr/bin/env bash
# Compile an original probe using production resolve factory, rectangle stages and sample transfers.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
platform="${1:-windows}"
out="${APS5_FIXED_RESOLVE_PROBE_BUILD:-$root/build/fixed-color-resolve-probe/$platform}"
headers="${VULKAN_HEADERS:-$root/upstreams/AnyPS5/3rdparty/Vulkan-Headers/include}"
graphics="$root/upstreams/AnyPS5/core/libs/prx/libSceAgcDriver/Graphics"
sources=("$root/tools/gpu-probe/fixed_color_resolve_probe.cpp" "$graphics/src/ColorRenderTarget.cpp" "$graphics/src/ColorSampleTransfer.cpp" "$graphics/src/ColorResolveFragment.cpp" "$root/upstreams/AnyPS5/core/shader/recompiler/SpirvBackend/src/RectListShaders.cpp" "$root/upstreams/AnyPS5/core/shader/recompiler/SpirvBackend/src/SpirvModule.cpp")
includes=(-I"$headers" -I"$root/upstreams/AnyPS5/core/libs" -I"$root/upstreams/AnyPS5/core/shader/recompiler" -I"$root/upstreams/AnyPS5/core/libs/prx/libc/include" -I"$root/upstreams/AnyPS5/3rdparty/SPIRV-Headers/include" -I"$out")
for directory in "$root"/upstreams/AnyPS5/core/shader/recompiler/*/include; do includes+=(-I"$directory"); done
mkdir -p "$out"
python3 "$root/scripts/check-color-sample-transfer.py" --shaders-only
python3 "$root/scripts/check-fixed-color-resolve.py" --shaders-only
glslangValidator -V --target-env vulkan1.1 "$root/tools/gpu-probe/fixed_color_resolve.vert" -o "$out/vertex.spv"
spirv-val --target-env vulkan1.1 "$out/vertex.spv"
python3 - "$out" <<'PYHEADER'
import pathlib,struct,sys
out=pathlib.Path(sys.argv[1]);data=(out/'vertex.spv').read_bytes();words=struct.unpack('<'+'I'*(len(data)//4),data)
(out/'fixed_resolve_vertex_spv.h').write_text('#include <cstdint>\ninline constexpr std::uint32_t FIXED_RESOLVE_VERTEX_SPV[] = {\n'+''.join('  '+', '.join(hex(w) for w in words[i:i+8])+',\n' for i in range(0,len(words),8))+'};\n')
PYHEADER
case "$platform" in
    windows)
        python3 - "$out/vulkan-1.def" "${sources[@]}" <<'PY'
import pathlib, re, sys
names=sorted(set(re.findall(r'\b(vk[A-Z]\w*)\b','\n'.join(pathlib.Path(p).read_text() for p in sys.argv[2:]))))
pathlib.Path(sys.argv[1]).write_text('LIBRARY vulkan-1.dll\nEXPORTS\n'+''.join('  '+n+'\n' for n in names))
PY
        "${DLLTOOL_WINDOWS:-x86_64-w64-mingw32-dlltool}" -m i386:x86-64 -d "$out/vulkan-1.def" -l "$out/libvulkan-1.a"
        "${CXX_WINDOWS:-x86_64-w64-mingw32-g++}" -std=c++20 -O2 -Wall -Wextra -Wno-missing-field-initializers \
            -DANYPS5_ENABLE_SPIRV_TOOLS=0 -ffunction-sections -fdata-sections -Wl,--gc-sections -static -static-libgcc -static-libstdc++ "${includes[@]}" "${sources[@]}" "$out/libvulkan-1.a" -o "$out/probe.exe"
        "${OBJCOPY_WINDOWS:-x86_64-w64-mingw32-objcopy}" --rename-section .eh_frame=.ehfram "$out/probe.exe"
        ;;
    macos)
        : "${MOLTENVK_LIB:?Set MOLTENVK_LIB to a matching macOS libMoltenVK.dylib}"
        xcrun clang++ -std=c++20 -O2 -DANYPS5_ENABLE_SPIRV_TOOLS=0 -ffunction-sections -fdata-sections -Wl,-dead_strip -Wall -Wextra -Wno-missing-field-initializers "${includes[@]}" "${sources[@]}" \
            "$MOLTENVK_LIB" -Wl,-rpath,"$(dirname "$MOLTENVK_LIB")" -o "$out/probe"
        ;;
    *) echo 'usage: build-fixed-color-resolve-probe.sh windows|macos' >&2; exit 2 ;;
esac
python3 - "$root" "$out" "${sources[@]}" <<'PY'
import hashlib,json,pathlib,subprocess,sys
root,out=map(pathlib.Path,sys.argv[1:3]); paths=list(map(pathlib.Path,sys.argv[3:]));graphics=root/'upstreams/AnyPS5/core/libs/prx/libSceAgcDriver/Graphics'
paths += [graphics/'include'/name for name in ('ColorResolve.hpp','ColorSampleTransfer.hpp','Resources.hpp','Context.hpp','State.hpp')]
paths += list(graphics.glob('shaders/ColorSample*')) + list(graphics.glob('shaders/ColorResolve*'))
paths += [root/'tools/gpu-probe/fixed_color_resolve.vert', root/'scripts/build-fixed-color-resolve-probe.sh', root/'upstreams/AnyPS5/core/shader/recompiler/Recompiler.hpp']
paths += list((root/'upstreams/AnyPS5/core/shader/recompiler/SpirvBackend/include').glob('*.hpp'))
hashfile=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
data={'schema':1,'commit':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),
 'files':{p.name:hashfile(p) for p in out.iterdir() if p.is_file() and p.name!='manifest.json'},
 'sources':{str(p.relative_to(root)):hashfile(p) for p in paths},
 'scope':'production RGBA8_UNORM resolve fragment+rect-list stages+sample uploads in an isolated pipeline; full/partial geometry preserves destination, native resolve comparisons at 2/4 and scalar average at 8; not full guest rendering',
 'cases':6,'width':17,'height':19,'checked_channels':7752,'native_resolve_cases':4,'maximum_error_unorm_units':1,'non_tie_error_unorm_units':0}
(out/'manifest.json').write_text(json.dumps(data,indent=2)+'\n')
PY
printf 'Built production fixed color resolve probe in %s\n' "$out"
