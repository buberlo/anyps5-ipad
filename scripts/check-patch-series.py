#!/usr/bin/env python3
"""Check an applied overlapping patch stack by reversing a disposable copy.

Only patch-touched files are copied. The real checkout is never modified, and
git's normal path validation stays enabled. Return 0 only if the whole series
can be reversed in order. git apply --reverse --check on several overlapping
patches does not simulate each intermediate tree, hence the scratch copy.
"""
import pathlib
import shlex
import shutil
import subprocess
import sys
import tempfile


def applied(repo, patches):
    paths = set()
    for patch in patches:
        for line in patch.read_text().splitlines():
            if not line.startswith("diff --git "):
                continue
            fields = shlex.split(line)
            for field in fields[2:4]:
                path = pathlib.PurePosixPath(field[2:])
                if not field.startswith(("a/", "b/")) or path.is_absolute() or ".." in path.parts:
                    raise ValueError("unsafe patch path: " + field)
                paths.add(path)
    with tempfile.TemporaryDirectory(prefix="anyps5-patch-check-") as directory:
        scratch = pathlib.Path(directory)
        for path in paths:
            source, target = repo / path, scratch / path
            if source.is_symlink():
                raise ValueError("patch target must not be a symlink: " + str(source))
            if source.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
        for patch in reversed(patches):
            check = subprocess.run(["git", "apply", "--reverse", str(patch)], cwd=scratch,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if check.returncode:
                return False
    return True


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit("usage: check-patch-series.py REPO PATCH...")
    sys.exit(0 if applied(pathlib.Path(sys.argv[1]).resolve(),
                         [pathlib.Path(p).resolve() for p in sys.argv[2:]]) else 1)
