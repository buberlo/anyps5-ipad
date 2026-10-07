# AnyPS5 upstream refresh, 2026-10-08

The source pin now uses
[`df16c4c256be3c44e03eb9149a6ce5f8e8a038b2`](https://github.com/boykopovar/AnyPS5/commit/df16c4c256be3c44e03eb9149a6ce5f8e8a038b2).
Compared with `ee391a5614246338aec9cb7a3a3dd4f479aec9f3`, this incorporates
all 285 additional commits, including merges, across 359 files: 13,333
insertions and 1,632 deletions. Only changes merged into upstream main are
included; open pull requests are not part of that snapshot.

| Component | Reviewed branch head | Result of live check |
| --- | --- | --- |
| AnyPS5 main | `df16c4c256be3c44e03eb9149a6ce5f8e8a038b2` | Full newer snapshot integrated |
| Madeira main | `48f976429c189f8396e23d251d8a82f43c705922` | Already current |
| FEX ios-port-2607 | `3bec2ac498bf78156ab47c0c194b0e8cb2849756` | Already current |
| Wine madeira-lgpl | `257f271cfffed9f22f7987cac53bc00095d092fe` | Already current |
| MoltenVK main | `fae55a18779ee59da2cc5373a367a0282779c171` | Already current |

Madeira's recorded DXMT and Dock gitlinks are retained as specified by its
current snapshot. Its Direct3D backend does not replace this app's Vulkan
path. The source checkout and local patch series are current at the check;
installed game packages retain their previous build provenance until rebuilt
and imported. No device result is implied by changing a source pin.

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

## Patch reconciliation and local checks

All 28 AnyPS5 patches were regenerated against the new pin. Forward application
of the complete series succeeds, all 62 touched files match the reviewed source
candidate, and reversing the complete series restores the pristine pin.
The root source checkout was updated with the same series; previous local
changes were preserved separately before replacing that checkout. Existing
Madeira, Wine and FEX patches were retained at their already-current pins.

The reconciliation keeps new writable-arena pins/shared-backing support and
upstream image-int64 atomic feature enumeration. Buffer-int64 capabilities
remain gated by actual support. Duplicate local/offline exports were reconciled
without discarding new exports or returning fabricated service success.
Upstream already accepts the shader-engine partition bit; the local patch now
retains its explanation and regressions instead of overwriting newer state
validation. Windows descriptor ownership and shader/depth fixes remain.

The native ARM64 relinker and NID patcher have been rebuilt in a separate build
directory. All 28 portable regression tests pass. The bundled Unity module-path
patch also updates the Linux alignment test's expected missing-module diagnostic;
its alignment and refusal checks remain enabled. Four macOS production contract
groups pass AddressSanitizer and UndefinedBehaviorSanitizer: shader alignment,
JSON, JSON2 allocator ownership, and offline-service/lifecycle contracts.

Build these portable tools and their regression suite locally with:

```sh
APS5_HOST_TOOLS_TESTS=ON scripts/build-host-relinker.sh
python3 scripts/test-hle-compatibility.py
```

The fresh WinLibs GCC15.2 posix-SEH/UCRT Release build succeeds for all 50
previously distributed PRX implementations, the Windows relinker/NID patcher,
shader replay tool and 24 test targets. Old objects are not reused. Timing and
AGC create logs remain disabled. An unpatched HLE archive is exported locally
with source/patch hashes and the explicit failed-test status; it is not a
qualified device runtime.

Of 25 linked Windows test invocations under local macOS Wine, 22 pass. JSON,
allocator ownership, filesystem, priority, math, existing exception contracts,
uniform/wide-subgroup shader emission, depth/stencil eligibility, AJM and the
six AudioOut/AudioOut2 contracts pass. Three outcomes remain open:

- The new `guest_raise_exception` test aborts after reaching its leaving-wait
  race phase. An unchanged repeat hangs in the host-blocked phase and is stopped
  after exceeding upstream's 30-second test limit. The suspend/context delivery
  path is not qualified for Wine, FEX or the iPad by these results.
- `guest_memory` fails the Windows working-set minimum/maximum assertion in
  `CheckMlock` under macOS Wine. This does not establish native Windows behavior
  or qualify locking on the iPad.
- The full AGC graphics suite still rejects a fixture's fixed-function
  interpolation at pc=0, as the previous pinned build did. Its separate
  depth/stencil subset passes. The failing fixture is not skipped or rewritten
  to manufacture a complete-suite success.

All 14 complete private shader captures recompile with the updated compiler
and pass external `spirv-val --target-env vulkan1.2`. The remaining capture lacks
required descriptor data and is excluded from success. Raw capture code remains
local. No displayed frame or audible output is tested by shader replay.
A new private Solitaire package is prepared from the immutable original dump
using the new native relinker/NID patcher and rebuilt HLE archive. Its 47 explicit
HLE dependencies resolve and its complete manifest inventory is hash-verified.
The first preparation attempt used a local normalized directory containing
duplicate module aliases and was refused; it did not replace an installation.
The successful package is `prepared_unexecuted`, with no Windows GPU or iPad
result. Dumps, converted game binaries and raw shader captures are outside Git.

See the [structured integration evidence](evidence/upstream-refresh-20261008.json).

## Runtime qualification after integration

Source integration, local builds and host contracts do not qualify the iPad
execution path. Prepare and hash-check a separate private game candidate with
the new relinker, NID patcher and HLE libraries. Test upstream signal delivery
on the iPad before enabling it for the game; retain the original dump,
production runtime and saves as rollback inputs.

Under the shared iPad lease, compare the new package with the recorded baseline.
Capture CPU/exception, shader, audio and rendering outcomes separately. Restore
the production installation after failed diagnostics. White output, audible
distorted audio or a valid swapchain do not establish playable gameplay.

Native M2 attachment capability queries through the current Wine/FEX/MoltenVK
route report only 1, 2 and 4 samples. Solitaire's captured draw state requests
8 samples. The candidate upstream still rejects multisampling; this hardware
and renderer gap remains open even if its CPU/signal changes fix another blocker.
See [device capability evidence](evidence/solitaire-ipad-msaa-capabilities.json).
