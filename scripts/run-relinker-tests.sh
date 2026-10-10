#!/usr/bin/env bash
# Run AnyPS5's synthetic relinker tests. They build tiny ELF fixtures in
# temp directories. They do not read a game dump.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
relinker="${1:-${RELINKER:-$root/build/host-tools/build/relinker/relinker}}"
tests="${APS5_ANYPS5_SOURCE:-$root/upstreams/AnyPS5}/core/relinker/relinker/tests"
fixture="${2:-${APS5_GUEST_INIT_FINI_FIXTURE:-$(dirname "$relinker")/guest_init_fini_host_fixture.prx}}"
if [ ! -x "$relinker" ]; then
    echo "relinker not found at $relinker" >&2
    exit 1
fi

fail=0
for test in "$tests"/test_*.py; do
    name="$(basename "$test")"
    arguments=("$relinker")
    if [ "$name" = test_guest_init_fini.py ] && [ -f "$fixture" ]; then
        arguments+=("$fixture")
    fi
    if python3 "$test" "${arguments[@]}"; then
        echo "PASS $name"
    else
        echo "FAIL $name" >&2
        fail=$((fail + 1))
    fi
done
echo "failed=$fail"
echo "This runner covers Python fixtures only; BUILD_TESTING=ON and CTest also cover C++ contracts."
[ "$fail" -eq 0 ]
