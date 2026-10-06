#!/usr/bin/env python3
"""Real subprocess tests using temporary coordination records; no device access."""
import fcntl
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
HELPER = ROOT / "scripts/with-ipad-lease.py"


class LeaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.dir = Path(self.temp.name)
        self.record = self.dir / "ipad-access.json"
        self.marker = self.dir / "command-ran"
        self.original = {"owner": None, "status": "ready", "deviceWorkPaused": False,
                         "allowedOwners": ["anyps5-ipad", "other"], "threadId": "previous-thread",
                         "nextRequestedPhases": [{"owner": "other", "phase": "preserve me"}],
                         "unrelated": {"nested": [1, 2, 3]}}
        self.write(self.original)

    def write(self, data):
        self.record.write_text(json.dumps(data))

    def read(self):
        return json.loads(self.record.read_text())

    def argv(self, code, minutes="0.1", owner="anyps5-ipad", thread="thread-a"):
        return [sys.executable, str(HELPER), "--record", str(self.record), "--owner", owner,
                "--thread-id", thread, "--minutes", minutes, "--", sys.executable, "-c", code]

    def execute(self, code=None, **kwargs):
        if code is None:
            code = f"from pathlib import Path; Path({str(self.marker)!r}).write_text('ran')"
        return subprocess.run(self.argv(code, **kwargs), capture_output=True, text=True, timeout=10)

    def assert_denied(self, data):
        self.write(data)
        before = self.record.read_bytes()
        result = self.execute()
        self.assertEqual(result.returncode, 75, result.stderr)
        self.assertFalse(self.marker.exists())
        self.assertEqual(self.record.read_bytes(), before)

    def test_occupied_and_expired_foreign_never_launch(self):
        for expiry in ("2099-01-01T00:00:00+00:00", "2000-01-01T00:00:00+00:00"):
            with self.subTest(expiry=expiry):
                self.assert_denied(dict(self.original, owner="other", threadId="other-thread", reservedUntil=expiry))

    def test_paused_never_launches(self):
        self.assert_denied(dict(self.original, deviceWorkPaused=True))
        self.assert_denied(dict(self.original, status="user-stopped-all-game-work"))

    def test_active_same_identity_does_not_overlap(self):
        self.assert_denied(dict(self.original, owner="anyps5-ipad", threadId="thread-a",
                                reservedUntil="2099-01-01T00:00:00+00:00"))
        self.assert_denied(dict(self.original, owner="anyps5-ipad", threadId="different-thread",
                                reservedUntil="2000-01-01T00:00:00+00:00"))

    def test_only_expired_legacy_same_identity_can_be_renewed(self):
        record = dict(self.original, owner="anyps5-ipad", threadId="thread-a", reservedUntil="2000-01-01T00:00:00+00:00")
        self.write(record)
        self.assertEqual(self.execute().returncode, 0)
        self.assertTrue(self.marker.exists())
        self.marker.unlink()
        self.assert_denied(dict(record, commandLease={"token": "stale-wrapper-needs-inspection"}))

    def test_missing_owner_invalid_pause_and_not_allowed_fail_closed(self):
        missing = dict(self.original); missing.pop("owner")
        for record in (missing, dict(self.original, deviceWorkPaused="false"), dict(self.original, allowedOwners=["other"]), dict(self.original, allowedOwners=None)):
            with self.subTest(record=record):
                self.assert_denied(record)

    def test_success_releases_preserves_unrelated_fields_and_shared_lock_name(self):
        result = self.execute()
        self.assertEqual(result.returncode, 0, result.stderr)
        current = self.read()
        self.assertIsNone(current["owner"])
        self.assertNotIn("commandLease", current)
        self.assertEqual(current["nextRequestedPhases"], self.original["nextRequestedPhases"])
        self.assertEqual(current["unrelated"], self.original["unrelated"])
        self.assertTrue(self.record.with_suffix(".lock").exists())
        self.assertFalse(Path(str(self.record) + ".lock").exists())

    def test_child_failure_is_propagated_and_released(self):
        result = self.execute("raise SystemExit(17)")
        self.assertEqual(result.returncode, 17, result.stderr)
        self.assertIsNone(self.read()["owner"])

    def test_spawn_failure_releases(self):
        command = self.argv("")[:-3] + [str(self.dir / "missing-executable")]
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 75, result.stderr)
        self.assertIsNone(self.read()["owner"])

    def test_no_long_command_lock_and_concurrent_metadata_is_preserved(self):
        code = f'''import fcntl,json
from pathlib import Path
p=Path({str(self.record)!r})
with p.with_suffix('.lock').open('r+') as lock:
 fcntl.flock(lock, fcntl.LOCK_EX|fcntl.LOCK_NB)
 r=json.loads(p.read_text()); r['child_metadata']='keep'; r['nextRequestedPhases'].append({{'owner':'later'}})
 p.write_text(json.dumps(r))
'''
        result = self.execute(code)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.read()["child_metadata"], "keep")
        self.assertEqual(self.read()["nextRequestedPhases"][-1], {"owner": "later"})

    def test_finally_never_stomps_new_owner_or_new_same_identity_token(self):
        for owner in ("other", "anyps5-ipad"):
            self.write(self.original)
            code = f'''import fcntl,json
from pathlib import Path
p=Path({str(self.record)!r})
with p.with_suffix('.lock').open('r+') as lock:
 fcntl.flock(lock,fcntl.LOCK_EX)
 r=json.loads(p.read_text()); r.update(owner={owner!r},threadId='thread-a',status='new-owner-status',commandLease={{'token':'newer-token'}},reservedUntil='2099-01-01T00:00:00+00:00')
 p.write_text(json.dumps(r))
'''
            result = self.execute(code)
            self.assertEqual(result.returncode, 0, result.stderr)
            current = self.read()
            self.assertEqual(current["owner"], owner)
            self.assertEqual(current["commandLease"], {"token": "newer-token"})
            self.assertEqual(current["status"], "new-owner-status")

    def test_parallel_wrapper_claim_is_rejected(self):
        first = subprocess.Popen(self.argv(f"from pathlib import Path; import time; Path({str(self.marker)!r}).touch(); time.sleep(0.7)"), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 3
            while not self.marker.exists() and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertTrue(self.marker.exists())
            result = self.execute("raise SystemExit(99)")
            self.assertEqual(result.returncode, 75, result.stderr)
            self.assertEqual(first.communicate(timeout=5)[1], "")
            self.assertEqual(first.returncode, 0)
        finally:
            if first.poll() is None:
                first.terminate(); first.communicate(timeout=5)

    def test_timeout_kills_descendant_before_release(self):
        late = self.dir / "late-descendant"
        child = f"import time; from pathlib import Path; time.sleep(1); Path({str(late)!r}).touch()"
        code = f"import subprocess,sys,time; subprocess.Popen([sys.executable,'-c',{child!r}]); time.sleep(10)"
        result = self.execute(code, minutes="0.003")
        self.assertEqual(result.returncode, 124, result.stderr)
        self.assertIsNone(self.read()["owner"])
        time.sleep(1.1)
        self.assertFalse(late.exists())

    def test_pause_requested_during_command_stops_it_and_preserves_pause(self):
        code = f"from pathlib import Path; import time; Path({str(self.marker)!r}).touch(); time.sleep(10)"
        child = subprocess.Popen(self.argv(code), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 3
            while not self.marker.exists() and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertTrue(self.marker.exists())
            with self.record.with_suffix(".lock").open("r+") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                record = self.read(); record.update(deviceWorkPaused=True, status="paused-by-user")
                self.write(record)
            output = child.communicate(timeout=5)
            self.assertEqual(child.returncode, 75, output)
            self.assertTrue(self.read()["deviceWorkPaused"])
            self.assertEqual(self.read()["status"], "paused-by-user")
            self.assertIsNone(self.read()["owner"])
        finally:
            if child.poll() is None:
                child.terminate(); child.communicate(timeout=5)

    def test_sigterm_releases_after_stopping_own_child(self):
        code = f"from pathlib import Path; import time; Path({str(self.marker)!r}).touch(); time.sleep(10)"
        child = subprocess.Popen(self.argv(code), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 3
            while not self.marker.exists() and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertTrue(self.marker.exists())
            child.terminate()
            output = child.communicate(timeout=5)
            self.assertEqual(child.returncode, 143, output)
            self.assertIsNone(self.read()["owner"])
        finally:
            if child.poll() is None:
                child.kill(); child.communicate(timeout=5)

    def test_successful_leader_cannot_leave_ordinary_background_child_running(self):
        late = self.dir / "late-child"
        child = f"import time; from pathlib import Path; time.sleep(1); Path({str(late)!r}).touch()"
        code = f"import subprocess,sys; subprocess.Popen([sys.executable,'-c',{child!r}])"
        result = self.execute(code)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIsNone(self.read()["owner"])
        time.sleep(1.1)
        self.assertFalse(late.exists())

    def test_explicit_record_required_and_no_implicit_command(self):
        result = subprocess.run([sys.executable, str(HELPER), "--owner", "anyps5-ipad", "--thread-id", "t", "--minutes", "1", "--", sys.executable, "-c", "raise SystemExit(99)"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("--record", result.stderr)
        result = subprocess.run(self.argv("")[:-4], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)


if __name__ == "__main__":
    unittest.main()
