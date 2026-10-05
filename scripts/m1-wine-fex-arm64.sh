#!/usr/bin/env bash
# M1 groundwork: run an AnyPS5 Windows PE under Wine + FEX on ARM64 Linux.
# This host check refuses to pretend the run happened on x86-64.
# No game data is downloaded. Point PE at a PE you built with M0
# (a relinker self-test or your own legally obtained executable).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
pe="${1:-}"

if [ "$(uname -m)" != "aarch64" ]; then
    cat <<EOF
M1 was not run. This machine is $(uname -m), not aarch64.
On an ARM64 Linux host the intended command, after FEX and Wine are installed, is:

  FEXInterpreter wine64 ${pe:-/path/to/anyps5.exe}

Graphics are out of scope for M1. Expect the process to get as far as the
guest entry and then fail on a missing title, a Vulkan loader, or the
448 GiB GuestArena reservation. For the arena, export before launching:

  APS5_GUEST_ARENA_LAZY=1
  APS5_GUEST_ARENA_SIZE=0x100000000

That last value is 4 GiB and is a guess, not a measurement.
EOF
    exit 0
fi

if [ -z "$pe" ]; then
    echo "usage: $0 /path/to/program.exe" >&2
    exit 1
fi
if ! command -v FEXInterpreter >/dev/null 2>&1; then
    echo "FEXInterpreter is not on PATH" >&2
    exit 1
fi
if ! command -v wine64 >/dev/null 2>&1; then
    echo "wine64 is not on PATH" >&2
    exit 1
fi

export APS5_GUEST_ARENA_LAZY="${APS5_GUEST_ARENA_LAZY:-1}"
export APS5_GUEST_ARENA_SIZE="${APS5_GUEST_ARENA_SIZE:-0x100000000}"
exec FEXInterpreter wine64 "$pe"
