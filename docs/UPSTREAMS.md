# Pinned upstreams

Recorded 2026-10-08. `git submodule status` checks local checkouts. Dates below are the
commits we resolved while creating the pins, not a claim about later
upstream movement.

| Path | Remote | Commit | Branch |
| --- | --- | --- | --- |
| `upstreams/AnyPS5` | https://github.com/boykopovar/AnyPS5.git | `8388121a4f2e2c85fc94191003a37b20c9f076fa` | `main` |
| `upstreams/Madeira` | https://github.com/willfaust/Madeira.git | `48f976429c189f8396e23d251d8a82f43c705922` | `main` |
| `upstreams/FEX` | https://github.com/willfaust/FEX.git | `3bec2ac498bf78156ab47c0c194b0e8cb2849756` | `ios-port-2607` |
| `upstreams/wine` | https://github.com/willfaust/wine.git | `257f271cfffed9f22f7987cac53bc00095d092fe` | `madeira-lgpl` |
| `upstreams/MoltenVK` | https://github.com/KhronosGroup/MoltenVK.git | `fae55a18779ee59da2cc5373a367a0282779c171` | `main` |

Madeira's tree also gitlinks, at its pinned commit:

| Gitlink | Commit | Vendored here? |
| --- | --- | --- |
| `FEX` | `3bec2ac498bf78156ab47c0c194b0e8cb2849756` | yes, `upstreams/FEX` |
| `wine` | `257f271cfffed9f22f7987cac53bc00095d092fe` | yes, `upstreams/wine` |
| `dxmt` | `db546ee466ad50ec6463edb5fa183de11db43761` | no |
| `madeira-dock` | `72558e416a390f3669457930f09a389e54f49556` | no |

`scripts/link-madeira-siblings.sh` points `upstreams/Madeira/FEX` and
`upstreams/Madeira/wine` at the sibling checkouts when those directories are
empty. DXMT stays out until a build needs the D3D path.

AnyPS5's own nested submodules (SDL, Vulkan-Headers, glslang, SPIRV-Tools,
VulkanMemoryAllocator, FreeType, FFmpeg, stb, LibAtrac9) are not pinned in
the parent. `scripts/m0-build-anyps5.sh` runs
`git submodule update --init --depth 1` inside AnyPS5, which fetches the
commits AnyPS5 itself recorded. Vulkan-Headers inside that pin is
`2fa203425eb4af9dfc6b03f97ef72b0b5bcb8350`.

Clones are shallow (`--depth 1`) except where a submodule add had already
fetched a branch tip. The gitlink is the full commit id either way.

The [2026-10-08 refresh report](UPSTREAM-REFRESH.md) documents the complete
AnyPS5 update and reconciliation of the local patch series. Madeira, Wine, FEX
and MoltenVK already match their live configured branches at this check.
Installed game packages have separate provenance and do not change when this
source pin is updated.

## Checking for subsequent updates locally

```sh
python3 scripts/check-upstream-heads.py
python3 scripts/check-upstream-heads.py --json
```

The check compares indexed pins with live branch heads and does not change
sources or execute builds. Exit 0 means all five matched at the reported time,
exit 1 means newer commits require integration, and exit 2 means at least one
query failed. A failed network query is never reported as current.
Run it before upstream refreshes. For updates, fetch the full merged branch
snapshot, reconcile the entire local patch series, verify clean application and
reversal, rebuild and run the affected local tests, then update the reviewed
pin. Preserve historical device evidence and qualify newly built packages
separately. Do not use floating branches as build inputs. No GitHub Actions are
used; this command does not schedule automatic runs.

The same-day [follow-up snapshot](evidence/anyps5-followup-20261008.json)
adds all 200 subsequently merged AnyPS5 commits at `8388121a`. All 33 local
patches were reconciled, including the newer HTTP2 completion routing, NP
callback registration errors, UTF16 helpers, graphics fixtures and host-blocked
exception synchronization. The ARM64 host tools pass 30 tests, the production
interpolation guard passes 111 cases, and four sanitizer contract groups pass.
The new Windows HLE snapshot and actual game still require rebuilding and
qualification.

Use `python3 scripts/check-upstream-heads.py --fex-release` to additionally
compare the canonical monthly FEX release with the iOS fork. A current fork
branch alone does not imply that a newer mainline release is integrated.
The [FEX-2610 audit](evidence/fex-2610-ios-port-audit-20261008.json) records the
initial divergence and merge conflicts; it is source analysis, not a device
runtime result.
