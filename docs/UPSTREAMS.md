# Pinned upstreams

Recorded 2026-10-08. `git submodule status` checks local checkouts. Dates below are the
commits we resolved while creating the pins, not a claim about later
upstream movement.

| Path | Remote | Commit | Branch |
| --- | --- | --- | --- |
| `upstreams/AnyPS5` | https://github.com/boykopovar/AnyPS5.git | `6e037e9899efb7439913a6e1151b901a92c9c9c8` | `main` |
| `upstreams/Madeira` | https://github.com/willfaust/Madeira.git | `48f976429c189f8396e23d251d8a82f43c705922` | `main` |
| `upstreams/FEX` | https://github.com/FEX-Emu/FEX.git | `14c92681f4d62cf84d901460e0358de09c8847a7` | `FEX-2610` tag |
| `upstreams/wine` | https://github.com/willfaust/wine.git | `257f271cfffed9f22f7987cac53bc00095d092fe` | `madeira-lgpl` |
| `upstreams/MoltenVK` | https://github.com/KhronosGroup/MoltenVK.git | `67b2682699d7903606b97a0392061d85d27d49e6` | `main` |

Madeira's tree also gitlinks, at its pinned commit:

| Gitlink | Commit | Vendored here? |
| --- | --- | --- |
| `FEX` | `3bec2ac498bf78156ab47c0c194b0e8cb2849756` | overridden by official FEX-2610 + local iOS port |
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
AnyPS5 update and reconciliation of the local patch series. The table records
heads observed at 09:34 UTC, plus the official monthly FEX release. Madeira
and Wine required no additional pin change. Later upstream movement is separate.
Installed game packages have separate provenance and do not change when this
source pin is updated.

## Checking for subsequent updates locally

```sh
python3 scripts/check-upstream-heads.py
python3 scripts/check-upstream-heads.py --json
```

The check compares indexed pins with live branch heads and the latest canonical
FEX monthly tag, and does not change
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

The [FEX-2610 integration record](evidence/fex-2610-integration-20261008.json)
separates source reconciliation, builds and bounded device checks. The FEX pin
now names the official release; [two reproducible patches](../patches/fex/README.md)
carry the iOS port and allocator adaptations. Monthly releases are always checked;
`--fex-release` also emits that result as a separate JSON field for older consumers.
The earlier [merge audit](evidence/fex-2610-ios-port-audit-20261008.json) remains a
historical pre-integration record.

The final same-day AnyPS5 pin is `6e037e98`: the standalone native tools pass
32 tests, all 33 patches match the reviewed source and reverse cleanly, and
interpolation/sanitizer checks pass. These source changes are not yet rebuilt
into the installed game's Windows HLE package.
