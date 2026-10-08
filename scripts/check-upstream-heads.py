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
import re
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


def check_fex_release():
    """A current iOS fork branch does not establish mainline release freshness.

    Do not fetch or change the fork. An ancestry check is available only when
    both commits already exist locally; backported changes need manual review.
    """
    url = "https://github.com/FEX-Emu/FEX.git"
    record = {"name": "FEX mainline release", "url": url}
    try:
        rows = git("ls-remote", "--tags", url, "refs/tags/FEX-[0-9][0-9][0-9][0-9]*").splitlines()
        tags = {}
        for row in rows:
            fields = row.split()
            if len(fields) != 2:
                raise ValueError("invalid tag row")
            sha, ref = fields
            match = re.fullmatch(r"refs/tags/(FEX-(\d{4}))(\^\{\})?", ref)
            if not match:
                continue
            if not re.fullmatch(r"[0-9a-f]{40}", sha):
                raise ValueError("invalid tag object")
            entry = tags.setdefault(match[1], {})
            entry["commit" if match[3] else "tag_object"] = sha
        if not tags:
            raise ValueError("no monthly release tags")
        tag = max(tags, key=lambda name: int(name[4:]))
        # A lightweight tag names its commit directly; an annotated tag's
        # peeled ref, when returned, identifies the commit rather than the tag.
        sha = tags[tag].get("commit", tags[tag]["tag_object"])
        pin = git("rev-parse", ":upstreams/FEX")
        record.update(tag=tag, release_commit=sha, pin=pin,
                      status="release_not_compared", installed_runtime_qualified=False)
        repo = ROOT / "upstreams/FEX"
        exists = subprocess.run(["git", "-C", str(repo), "cat-file", "-e", sha + "^{commit}"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
        if exists.returncode:
            return record
        result = subprocess.run(["git", "-C", str(repo), "merge-base", "--is-ancestor", sha, pin],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
        if result.returncode not in (0, 1):
            raise ValueError("ancestry check failed")
        record["status"] = "release_ancestor_of_pin" if result.returncode == 0 else "release_integration_review_required"
    except (OSError, subprocess.SubprocessError, ValueError) as error:
        record.update(status="query_failed", reason=type(error).__name__)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="emit a machine-readable report")
    parser.add_argument("--fex-release", action="store_true", help="also inspect mainline monthly FEX tags; never fetch or replace the iOS fork")
    args = parser.parse_args()
    with ThreadPoolExecutor(max_workers=len(UPSTREAMS)) as pool:
        records = list(pool.map(check, UPSTREAMS))
    release = check_fex_release() if args.fex_release else None
    failed = any(r["status"] == "query_failed" for r in records)
    behind = any(r["status"] == "update_required" for r in records)
    if release:
        failed |= release["status"] == "query_failed"
        behind |= release["status"] in ("release_not_compared", "release_integration_review_required")
    if args.json:
        report = {"schema": 1, "scope": "live branch heads versus indexed pins",
                  "checked_at": datetime.now(timezone.utc).isoformat(),
                  "modified_files": False, "upstreams": records}
        if release:
            report["fex_mainline_release"] = release
        print(json.dumps(report, indent=2))
    else:
        for r in records:
            print(f"{r['name']} ({r['branch']}): {r['status']}"
                  + (f" {r['pin'][:12]} -> {r['upstream_head'][:12]}" if "pin" in r else ""))
        if release:
            print(f"FEX mainline ({release.get('tag', 'unknown')}): {release['status']}")
    return 2 if failed else 1 if behind else 0


if __name__ == "__main__":
    raise SystemExit(main())
