#!/usr/bin/env python3
"""Run one bounded command only after atomically claiming an explicit iPad record.

All cooperating writers must flock record.with_suffix('.lock') and preserve
unknown JSON fields. This wrapper is not a sandbox for commands that deliberately
escape their process group, nor can it stop an independently launched iPad app.
"""
import argparse
from contextlib import contextmanager
from datetime import datetime, timedelta, timezone
import fcntl
import json
import math
import os
from pathlib import Path
import signal
import stat
import subprocess
import sys
import tempfile
import time
import uuid

MAX_RECORD_BYTES = 1024 * 1024
STOP_GRACE_SECONDS = 2.0
TOKEN_FIELD = "commandLease"


class LeaseError(Exception):
    pass


def now():
    return datetime.now(timezone.utc)


def timestamp(value):
    return value.isoformat()


def paused(record):
    value = record.get("deviceWorkPaused", False)
    if not isinstance(value, bool):
        raise LeaseError("deviceWorkPaused must be a boolean")
    status = record.get("status", "")
    if not isinstance(status, str):
        raise LeaseError("status must be a string")
    return value or "paused" in status.lower() or status.lower().startswith("user-stopped") or status.lower() == "stopped"


def check_permission(record, owner):
    if "allowedOwners" not in record:
        return
    allowed = record["allowedOwners"]
    if not isinstance(allowed, list) or any(not isinstance(item, str) for item in allowed):
        raise LeaseError("allowedOwners must be a string array")
    if owner not in allowed:
        raise LeaseError("owner is not in allowedOwners")


def validate(record):
    if not isinstance(record, dict) or "owner" not in record:
        raise LeaseError("record must be an object with an explicit owner")
    if record["owner"] is not None and (not isinstance(record["owner"], str) or not record["owner"]):
        raise LeaseError("owner must be null or a nonempty string")
    if paused(record):
        raise LeaseError("device work is paused")


class Lease:
    def __init__(self, path, owner, thread, seconds):
        self.path = Path(path).absolute()
        self.owner, self.thread, self.seconds = owner, thread, seconds
        # Shared convention: ipad-access.json -> ipad-access.lock.
        self.lock_path = self.path.with_suffix(".lock")
        if self.lock_path == self.path:
            raise LeaseError("record path must not itself end in .lock")
        self.token = str(uuid.uuid4())
        self.reserved_until = None

    @contextmanager
    def transaction(self):
        flags = os.O_RDWR | os.O_CREAT | getattr(os, "O_NOFOLLOW", 0)
        fd = os.open(self.lock_path, flags, 0o600)
        try:
            if not stat.S_ISREG(os.fstat(fd).st_mode):
                raise LeaseError("lock is not a regular file")
            deadline = time.monotonic() + 2
            while True:
                try:
                    fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    break
                except BlockingIOError:
                    if time.monotonic() >= deadline:
                        raise LeaseError("coordination lock is busy")
                    time.sleep(0.025)
            yield
        finally:
            os.close(fd)

    def read(self):
        fd = os.open(self.path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
        with os.fdopen(fd, "rb") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size > MAX_RECORD_BYTES:
                raise LeaseError("record is not a bounded regular file")
            data = stream.read(MAX_RECORD_BYTES + 1)
        if len(data) > MAX_RECORD_BYTES:
            raise LeaseError("record is too large")
        result = json.loads(data)
        if not isinstance(result, dict):
            raise LeaseError("record must be a JSON object")
        return result

    def write(self, record):
        # Same-directory replace keeps a readable complete document between updates;
        # the stable separate lock coordinates writers across inode replacement.
        mode = stat.S_IMODE(self.path.lstat().st_mode)
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=self.path.parent,
                                             prefix=self.path.name + ".", delete=False) as stream:
                temporary = Path(stream.name)
                os.fchmod(stream.fileno(), mode)
                json.dump(record, stream, indent=2, ensure_ascii=False)
                stream.write("\n")
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, self.path)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)

    def matches(self, record):
        lease = record.get(TOKEN_FIELD)
        return (record.get("owner") == self.owner and record.get("threadId") == self.thread
                and isinstance(lease, dict) and lease.get("token") == self.token
                and record.get("reservedUntil") == self.reserved_until)

    def claim(self):
        with self.transaction():
            record = self.read()
            validate(record)
            check_permission(record, self.owner)
            owner = record["owner"]
            if owner is not None:
                if owner != self.owner or record.get("threadId") != self.thread:
                    raise LeaseError("device is owned by another owner/thread (expiry is not takeover permission)")
                # Do not overlap a currently active operation, even from this thread.
                # Only a legacy expired reservation of the same identity may be renewed.
                try:
                    expiry = datetime.fromisoformat(record["reservedUntil"].replace("Z", "+00:00"))
                except (KeyError, AttributeError, TypeError, ValueError):
                    raise LeaseError("same-identity reservation has no valid expiry; release it explicitly")
                if expiry.tzinfo is None or expiry > now():
                    raise LeaseError("same-identity reservation is still active; use one wrapper for sequential commands")
            if record.get(TOKEN_FIELD) is not None:
                raise LeaseError("a wrapper lease already exists; inspect its process before explicit recovery")
            claimed = now()
            self.reserved_until = timestamp(claimed + timedelta(seconds=self.seconds + STOP_GRACE_SECONDS + 3))
            record.update(owner=self.owner, threadId=self.thread, status="command-lease-active",
                          updatedAt=timestamp(claimed),
                          reservedUntil=self.reserved_until)
            record[TOKEN_FIELD] = {"token": self.token, "wrapperPid": os.getpid(),
                                   "timeoutSeconds": self.seconds, "claimedAt": timestamp(claimed)}
            self.write(record)

    def check(self):
        with self.transaction():
            record = self.read()
            if not self.matches(record):
                raise LeaseError("lease ownership changed; stopping only this wrapper's process group")
            validate(record)
            check_permission(record, self.owner)

    def release(self):
        with self.transaction():
            record = self.read()
            if not self.matches(record):
                return False
            record.pop(TOKEN_FIELD)
            record.update(owner=None, lastOwner=self.owner, updatedAt=timestamp(now()), reservedUntil=timestamp(now()))
            if record.get("status") == "command-lease-active":
                record["status"] = "command-lease-released"
            self.write(record)
            return True


