#!/usr/bin/env bash
# Run AnyPS5's synthetic relinker tests. They build tiny ELF fixtures in
# temp directories. They do not read a game dump.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
relinker="${1:-${RELINKER:-$root/build/anyps5/core/relinker/relinker}}"
tests="$root/upstreams/AnyPS5/core/relinker/relinker/tests"
if [ ! -x "$relinker" ]; then
    echo "relinker not found at $relinker" >&2
    exit 1
fi

fail=0
for test in "$tests"/test_*.py; do
    name="$(basename "$test")"
    if python3 "$test" "$relinker"; then
        echo "PASS $name"
    else
        echo "FAIL $name" >&2
        fail=$((fail + 1))
    fi
done
echo "failed=$fail"
[ "$fail" -eq 0 ]
