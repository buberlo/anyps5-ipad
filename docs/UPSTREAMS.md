# Pinned upstreams

Recorded 2026-10-05. `git submodule status` is the check. Dates below are the
commits we resolved while creating the pins, not a claim about later
upstream movement.

| Path | Remote | Commit | Branch |
| --- | --- | --- | --- |
| `upstreams/AnyPS5` | https://github.com/boykopovar/AnyPS5.git | `0518f0e02187b6c7c00e6f7e7a7265c848efb346` | `main` |
| `upstreams/Madeira` | https://github.com/willfaust/Madeira.git | `bbbf8d0e20fd8b75f433f4a8d2a8eaf8d5571120` | `main` |
| `upstreams/FEX` | https://github.com/willfaust/FEX.git | `be778d7ba5b98bee0405fd1d6a50d305a0807df0` | `ios-port-2607` |
| `upstreams/wine` | https://github.com/willfaust/wine.git | `3a54f56896c85c932870afe6fd91404bbbcb74c8` | `madeira-lgpl` |
| `upstreams/MoltenVK` | https://github.com/KhronosGroup/MoltenVK.git | `fae55a18779ee59da2cc5373a367a0282779c171` | `main` |

Madeira's tree also gitlinks, at its pinned commit:

| Gitlink | Commit | Vendored here? |
| --- | --- | --- |
| `FEX` | `be778d7ba5b98bee0405fd1d6a50d305a0807df0` | yes, `upstreams/FEX` |
| `wine` | `3a54f56896c85c932870afe6fd91404bbbcb74c8` | yes, `upstreams/wine` |
| `dxmt` | `8937c08c38f5cb867d995f99b2323739a9f6ffdf` | no |
| `madeira-dock` | `3cadfbea700e4da4b04e331dd7ef1ba633dfacef` | no |

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
