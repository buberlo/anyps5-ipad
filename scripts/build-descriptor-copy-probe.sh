#!/usr/bin/env bash
# Original descriptor-copy/layout factorial; no game or private shader data.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
platform="${1:-windows}"
out="${APS5_DESCRIPTOR_PROBE_BUILD:-$root/build/descriptor-copy-probe/$platform}"
headers="${VULKAN_HEADERS:-$root/upstreams/AnyPS5/3rdparty/Vulkan-Headers/include}"
source="$root/tools/gpu-probe/descriptor_copy_probe.cpp"
mkdir -p "$out"
for stage in vert frag; do
    glslangValidator -V --target-env vulkan1.1 "$root/tools/gpu-probe/descriptor_copy_probe.$stage" -o "$out/$stage.spv"
    spirv-val --target-env vulkan1.1 "$out/$stage.spv"
done
python3 - "$out" <<'PY'
import pathlib,struct,sys
out=pathlib.Path(sys.argv[1]); lines=['#include <cstdint>\n']
for stage,name in [('vert','VERTEX'),('frag','FRAGMENT')]:
    data=(out/(stage+'.spv')).read_bytes();words=struct.unpack('<'+'I'*(len(data)//4),data)
    lines.append('inline constexpr std::uint32_t DESCRIPTOR_COPY_'+name+'_SPV[] = {\n')
    lines.extend(' '+', '.join(hex(w) for w in words[i:i+8])+',\n' for i in range(0,len(words),8));lines.append('};\n')
(out/'descriptor_copy_shaders.h').write_text(''.join(lines))
PY
case "$platform" in
    windows)
        python3 - "$source" "$out/vulkan-1.def" <<'PY'
import pathlib,re,sys
source,out=map(pathlib.Path,sys.argv[1:]);names=sorted(set(re.findall(r'\b(vk[A-Z]\w*)\b',source.read_text())))
out.write_text('LIBRARY vulkan-1.dll\nEXPORTS\n'+''.join(' '+name+'\n' for name in names))
PY
        "${DLLTOOL_WINDOWS:-x86_64-w64-mingw32-dlltool}" -m i386:x86-64 -d "$out/vulkan-1.def" -l "$out/libvulkan-1.a"
        "${CXX_WINDOWS:-x86_64-w64-mingw32-g++}" -std=c++20 -O2 -Wall -Wextra -Wno-missing-field-initializers -static -static-libgcc -static-libstdc++ -I"$headers" -I"$out" "$source" "$out/libvulkan-1.a" -o "$out/probe.exe"
        "${OBJCOPY_WINDOWS:-x86_64-w64-mingw32-objcopy}" --rename-section .eh_frame=.ehfram "$out/probe.exe"
        ;;
    macos)
        : "${MOLTENVK_LIB:?Set MOLTENVK_LIB to a matching macOS libMoltenVK.dylib}"
        xcrun clang++ -std=c++20 -O2 -Wall -Wextra -Wno-missing-field-initializers -I"$headers" -I"$out" "$source" "$MOLTENVK_LIB" -Wl,-rpath,"$(dirname "$MOLTENVK_LIB")" -o "$out/probe"
        ;;
    *) echo 'usage: build-descriptor-copy-probe.sh windows|macos' >&2;exit 2 ;;
esac
python3 - "$root" "$out" <<'PY'
import hashlib,json,pathlib,subprocess,sys
root,out=map(pathlib.Path,sys.argv[1:]);digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
paths=[root/'tools/gpu-probe'/('descriptor_copy_probe.'+suffix) for suffix in ('cpp','vert','frag')]+[root/'scripts/build-descriptor-copy-probe.sh']
manifest={'schema':1,'commit':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),
 'sources':{str(p.relative_to(root)):digest(p) for p in paths},'files':{p.name:digest(p) for p in out.iterdir() if p.is_file() and p.name!='manifest.json'},
 'scope':'original native Vulkan ordinary texture2D plus separate sampler and readonly SSBO; original/copy/copy override/explicit rewrite versus GENERAL/attachment optimal; not AnyPS5 Draw or Recorder',
 'cases':8,'width':17,'height':19,'checked_channels':10336,'guard_bytes_per_case':256,'maximum_error_unorm_units':0}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
PY
printf 'Built original descriptor/layout probe in %s\n' "$out"
