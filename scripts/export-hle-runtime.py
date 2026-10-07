#!/usr/bin/env python3
"""Export only built HLE PRXs and their compiler DLLs; never export game data."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/runtime-probes"))
from pe_image import PEImage


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--unpatched", required=True, type=Path)
    parser.add_argument("--runtime-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--provenance", type=Path, help="Local build metadata to include in the manifest")
    args = parser.parse_args()
    # An explicit allowlist keeps dumps and unrelated build outputs out of the archive.
    libraries = sorted(args.unpatched.glob("*.prx"))
    if not libraries:
        raise ValueError("No built HLE PRXs")
    inputs = [("unpatched/" + p.name, p) for p in libraries]
    inputs += [("runtime/" + name, args.runtime_dir / name)
               for name in ("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")]
    contents = {}
    for name, path in inputs:
        if path.is_symlink():
            raise ValueError(f"Refusing symlink {path}")
        image = PEImage(path)
        if name == "unpatched/libc.prx" and "GuestArenaAllocate_nid_postfix" not in image.exports()[0]:
            raise ValueError("HLE inputs must be unpatched")
        contents[name] = image.data
    metadata = {"schema": 1, "kind": "anyps5_unpatched_hle_runtime",
                "source_commit": os.environ.get("APS5_SOURCE_COMMIT") or os.environ.get("GITHUB_SHA"),
                "toolchain": "WinLibs GCC 15.2.0 posix-seh UCRT r7",
                "files": {name: hashlib.sha256(data).hexdigest() for name, data in contents.items()},
                "contains_game_data": False, "runtime_verified": False}
    if args.provenance:
        metadata["build_provenance"] = json.loads(args.provenance.read_text())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=args.output.parent, suffix=".zip")
    os.close(fd)
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for name, data in sorted(contents.items()):
                archive.writestr(name, data)
            archive.writestr("hle-manifest.json", json.dumps(metadata, indent=2) + "\n")
        os.replace(temporary, args.output)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    print(f"Exported {len(libraries)} unpatched HLE libraries and three compiler DLLs: {args.output}")


if __name__ == "__main__":
    main()
