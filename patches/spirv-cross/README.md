# MoltenVK SPIRV-Cross patches

This is the fetched dependency at `upstreams/MoltenVK/External/SPIRV-Cross`,
not AnyPS5's SPIRV-Cross copy. Its upstream commit stays at the revision in
MoltenVK's `ExternalRevisions/SPIRV-Cross_repo_revision`:
`aa217aeb6c9f0ace7a0ab233b28807edf45eb165`.

`0001-precise-multiply-negative-zero.patch` backports
[SPIRV-Cross #2704](https://github.com/KhronosGroup/SPIRV-Cross/pull/2704),
by Zheorgyan, head `51648cae36ebcb24442333ea3dd074b0c4ea66ae`.
The PR was open when reviewed on 2026-10-09. The patch includes the four
production emitter changes and all six upstream reference-output updates.
It uses a negative-zero FMA addend for precise multiplication and initializes
matrix accumulation with negative zero; the overwritten temporary stays zero.

```sh
scripts/apply-patches.sh --only spirv-cross
scripts/check-precise-negative-zero.py --metal
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  APS5_BUILD_JOBS=2 scripts/build-moltenvk-local.sh ios
```

The regression translates an original SPIR-V fixture through the actual
`MVK_spirv_cross::CompilerMSL` library, validates its `NoContraction`
decorations and emitted helpers, and optionally runs that emitted MSL on the
Mac's Metal GPU. Forty bit-exact float/half scalar, vector and matrix channels
include positive-zero and finite controls. A generated-MSL mutation restores
positive-zero FMA addends and must fail exactly the 24 negative-zero channels.
Fast math is disabled in both GPU runs. Metal device unavailability is a test
failure, never an accepted skip. Native Mac validation does not qualify the
physical iPad or game rendering.

Dependencies must already be initialized. The local build wrapper checks their
pins, applies this series before compilation, builds and packages the real
SPIRV-Cross external library, then rebuilds MoltenVK for the selected platform.
It performs no downloads or forced checkouts. Upstream `fetchDependencies`
uses `git checkout --force`; do not run it over locally patched dependencies.
On a fresh, unmodified checkout, fetch dependencies with `--none` first, then
use this wrapper. Never build externals before applying the series.

Applying the patch changes source only. The iPad receives it only after a fresh
iPhoneOS MoltenVK archive is linked into a rebuilt native app and installed.
Updating Windows HLE DLLs or importing a game cannot replace the statically
linked translator. The wrapper writes archive hashes to
`build/moltenvk-local/<platform>/receipt.json`; this marks a local build only.

All checks and builds are local; no GitHub Actions are used.
