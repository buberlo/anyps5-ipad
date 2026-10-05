# Patches

One series per upstream. `scripts/apply-patches.sh` applies every
`patches/<name>/*.patch` onto `upstreams/<matching-submodule>` in lexical
order. The series are regenerated from the submodule diffs; do not edit a
submodule by hand and forget to refresh the patch.

| Series | Files |
| --- | --- |
| `anyps5` | GuestArena size/base/lazy reserve; MoltenVK portability bits; `GetThreadDescription` prototype for mingw-w64 11; `APS5_SLIM` Vulkan loader without SDL |
| `madeira` | FEXBridge AVX switch; extended-virtual-addressing; winevulkan + MoltenVK |
| `fex` | rpmalloc POSIX `write` log; declare `InitializeAllocator`; keep iOS compile-block logs under `FEX_IOS_HOST`; `VirtualQuery` only on Windows |
| `wine` | empty. The iOS unix side forces `SONAME_LIBVULKAN` without a Wine change. |
| `moltenvk` | empty. No MoltenVK source change in this round. |

Status, including what was compiled and what was not: [../docs/PATCHES.md](../docs/PATCHES.md).
