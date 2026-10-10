# Apple upstream integration, 2026-10-09

This is the historical qualification of the targeted backport on `6e037e98`.
It has been superseded in source by the [full main integration](ANYPS5-MAIN-INTEGRATION.md).
The artifacts and results below belong to the earlier build and are preserved;
they do not qualify the newer source or an installed game package.

This update adopts the useful exception and shader/descriptor changes identified
in the [upstream review](APPLE-UPSTREAM-REVIEW.md). It retains the existing
Windows HLE → Wine/FEX → Vulkan → MoltenVK path and the previous MiniGolf and
Solitaire repairs. All builds and tests are local; no GitHub Actions are used.

Local qualification is complete: 47 HLE libraries were built, all 36 selected
host test programs passed, and the explicit save-memory read-failure CTest
passed without a skip. The native ARM64 iPhoneOS app builds and links with the
patched MoltenVK. Its embedded ARM64EC components pass provenance checks; all
four corrected MSL helper emitters are present in the linked executable.

This app is **unsigned and not installed**. Its source bundle version is 42;
that is build metadata for this local qualification, not a new device milestone.
A deployment needs signing and a suitable version number. The updated HLE
package also needs fresh matching relinker/NID tools when preparing a private
title package.

## Adopted changes

| Source | Local implementation | Qualification scope |
|---|---|---|
| [AnyPS5 #927](https://github.com/boykopovar/AnyPS5/pull/927) | Patch0073 exports the guest C unwind personality with SysV ABI and keeps a separate Microsoft-ABI host wrapper. Unwind dispatch recognizes both. | Actual Windows libc/libkernel build, three exception CTests and calls into the real NID-patched library. |
| [AnyPS5 #1571](https://github.com/boykopovar/AnyPS5/pull/1571) | Patch0074 clamps the shader IR result at the smallest normal float before applying the existing underflow, zero and NaN rules. | Twelve native production-branch fixtures with ASan/UBSan; removing the clamp fails two boundary fixtures. These are CPU tests of IR generation, not iPad shader execution. |
| [AnyPS5 #1580](https://github.com/boykopovar/AnyPS5/pull/1580) | Patch0075 admits storage-buffer layouts above ordinary descriptor limits only when the actual feature and update-after-bind limits allow them. | Production graphics host validator, descriptor allocation/resource tests, eight feature-policy cases and the existing depth-state subset. Vulkan device compilation is included in the full HLE build. |
| [SPIRV-Cross #2704](https://github.com/KhronosGroup/SPIRV-Cross/pull/2704) | The separate SPIRV-Cross patch preserves negative zero in precise scalar/vector multiplication and matrix accumulation. | Actual MoltenVK SPIRV-Cross output executed through Metal on an Apple M3 Pro: 40 exact float/half results passed; the positive-zero mutation fails all 24 negative-zero channels. |

The exception change corrects an ABI mismatch in the old Windows package: after
NID patching, `FW2QeoYK7-c` referred to the Microsoft-ABI host function, but a PS5
caller uses SysV. A valid synthetic cleanup call returns
`_URC_FATAL_PHASE1_ERROR` through that old library and succeeds through the new
library. PE export addresses confirm that the new NID targets the guest
function while the host wrapper remains available by its original name. The
macOS-only libc++/Mach-O exception implementation is not part of this backport.

Descriptor update-after-bind remains conditional. The app retains
`robustBufferAccess`; it disables this descriptor feature when the device does
not support its combination with robust access. Ordinary and update-after-bind
layout cache keys and pool chains are distinct. Mixed sampler/image/buffer
layouts must meet the applicable per-stage and per-set limits. Draw binding
snapshots, pool ownership and dirty tracking remain intact. Vulkan allocation
failure still fails explicitly; no feature or capacity is fabricated.

## Latest FEX check

The official `main` and the latest release, **FEX-2610**, both resolve to
`14c92681f4d62cf84d901460e0358de09c8847a7` on this date. That is already the
project's integrated FEX base. There are no later main commits to import. The
existing iOS/ARM64EC/AVX and exception patches remain applied.

`scripts/check-upstream-heads.py` now checks official FEX `main` separately from
the monthly release. A later change on main will therefore be visible even
before the next release. A pin that already contains the release is reported as
`release_included`, while main's freshness is checked independently. The
configured Madeira FEX branch is unchanged; its
alternative `ios-port` branch is older and is not used as an update.

Dependency pins remain AnyPS5
`6e037e9899efb7439913a6e1151b901a92c9c9c8`, Madeira
`48f976429c189f8396e23d251d8a82f43c705922`, Wine
`257f271cfffed9f22f7987cac53bc00095d092fe`, MoltenVK
`67b2682699d7903606b97a0392061d85d27d49e6` and SPIRV-Cross
`aa217aeb6c9f0ace7a0ab233b28807edf45eb165`. This is a targeted backport; it does
not claim that the whole AnyPS5 main branch has been integrated.

## Reproducible packages

The SPIRV-Cross patch is applied **before** building MoltenVK's external
dependencies. `fetchDependencies` force-checks out dependency files, so running
it after patching would erase the fix. See the [README build sequence](../README.md).
Use `scripts/build-moltenvk-local.sh ios` for an initialized checkout.

The wrapper records source pins, patch/emitter hashes and the resulting iOS
archive. Before the app links it, `scripts/check-moltenvk-package.py` checks the
successful iOS receipt, current source identity and archive digest/size. It also
finds all four corrected MSL helper emitters in the **compiled archive**. The
producer selects the native static slice from XCFramework metadata, and the
guard requires exactly `ios-arm64` without a simulator variant. Twelve
positive/stale-input controls exercise these checks. Packaging errors fail the
wrapper even when a subsequent packaging command succeeds. A patched checkout alone
does not prove that the app contains a patched MoltenVK binary.

The full HLE build includes the new exception personality and graphics validator
targets. Its archive contains HLE libraries and compiler runtime DLLs, with
source/patch/toolchain hashes and host test results; it contains no game files.
Incremental compilation uses the existing local build tree. Each selected PRX
is taken from the resulting build, not copied from the previous distribution.
The final HLE libc was separately copied, NID-patched with the freshly built
tool and called through the guest SysV export; that actual distribution binary
also passes the cleanup/dispatch regression.

The complete ordered patch check applies patches to pristine pinned files,
compares all touched files byte-for-byte and verifies executable modes against
the actual checkout, then reverses back to pristine content. Its explicit
checks remain effective with `python -O`; nine isolated controls cover byte/mode
drift, missing patch detection and the FEX release/main states. Three older
patches (0069, 0070 and 0072) needed Git
diff/new-file metadata normalization. Their source hunks are unchanged. Old
private build receipts remain historical records and are not rewritten.

## Deliberately excluded

- AnyPS5 #1565 only adjusts a test that does not exist in this pin. It supplies
  no missing vertex-subgroup implementation or game optimization.
- Madeira #227 advertises overlapping reservation tails. It neither matches
  the arena's allocation predicates nor preserves safe ownership for sparse
  commits. Real nonoverlapping lazy reservations remain in place.
- The native ARM64 HLE/FEXCore runner and #1835 AOT compiler remain separate
  research routes. Their missing guest signal/exception and module contracts
  cannot replace the current Unity/IL2CPP runtime.

These are compatibility and build correctness improvements. A host test, a Mac
Metal result or an iPhoneOS build does not establish iPad GPU compatibility,
MiniGolf gameplay or a frame-rate gain. The installed iPad app and private game
packages were not changed during this integration. The outstanding MiniGolf
device retry remains documented in [MINIGOLF.md](MINIGOLF.md).

Exact build/test/artifact status is recorded in
[integration evidence](evidence/apple-upstream-integration-20261009.json).
