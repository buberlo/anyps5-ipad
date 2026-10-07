# AnyPS5 upstream refresh, 2026-10-08

The installed app and production pin still use `ee391a5614246338aec9cb7a3a3dd4f479aec9f3`.
The candidate upstream snapshot is
[`df16c4c256be3c44e03eb9149a6ce5f8e8a038b2`](https://github.com/boykopovar/AnyPS5/commit/df16c4c256be3c44e03eb9149a6ce5f8e8a038b2).
It contains 285 additional commits, including merge commits, and changes 359
files: 13,333 insertions and 1,632 deletions. Only changes merged into upstream
main are included; open pull requests are not treated as part of that snapshot.

## Why update before further game-specific repairs

The new Windows `sceKernelRaiseException` implementation suspends a running
thread, obtains its context, redirects delivery below the SysV red zone and
restores the context afterwards. Alertable waits have a separate queued-delivery
path. This replaces the old missing implementation with a concrete upstream
approach. Whether Wine/FEX on iPad preserves all required guest state through
that approach still needs independent target-thread, context, red-zone and game
tests. A failed special-user-APC preemption probe on the existing app does not
establish the outcome of this different suspend/context route.

Other changes include fixed no-overwrite memory mapping checks, writable fiber
contexts under Windows write tracking, scalar/relative register fixes, additional
texture swizzle and sampling paths, DCC first-write keys, resource lifetime
repairs, AJM resampling and AudioOut2 timing/lifetime changes. Linux dma-buf
imports remain Linux-specific; they do not provide iPad zero-copy memory.
The actual Solitaire output uses the older AudioOut path, so AudioOut2 updates
are not by themselves evidence that the reported distorted audio is fixed.

## Initial compatibility audit

The snapshot has been fetched into a separate source worktree. The current
28-patch series was checked without changing the production sources or device:

| Initial result | Patches |
| --- | --- |
| Apply directly | 18 |
| Three-way application succeeds; review required | 2 |
| Manual reconciliation required | 8 |

Conflicts cover lazy arena reservation, Vulkan portability, offline/local HLE
exports, Windows descriptor lifetime, shader-engine partition state, equivalent
stencil clears/Z ordering and the unobserved viewport-depth tests. Several are
upstream overlap rather than missing functionality. For example, upstream now
already accepts the shader-engine partition bit. Reapplying an older version of
that file would overwrite new sample-mask/depth-export handling.

The candidate currently contains the automatically applicable patches. The
lazy-arena include conflict has been reconciled while retaining new upstream
writable-pin/shared-backing support. Vulkan feature enumeration retains upstream
image-int64 atomics and the existing feature-gated buffer-int64 capability.
Remaining manual conflicts are not resolved yet. This is a preparation result,
not a build, install or runtime result.

## Integration order and acceptance

1. Reconcile or retire every overlapping patch, retaining the high-address lazy
   arena, iPad Vulkan feature gates, three-image swapchain, controls and lifecycle
   behavior. No validation guard is deleted to make the build or game appear to
   pass.
2. Regenerate the remaining patch series against the exact candidate snapshot.
   Check forward application, patched source equality and complete reversal in
   disposable copies. Update the gitlink and pin documents only for the reviewed
   final series.
3. Build the relinker, NID patcher and complete required HLE set locally. Recheck
   exports/imports and the same synthetic CPU, write-watch, exception, shader and
   renderer fixtures. No GitHub Actions are used.
4. Prepare and hash-check a separate private Solitaire candidate using the new
   tools and libraries. Test the upstream signal implementation before enabling
   it for the game. Keep original dumps, production runtime and saves intact.
5. Under the shared iPad lease, compare the new candidate with the recorded
   baseline. Restore the production installation after failed diagnostic tests.
   White output, audible distorted audio or a valid swapchain are not acceptance.

Native M2 attachment capability queries through the current Wine/FEX/MoltenVK
route report only 1, 2 and 4 samples. Solitaire's captured draw state requests
8 samples. The candidate upstream still rejects multisampling; this hardware
and renderer gap remains open even if its CPU/signal changes fix another blocker.
See [device capability evidence](evidence/solitaire-ipad-msaa-capabilities.json).
