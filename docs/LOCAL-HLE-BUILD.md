# Local qualified HLE builds

Use `scripts/build-hle.py` on Windows, or on macOS/Linux with a local Wine capable
of running the pinned Windows compiler. It verifies the WinLibs GCC 15.2 posix-SEH
UCRT r7, FFmpeg and embedded Python download hashes. CMake, Ninja, curl and an
archive extractor must be installed locally. No GitHub Actions are used.

Apply the AnyPS5 patches before invoking this helper. An omitted target list builds
all available PRX targets; explicit targets must exist and retain libc/libkernel.
`--from-hle` reads a prior archive's manifest to select names, without reusing old
binaries. Only this invocation's selected PRXs enter the exported archive.

```sh
scripts/apply-patches.sh --only anyps5
python3 scripts/build-hle.py libSceJson2 libSceHttp2 libSceSsl --jobs 3
# On macOS/Linux add --wine /absolute/path/to/wine
```

The default output is `build/hle-runtime/hle-runtime.zip`. It contains selected
unpatched PRXs, the pinned compiler runtime DLLs and a manifest with hashes,
source/patch/toolchain provenance and test results. It contains no game data.

The helper runs the existing exception/runtime tests and the new JSON2 allocator,
offline API, filesystem and thread tests. Their required libraries are built as
test dependencies; only the explicitly selected distribution targets are exported.
`--skip-tests` leaves the archive explicitly untested. Passing host tests never
marks gameplay or the iPad runtime as verified.

Host contracts default to a lazy 4-GiB virtual arena at 8 GiB, in 256-MiB chunks.
Explicit `APS5_GUEST_ARENA_*` settings override it and enter build provenance.
This reserves virtual addresses on demand, not 4 GiB of physical RAM. It does not
change or qualify the iPad/game arena; reservation failures still fail the tests.

The Git Bash `m0-build-anyps5-winlibs.sh` route remains available for the existing
selected closure and Windows source-build transfer. Set `APS5_HLE_TESTS=ON` to run
its focused compatibility tests. See [API coverage](HLE-API-COVERAGE.md) and
[runtime performance](RUNTIME-PERFORMANCE.md) for the limits of these results.
