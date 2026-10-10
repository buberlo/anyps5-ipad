# Local qualified HLE builds

Use `scripts/build-hle.py` on Windows, or on macOS/Linux with a local Wine capable
of running the pinned Windows compiler. It verifies the WinLibs GCC 15.2 posix-SEH
UCRT r7, FFmpeg and embedded Python download hashes. CMake, Ninja, curl and an
archive extractor must be installed locally. No GitHub Actions are used.

Apply the AnyPS5 patches before invoking this helper. An omitted target list builds
all available PRX targets; explicit targets must exist and retain libc/libkernel.
The object-manager target is named `ulobjmgr`; this exact name is accepted alongside
the existing `lib*` names. Every selected target must still have a matching source
directory. Manifest paths and shell characters are rejected before configuration.
`--from-hle` reads a prior archive's manifest to select names, without reusing old
binaries. `--source /absolute/path/to/patched/AnyPS5` selects a separate checkout
for a pin upgrade; use its matching host tools when preparing the package. Only this invocation's selected PRXs enter the exported archive.

```sh
scripts/apply-patches.sh --only anyps5
python3 scripts/build-hle.py libSceJson2 libSceHttp2 libSceSsl --jobs 3
# On macOS/Linux add --wine /absolute/path/to/wine
```

The default output is `build/hle-runtime/hle-runtime.zip`. It contains selected
unpatched PRXs, the pinned compiler runtime DLLs and a manifest with hashes,
source/patch/toolchain provenance and test results. It contains no game data.

The helper runs the existing exception/runtime tests and the JSON2 RTTI/virtual allocator,
offline API, filesystem and thread tests. The upstream refresh also adds busy-thread
exception delivery, memory, uniform/wide-subgroup shader emission and AudioOut2
mix/latency/layout/timing contracts, each with a bounded execution time. These are
host contracts and do not demonstrate audible output or physical GPU rendering.
The selected test set also includes the existing HMD-without-device,
player-review-dialog and object-manager contracts. Patch0066 registers the HMD
test with the ordinary library build; it does not introduce a replacement HMD
implementation or emulate connected hardware or PSN access.
Their required libraries are built as
test dependencies; only the explicitly selected distribution targets are exported.
`--skip-tests` leaves the archive explicitly untested. Passing host tests never
marks gameplay or the iPad runtime as verified.

The historical three-patch full-main qualification passes 40 selected host
targets. The current six-patch combined build also includes exception-output and
unused-barycentric proof tests: all 42 selected and ten additional targets pass,
producing 53 distinct cases. One internal AVX subcase remains skipped on local
Wine; there are no CTest skips or failures. Before/after hashes cover actual test
executables, PRXs and compiler runtime DLLs; changed inputs fail qualification.
The final export seals the immutable qualification and verifies embedded/outer
provenance without circular hashes. [Current build evidence](evidence/anyps5-combined-runtime-20261010.json).

These tests include the actual C unwind personality and complete AGC graphics validator. The
personality test distinguishes SysV guest calls from Microsoft-ABI host calls;
the graphics validator exercises conditional descriptor update-after-bind,
ordinary-layout parity, pool ownership and mixed resource limits. These graphics
tests mock Vulkan calls and do not qualify execution on a physical GPU. See
[full main integration](ANYPS5-MAIN-INTEGRATION.md) and the historical
[Apple integration](APPLE-UPSTREAM-INTEGRATION.md) for separate native Metal
shader results and artifact provenance.

Host contracts default to a lazy 4-GiB virtual arena at 8 GiB, in 256-MiB chunks.
Explicit `APS5_GUEST_ARENA_*` settings override it and enter build provenance.
This reserves virtual addresses on demand, not 4 GiB of physical RAM. It does not
change or qualify the iPad/game arena; reservation failures still fail the tests.
If a local Wine host cannot reserve that fixed range, preserve the failure and
qualify an explicit alternative with the unchanged memory test before running
the complete suite. For the MiniGolf build, the same memory-test binary failed
at `0x200000000` and passed at `0x7400000000`, with only the base changed. This
establishes address-dependent host availability; it does not identify the
underlying Wine/Rosetta mapping or prove a device result.

The Git Bash `m0-build-anyps5-winlibs.sh` route remains available for the existing
selected closure and Windows source-build transfer. Set `APS5_HLE_TESTS=ON` to run
its focused compatibility tests, including the complete AGC graphics validator
and 20 repeated exception-delivery runs. A failure or timeout fails the build.
See [API coverage](HLE-API-COVERAGE.md) and
[runtime performance](RUNTIME-PERFORMANCE.md) for the limits of these results.
