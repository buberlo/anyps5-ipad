#!/usr/bin/env python3
"""Apply each tracked patch stack to pristine touched files, then reverse it.

This catches context/order mistakes without resetting developers' submodules.
Nested tracked source (FEX/rpmalloc) is read from that nested repository's HEAD.
"""
import hashlib
import importlib.util
import pathlib
import shlex
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("patch_check", ROOT / "scripts/check-patch-series.py")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


def base_content(repo, path):
    candidate = repo / path
    directory = candidate.parent
    while not directory.exists():
        directory = directory.parent
    top = pathlib.Path(subprocess.check_output(["git", "-C", str(directory), "rev-parse", "--show-toplevel"], text=True).strip())
    relative = candidate.relative_to(top)
    proc = subprocess.run(["git", "-C", str(top), "show", "HEAD:" + relative.as_posix()], capture_output=True)
    return proc.stdout if proc.returncode == 0 else None


def main():
    for name, upstream in (("anyps5", "AnyPS5"), ("madeira", "Madeira"), ("wine", "wine"), ("fex", "FEX")):
        patches = sorted((ROOT / "patches" / name).glob("*.patch"))
        paths = set()
        for patch in patches:
            for line in patch.read_text().splitlines():
                if line.startswith("diff --git "):
                    paths.update(pathlib.Path(p[2:]) for p in shlex.split(line)[2:4])
        with tempfile.TemporaryDirectory(prefix="anyps5-pristine-check-") as tmp:
            scratch = pathlib.Path(tmp)
            before = {}
            for path in paths:
                data = base_content(ROOT / "upstreams" / upstream, path)
                before[str(path)] = None if data is None else hashlib.sha256(data).hexdigest()
                if data is not None:
                    target = scratch / path
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(data)
            for patch in patches:
                subprocess.run(["git", "apply", "--whitespace=error-all", str(patch)], cwd=scratch, check=True)
            assert checker.applied(scratch, patches), name + ": complete-stack detection failed"
            for patch in reversed(patches):
                subprocess.run(["git", "apply", "--reverse", str(patch)], cwd=scratch, check=True)
            for path, digest in before.items():
                file = scratch / path
                assert (hashlib.sha256(file.read_bytes()).hexdigest() if file.exists() else None) == digest, name + ": reverse differs: " + path
        print(f"PASS {name}: {len(patches)} patches apply, detect and reverse on pristine source")


if __name__ == "__main__":
    main()
