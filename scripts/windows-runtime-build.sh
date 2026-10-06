#!/usr/bin/env bash
# Shared primary/fallback Windows build. Runtime execution is a separate job.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
export PYTHONDONTWRITEBYTECODE=1
export ELF_CC=clang.exe ELF_LD=ld.lld.exe ELF_OBJCOPY=llvm-objcopy.exe ELF_NM=llvm-nm.exe
for tool in "$ELF_CC" "$ELF_LD" "$ELF_OBJCOPY" "$ELF_NM"; do
    command -v "$tool" >/dev/null || { echo "Missing ELF tool: $tool" >&2; exit 1; }
done
python3 tools/runtime-probes/test_runtime_reference.py
scripts/m0-build-anyps5-winlibs.sh
export PATH="$root/build/toolchains/winlibs/mingw64/bin:$PATH"
relinker="$root/build/anyps5-winlibs/core/relinker/relinker.exe"
nid_patcher="$root/build/anyps5-winlibs/core/libs/nid_patcher.exe"
WINDOWS_CXX=g++ NID_PATCHER="$nid_patcher" python3 tools/runtime-probes/test_runtime_packaging.py
scripts/build-demo.sh --profile smoke --output "$root/build/demo-smoke" --relinker "$relinker"
scripts/build-runtime-guest.sh --relinker "$relinker"
scripts/build-runtime-exceptions.sh --relinker "$relinker"
HOST_CXX=g++ WINDOWS_CXX=g++ scripts/build-runtime-probes.sh
artifact="$root/build/windows-runtime-artifact"
mkdir -p "$artifact/probes"
python3 scripts/package-runtime.py --build "$root/build/anyps5-winlibs" \
    --nid-patcher "$nid_patcher" --demo "$root/build/demo-smoke" \
    --runtime-dir "$root/build/toolchains/winlibs/mingw64/bin" \
    --output "$root/build/runtime-package-windows" --replace --zip "$artifact/runtime-package.zip"
cp build/runtime-probes/*-probe.exe "$artifact/probes/"
cp build/runtime-probes/cpu-reference.jsonl "$artifact/probes/"
printf 'Prepared runtime package and probes in %s. Runtime verification is a separate CI job.\n' "$artifact"
