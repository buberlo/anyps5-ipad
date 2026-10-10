#!/usr/bin/env python3
"""Apply each tracked patch stack to pristine touched files, then reverse it.

This catches context/order mistakes without resetting developers' submodules.
Nested tracked source (FEX/rpmalloc) is read from that nested repository's HEAD.
"""
import hashlib
import importlib.util
import pathlib
import shlex
import stat
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("patch_check", ROOT / "scripts/check-patch-series.py")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


def base_file(repo, path):
    candidate = repo / path
    directory = candidate.parent
    while not directory.exists():
        directory = directory.parent
    top = pathlib.Path(subprocess.check_output(["git", "-C", str(directory), "rev-parse", "--show-toplevel"], text=True).strip())
    relative = candidate.relative_to(top)
    entries = subprocess.check_output([
        "git", "-C", str(top), "ls-tree", "-z", "HEAD", "--", relative.as_posix(),
    ]).split(b"\0")
    entries = [entry for entry in entries if entry]
    if not entries:
        return None
    if len(entries) != 1:
        raise RuntimeError("expected one HEAD entry for " + str(candidate))
    header, entry_path = entries[0].split(b"\t", 1)
    mode, kind, object_id = header.split()
    if entry_path.decode() != relative.as_posix() or kind != b"blob" or mode not in (b"100644", b"100755"):
        raise RuntimeError("patch baseline must be one regular Git file: " + str(candidate))
    content = subprocess.check_output(["git", "-C", str(top), "cat-file", "blob", object_id.decode()])
    return content, int(mode, 8)


def file_state(path):
    if path.is_symlink():
        raise RuntimeError("patch target must not be a symlink: " + str(path))
    if not path.exists():
        return None
    if not path.is_file():
        raise RuntimeError("patch target must be a regular file: " + str(path))
    # Git records the owner's executable bit, not the full host permissions.
    mode = 0o100755 if path.stat().st_mode & stat.S_IXUSR else 0o100644
    return hashlib.sha256(path.read_bytes()).hexdigest(), mode


def require_equal(actual, expected, message):
    if actual != expected:
        raise RuntimeError(message)


def main():
    for name, upstream in (("anyps5", "AnyPS5"), ("madeira", "Madeira"),
                           ("wine", "wine"), ("fex", "FEX"),
                           ("spirv-cross", "MoltenVK/External/SPIRV-Cross")):
        patches = sorted((ROOT / "patches" / name).glob("*.patch"))
        paths = set()
        for patch in patches:
            for line in patch.read_text().splitlines():
                if line.startswith("diff --git "):
                    for field in shlex.split(line)[2:4]:
                        path = pathlib.PurePosixPath(field[2:])
                        if not field.startswith(("a/", "b/")) or path.is_absolute() or ".." in path.parts:
                            raise ValueError("unsafe patch path: " + field)
                        paths.add(pathlib.Path(path))
        with tempfile.TemporaryDirectory(prefix="anyps5-pristine-check-") as tmp:
            scratch = pathlib.Path(tmp)
            before = {}
            for path in paths:
                baseline = base_file(ROOT / "upstreams" / upstream, path)
                before[str(path)] = None if baseline is None else (hashlib.sha256(baseline[0]).hexdigest(), baseline[1])
                if baseline is not None:
                    target = scratch / path
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(baseline[0])
                    target.chmod(baseline[1] & 0o777)
            for patch in patches:
                subprocess.run(["git", "apply", "--whitespace=error-all", str(patch)], cwd=scratch, check=True)
            for path in paths:
                generated = scratch / path
                current = ROOT / "upstreams" / upstream / path
                require_equal(file_state(generated), file_state(current),
                              name + ": patched source bytes, presence or executable mode differ: " + str(path))
            if not checker.applied(scratch, patches):
                raise RuntimeError(name + ": complete-stack detection failed")
            for patch in reversed(patches):
                subprocess.run(["git", "apply", "--reverse", str(patch)], cwd=scratch, check=True)
            for path, state in before.items():
                file = scratch / path
                require_equal(file_state(file), state, name + ": reversed bytes, presence or executable mode differ: " + path)
        print(f"PASS {name}: {len(patches)} patches apply, match source and modes, detect and reverse on pristine source")


if __name__ == "__main__":
    main()
