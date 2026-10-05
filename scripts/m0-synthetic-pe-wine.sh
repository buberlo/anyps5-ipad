#!/usr/bin/env bash
# Turn AnyPS5's synthetic ELF fixture into a Windows PE with the Linux
# relinker, then run that PE under the host Wine. The fixture is the same
# image test_optional_plt.py executes on Windows: mov eax, 42; ret.
# No game data. Lavapipe is selected when the ICD is installed; this PE
# does not call Vulkan, so a green run does not exercise the GPU path.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
relinker="${RELINKER:-$root/build/anyps5/core/relinker/relinker}"
wine_bin="${WINE:-}"
if [ -z "$wine_bin" ]; then
    if command -v wine64 >/dev/null 2>&1; then
        wine_bin=wine64
    elif command -v wine >/dev/null 2>&1; then
        wine_bin=wine
    else
        echo "wine is not installed" >&2
        exit 1
    fi
fi
if [ ! -x "$relinker" ]; then
    echo "relinker not found at $relinker (build it with scripts/m0-build-anyps5.sh)" >&2
    exit 1
fi

work="$root/build/synthetic-pe"
rm -rf "$work"
mkdir -p "$work"

python3 - "$root" "$work/input.elf" <<'PY'
import importlib.util
import sys
from pathlib import Path

root, dest = sys.argv[1:]
path = Path(root) / "upstreams/AnyPS5/core/relinker/relinker/tests/test_optional_plt.py"
spec = importlib.util.spec_from_file_location("anyps5_optional_plt", path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
Path(dest).write_bytes(module.fixture())
PY

"$relinker" --skip-sce-module --windows "$work/input.elf" "$work/sample.exe"
"$relinker" --skip-sce-module --windows --to-intel "$work/input.elf" "$work/sample-intel.exe"
python3 - "$work/sample.exe" "$work/sample-intel.exe" <<'PY'
import struct, sys
from pathlib import Path

def subsystem(path):
    pe = Path(path).read_bytes()
    assert pe[:2] == b"MZ", path
    offset = struct.unpack_from("<I", pe, 0x3C)[0]
    return struct.unpack_from("<H", pe, offset + 24 + 68)[0]

for path in sys.argv[1:]:
    sub = subsystem(path)
    print(f"{path}: MZ subsystem={sub}")
    if sub != 3:
        raise SystemExit(f"{path}: expected IMAGE_SUBSYSTEM_WINDOWS_CUI (3)")
PY

icd=""
for candidate in /usr/share/vulkan/icd.d/lvp_icd.json /usr/share/vulkan/icd.d/lvp_icd.x86_64.json; do
    if [ -f "$candidate" ]; then icd="$candidate"; break; fi
done
if [ -n "$icd" ]; then
    export VK_DRIVER_FILES="$icd"
    export VK_LOADER_LAYERS_DISABLE='~all~'
    echo "VK_DRIVER_FILES=$icd"
else
    echo "lavapipe ICD not installed; Wine will use the default Vulkan loader"
fi

export WINEPREFIX="${WINEPREFIX:-$work/prefix}"
export WINEDEBUG="${WINEDEBUG:--all}"
mkdir -p "$WINEPREFIX"

run_one() {
    local pe="$1"
    echo "== $wine_bin $pe =="
    set +e
    "$wine_bin" "$pe"
    local status=$?
    set -e
    echo "exit=$status"
    if [ "$status" -ne 42 ]; then
        echo "expected process exit 42 from $pe" >&2
        exit 1
    fi
}

run_one "$work/sample.exe"
run_one "$work/sample-intel.exe"
echo "synthetic PE ran under Wine with exit 42"
