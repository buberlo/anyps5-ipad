# Patches

One series per upstream. `scripts/apply-patches.sh` applies every
`patches/<name>/*.patch` onto `upstreams/<matching-submodule>` in lexical
order. The series are regenerated from the submodule diffs; do not edit a
submodule by hand and forget to refresh the patch.

| Series | Files |
| --- | --- |
| `anyps5` | 22 patches: qualified lazy arena, Vulkan portability, dword-aligned serialized shader headers, shader validation, hardware shader-engine partition controls, Unity module paths, local/offline APIs, Windows descriptor and stack correctness, three swapchain images and opt-in pacing/cache counters. |
| `madeira` | 18 patches: static Vulkan/binder integration, memory repair, atomic write-fault classification and lifecycle with explicit menu activation, reported display timing, bounded diagnostics, quiet measurement, memory reporting, hardware audio period, and separate menu-app identity. |
| `fex` | Two default patches: rpmalloc POSIX logging and allocator declaration. Compile-block/CASPAL guards are now upstream. `experimental/` is excluded. |
| `wine` | Linux portability guards and bounded diagnostics, rebased on `257f271c`. |
| `moltenvk` | Four optional stability fixes (#2853, #2855, #2860, #2861). Default builds retain unmodified `fae55a18`; set `APS5_APPLY_MOLTENVK=1` on an isolated checkout to experiment. |

Status, including what was compiled and what was not: [../docs/PATCHES.md](../docs/PATCHES.md).

Each series is a derivative work of its upstream and is distributed under that upstream's license, not under the GPL-2.0-or-later grant in the repository [LICENSE](../LICENSE). `anyps5` stays GPL-2.0-only, `madeira` and `fex` stay GPL-3.0-or-later, and `wine` stays LGPL-2.1-or-later. `moltenvk` patches retain the upstream Apache-2.0 license. See [NOTICE](../NOTICE).
