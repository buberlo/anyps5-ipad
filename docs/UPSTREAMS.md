# Pinned upstreams

Recorded 2026-10-07. `git submodule status` is the check. Dates below are the
commits we resolved while creating the pins, not a claim about later
upstream movement.

| Path | Remote | Commit | Branch |
| --- | --- | --- | --- |
| `upstreams/AnyPS5` | https://github.com/boykopovar/AnyPS5.git | `ee391a5614246338aec9cb7a3a3dd4f479aec9f3` | `main` |
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

The [2026-10-08 AnyPS5 refresh audit](UPSTREAM-REFRESH.md) tracks a newer
merged-main candidate. Its integration and runtime checks are incomplete; the
production pin above remains authoritative.
