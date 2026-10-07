#!/usr/bin/env python3
"""Validate live FEX output from guest_argument_probe.c, excluding IR listings.

This checks the synthetic tracer contract, not arbitrary game execution.
Raw game traces must stay outside the public repository.
"""
import argparse
import json
import re
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    args = parser.parse_args()
    text = args.log.read_text(errors='replace')
    active = {}
    completed = []
    for line in text.splitlines():
        match = re.match(r'^E ([0-9a-fA-F]+) (\[guest-args\].*)$', line)
        if not match:
            continue
        thread, message = match.groups()
        if message.startswith('[guest-args] begin:'):
            assert thread not in active, 'nested or incomplete trace record'
            active[thread] = []
        elif message == '[guest-args] end':
            assert thread in active, 'end without begin'
            completed.append(active.pop(thread))
        elif message.startswith('[guest-args] value='):
            assert thread in active, 'value without begin'
            active[thread].append(int(message.split('=', 1)[1], 16))
    assert not active, 'incomplete trace record'
    assert len(completed) == 10, f'expected 10 calls, found {len(completed)}'
    base = 0x7400000000
    for values in completed:
        assert len(values) == 28, 'expected 7 registers and 21 pointer words'
        assert values[1] == base + 23 * 64, 'owner pointer differs'
        assert values[7:] == [base + i * 64 for i in range(21)], 'pointer words differ'
    assert len({values[0] for values in completed}) == 1, 'trace instruction differs'
    assert re.search(r'^\[arg-probe\] calls=10 pointer_checks=210 failures=0 checksum=c22c463457389aa8\r?$', text, re.M), 'guest preservation checks did not pass'
    print(json.dumps({'synthetic_trace_passed': True, 'calls': 10, 'pointer_checks': 210,
                      'callee_saved_registers_checked': ['r12', 'r13', 'r14', 'r15'],
                      'gameplay_qualified': False}))


if __name__ == '__main__':
    main()
