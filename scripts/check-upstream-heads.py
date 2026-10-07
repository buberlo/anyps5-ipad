#!/usr/bin/env python3
"""Compare pinned gitlinks with live upstream branches, without modifying either.

Run locally before a runtime update. A moving branch is never silently substituted
for a reproducible pin. Exit 1 means integration is needed; exit 2 means a query
failed and freshness could not be established.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
UPSTREAMS = (
    ("AnyPS5", "main"),
    ("Madeira", "main"),
    ("FEX", "ios-port-2607"),
    ("wine", "madeira-lgpl"),
    ("MoltenVK", "main"),
)


def git(*args):
    return subprocess.check_output(
        ["git", "-C", str(ROOT), *args], text=True,
        stderr=subprocess.PIPE, timeout=30,
    ).strip()


def check(item):
    name, branch = item
    path = "upstreams/" + name
    record = {"name": name, "branch": branch}
    try:
        entry = git("ls-files", "--stage", "--", path).split()
        if len(entry) != 4 or entry[0] != "160000" or entry[2] != "0":
            raise ValueError("expected one resolved gitlink in the index")
        pin = entry[1]
        url = git("config", "-f", ".gitmodules", "--get", "submodule." + path + ".url")
        refs = git("ls-remote", "--exit-code", url, "refs/heads/" + branch).split()
        if len(refs) != 2 or refs[1] != "refs/heads/" + branch:
            raise ValueError("expected exactly the requested remote branch")
        head = refs[0]
        if len(head) != 40 or any(c not in "0123456789abcdef" for c in head):
            raise ValueError("invalid remote commit id")
        record.update(pin=pin, upstream_head=head, status="current" if pin == head else "update_required")
    except (OSError, subprocess.SubprocessError, ValueError) as error:
        # Do not echo transport diagnostics that might contain local credentials.
        record.update(status="query_failed", reason=type(error).__name__)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="emit a machine-readable report")
    args = parser.parse_args()
    with ThreadPoolExecutor(max_workers=len(UPSTREAMS)) as pool:
        records = list(pool.map(check, UPSTREAMS))
    failed = any(r["status"] == "query_failed" for r in records)
    behind = any(r["status"] == "update_required" for r in records)
    if args.json:
        print(json.dumps({"schema": 1, "scope": "live branch heads versus indexed pins",
                          "checked_at": datetime.now(timezone.utc).isoformat(),
                          "modified_files": False, "upstreams": records}, indent=2))
    else:
        for r in records:
            print(f"{r['name']} ({r['branch']}): {r['status']}"
                  + (f" {r['pin'][:12]} -> {r['upstream_head'][:12]}" if "pin" in r else ""))
    return 2 if failed else 1 if behind else 0


if __name__ == "__main__":
    raise SystemExit(main())
