# Running a bounded device command

Use `scripts/with-ipad-lease.py` as the command launcher. A separate guard followed by a device command in a shell is insufficient: a rejected guard can be followed by an install if that shell does not stop on failure.

The wrapper requires an explicit existing coordination record, owner, thread ID, duration and command. It has no default device ID and does not infer a device from the record:

```sh
python3 scripts/with-ipad-lease.py \
  --record "$APS5_RECORD" --owner anyps5-ipad \
  --thread-id "$APS5_THREAD_ID" --minutes 10 \
  -- xcrun devicectl device info details --device "$APS5_DEVICE"
```

For several sequential commands, put the complete phase in one script and run it with a fail-fast shell inside one lease:

```sh
python3 scripts/with-ipad-lease.py \
  --record "$APS5_RECORD" --owner anyps5-ipad \
  --thread-id "$APS5_THREAD_ID" --minutes 10 \
  -- bash -euo pipefail scripts/local-device-phase.sh
```

The phase script must exist and contain the explicitly intended commands. `local-device-phase.sh` above is an example name, not a provided script. Pass device, app and evidence paths explicitly through its arguments or environment. Do not place an unguarded device command after the wrapper. Do not nest wrappers or run another wrapper concurrently for the same owner/thread. A wrapper is deliberately not a persistent interactive lease or a background monitor.

The wrapper uses the existing coordination convention `Path(record).with_suffix('.lock')`: `ipad-access.json` shares **ipad-access.lock** with other writers. All cooperating writers must take that same `flock` while reading and updating the JSON. The lock is released before the command starts; JSON updates replace the file atomically. Unknown fields, notes, authorization data, device identity and queued phases survive updates.

A claim is refused before any child process starts when work is paused, the owner is disallowed, another owner/thread holds the record, or an active reservation already exists for the same identity. An expired foreign reservation is still refused. An expired legacy reservation may be renewed only by its same owner/thread; any existing wrapper token requires inspection and explicit recovery. Missing/malformed records fail closed rather than creating a new coordination state.

The command timeout is greater than zero and at most 60 minutes. `reservedUntil` includes a small cleanup margin, but expiration never grants automatic takeover permission. During execution, the wrapper checks ownership and pause state. On timeout, SIGINT/SIGTERM/SIGHUP, a pause, or loss of ownership, it terminates its own local process group, then sends SIGKILL if necessary. Ordinary background children are cleaned up even if the leading command succeeds. If the direct child cannot be confirmed stopped, the reservation is retained for inspection. A successful cleanup releases only the matching owner, thread and unique token. A newer claim—even for the same owner/thread—remains untouched.

Local process termination cannot stop a remote iPad application, detached debugger service, or a child that deliberately creates another session. Device phases must keep required measurement/debugger work inside the bounded operation and explicitly clean up their own remote app/debugger before returning when that is necessary for handoff. The wrapper is a coordination tool, not a sandbox. SIGKILL or host power loss can leave a stale claim; inspect it before explicit recovery rather than stealing it.

Exit status preserves the command's result. Timeout is 124; a handled signal is 128 plus its number; refusal or an operational lease error is 75; invalid arguments are 2. A successful `devicectl` launch still proves only launch, not JIT execution, displayed frames, or benchmark acceptance.

Tests use temporary records and harmless local subprocesses only:

```sh
python3 -m unittest discover -s tools/runtime-probes -p test_ipad_lease.py -v
```