def stop_group(process):
    """Stop descendants as well as the direct child before making the record free."""
    def send(sig):
        try:
            os.killpg(process.pid, sig)
            return True
        except ProcessLookupError:
            return False
    if send(signal.SIGTERM):
        deadline = time.monotonic() + STOP_GRACE_SECONDS
        while time.monotonic() < deadline:
            process.poll()  # Reap the direct child so an otherwise empty group vanishes.
            try:
                os.killpg(process.pid, 0)
            except ProcessLookupError:
                break
            time.sleep(0.025)
        send(signal.SIGKILL)
    try:
        process.wait(timeout=STOP_GRACE_SECONDS)
    except subprocess.TimeoutExpired:
        raise LeaseError("child did not exit after SIGKILL; reservation retained for inspection")


def run(lease, command):
    lease.claim()  # No Popen is reachable after a rejected claim.
    process = None
    caught = []
    previous = {}
    try:
        for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
            previous[sig] = signal.signal(sig, lambda number, frame: caught.append(number))
        if caught:
            return 128 + caught[0]
        process = subprocess.Popen(command, start_new_session=True)
        deadline = time.monotonic() + lease.seconds
        while True:
            if caught:
                return 128 + caught[0]
            code = process.poll()
            if code is not None:
                return code if code >= 0 else 128 - code
            if time.monotonic() >= deadline:
                print("iPad lease: command timeout; stopping its process group", file=sys.stderr)
                return 124
            lease.check()
            time.sleep(min(0.1, max(0, deadline - time.monotonic())))
    finally:
        cleanup_complete = process is None
        try:
            if process is not None:
                stop_group(process)
                cleanup_complete = True
        finally:
            try:
                if cleanup_complete:
                    if not lease.release():
                        print("iPad lease: record changed; newer claim preserved", file=sys.stderr)
                else:
                    print("iPad lease: process cleanup unconfirmed; reservation retained", file=sys.stderr)
            finally:
                for sig, handler in previous.items():
                    signal.signal(sig, handler)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--record", type=Path, required=True, help="explicit existing coordination JSON; no default device")
    parser.add_argument("--owner", required=True)
    parser.add_argument("--thread-id", required=True)
    parser.add_argument("--minutes", type=float, required=True, help="command timeout, greater than 0 and at most 60")
    parser.add_argument("command", nargs=argparse.REMAINDER, help="-- COMMAND [ARG ...]; no implicit shell")
    args = parser.parse_args()
    if not args.owner.strip() or not args.thread_id.strip() or not math.isfinite(args.minutes) or not 0 < args.minutes <= 60:
        parser.error("owner/thread must be nonempty and minutes must be finite in (0, 60]")
    if not args.command or args.command[0] != "--" or len(args.command) < 2:
        parser.error("provide -- followed by an explicit command")
    try:
        return run(Lease(args.record, args.owner, args.thread_id, args.minutes * 60), args.command[1:])
    except (LeaseError, OSError, ValueError) as error:
        print(f"iPad lease: refused/failed: {error}", file=sys.stderr)
        return 75


if __name__ == "__main__":
    raise SystemExit(main())
