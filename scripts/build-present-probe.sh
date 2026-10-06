#!/usr/bin/env bash
# A real PE x64 swapchain probe; imports the standard Windows Vulkan loader.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${APS5_PRESENT_PROBE_BUILD:-$root/build/gpu-probe/windows-present}"
headers="${VULKAN_HEADERS:-$root/upstreams/AnyPS5/3rdparty/Vulkan-Headers/include}"
if [ ! -f "$headers/vulkan/vulkan.h" ]; then
    headers="$root/upstreams/MoltenVK/External/Vulkan-Headers/include"
fi
cc="${CC_WINDOWS:-x86_64-w64-mingw32-gcc}"
mkdir -p "$out"
import_lib="${VULKAN_IMPORT_LIB:-$out/libvulkan-1.a}"
if [ -z "${VULKAN_IMPORT_LIB:-}" ]; then
    # Generate only the public entry points this executable actually calls.
    python3 - "$root/tools/gpu-probe/present_probe.c" "$out/vulkan-1.def" <<'PY'
import pathlib, re, sys
names = sorted(set(re.findall(r'\b(vk[A-Z]\w+)\s*\(', pathlib.Path(sys.argv[1]).read_text())))
pathlib.Path(sys.argv[2]).write_text('LIBRARY vulkan-1.dll\nEXPORTS\n' + ''.join('  ' + n + '\n' for n in names))
PY
    "${DLLTOOL_WINDOWS:-x86_64-w64-mingw32-dlltool}" -m i386:x86-64 -d "$out/vulkan-1.def" -l "$import_lib"
fi
"$cc" -std=c11 -Wall -Wextra -Werror -O2 -I"$headers" \
    "$root/tools/gpu-probe/present_probe.c" "$import_lib" -luser32 -o "$out/present-probe.exe"
python3 - "$root" "$out" <<'PY'
import hashlib, json, pathlib, subprocess, sys
root, out = map(pathlib.Path, sys.argv[1:])
files = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file() and p.name != 'manifest.json'}
commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
(out / 'manifest.json').write_text(json.dumps({'schema': 1, 'commit': commit, 'platform': 'windows-x64', 'files': files}, indent=2) + '\n')
PY
printf 'Built Win32 present probe: %s\n' "$out/present-probe.exe"
