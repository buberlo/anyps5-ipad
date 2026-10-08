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

In the initial integration check, 22 of 25 linked Windows test invocations under
local macOS Wine passed. The follow-up repairs below supersede those three failed
host results while retaining their original evidence. JSON,
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

## Exception, memory and interpolation follow-up

Patch `0029-safe-wait-delivery-and-validation.patch` adds repairs after the
initial integration, without changing the upstream pin or rewriting its failed
result record.

Exception delivery now reserves a queued APC in the same atomic state that
tracks active waits. Wait exit keeps that state active through its last APC drain
and closes it with compare/exchange; a concurrent reservation forces another
drain. Failed enqueue releases the reservation before resuming the target.
The semaphore wait also covers native setup and cleanup, with final delivery
after its internal locks and waiter bookkeeping are released. Temporary host
tracing exposed a native-host instruction/stack context during a failed redirect;
that region now uses cooperative APC delivery rather than context redirection.
The idle path checks for a newly entered wait after obtaining the context,
because [SuspendThread completes asynchronously](https://devblogs.microsoft.com/oldnewthing/20150205-00/?p=44743).
Delivery preserves the existing context/red-zone route for running guest code.
The exception test's blocked-host handshake also waits for the worker to finish
its interrupted mutex acquisition before starting another round. All busy,
waiting, host-blocked and wait-exit assertions remain enabled.

The memory failure came from host-specific assumptions in the test. Wine's
`ProcessQuotaLimits` query returns fixed defaults, while its locking uses actual
host `mlock`/`munlock`; Windows working-set quota growth and repeated-unlock
errors are checked only on native Windows. Guest lock success/error checks
remain active under Wine. New coverage rejects a span with an inaccessible
middle page and verifies every guest page after protection is restored. Arena-end
mapping checks now use the configured arena's actual bounds, including overflow
and out-of-range refusal, instead of assuming the default full-size arena.
No lock implementation is replaced with synthetic success.

The interpolation validator now treats EXP sources as independent scalar VGPRs
and reads only enabled channels. Compressed XY/ZW exports use their actual two
packed operands. Disabled channel encodings no longer reject otherwise valid
depth exports. Enabled raw I/J reads, unfinished interpolation pairs and unknown
operations still fail the conservative guard. The full graphics fixture is
unchanged; 13 new guard cases cover these boundaries.

See the [follow-up results](evidence/runtime-contract-fixes-20261008.json) for
build, repeated delivery, full host-suite and private shader-replay outcomes.
The Git Bash HLE test route now includes the full graphics suite and repeats the
complete exception test 20 times, failing on an error or timeout. Native Windows
quota behavior and physical iPad execution remain separate qualification steps.

## Physical iPad contract follow-up

The current `guest_memory_tests.exe`, with the repaired HLE libraries, completes
on the M2 iPad with exit code 0. This covers the full guest memory test, including
configured arena bounds, inaccessible middle-page lock rejection and protection
restoration. It does not qualify Solitaire rendering or audio.

Before the native signal repair below, the complete exception test failed on
the device: self-delivery succeeded,
but its first busy-thread delivery expected a second handler call and saw only
one. Wine's native context snapshot/cache path did not establish delivery of the
modified x86 context to the running FEX thread.

A private, default-off native Wine/Madeira prototype tried FEX's existing suspend
doorbell and target-side context publication. Three bounded runs did not complete
the pending context. The diagnostic run verifies the doorbell write succeeds;
no suspend trap is observed. Disabling multiblock compilation for one diagnostic
run does not resolve this. The precise reason for the missing trap remains open.
FEX source was not changed, and the failed prototype was not promoted into the
public patch stack. Build 28, native source inputs and staged archives were
restored; the device library and configuration remain byte-identical.

Interpolation guards and private shader replay pass locally. A rendered iPad
draw with these updated libraries remains unqualified. See the
[device result record](evidence/ipad-contract-followup-20261008.json), which keeps
memory success separate from incomplete exception and graphics qualification.

## Target-published x64 suspension: Build 35

The native Wine/Madeira path now offers `APS5_X64_SIGNAL_SUSPEND=1`, set before
Wine initialization. It is default-off; it is not a blanket replacement for
Wine's WoW64 or other-process suspension. `APS5_X64_SIGNAL_TRACE=1` enables its
per-delivery diagnostics separately. No new FEX source modification is used.

Wine's ordinary Mach signal sender cannot address these same-task Wine
pseudo-processes through its absent process port. The new path resolves the
initialized AMD64 target's Mach thread to a native pthread and sends SIGUSR1.
The server creates a pending context; the target itself publishes it through
`wait_suspend`, so Get/Set/Resume use actual target state rather than a cached
native snapshot.

For translated code, the existing FEX exception reconstruction produces the
interrupted x64 state. Native ntdll consumes only an armed private suspend
exception and passes that state to Wine's suspension handshake. Native ARM64EC
pool aliases are resolved before checking the EC bitmap: the simulation flag
alone is insufficient during exception dispatch. A redirected x64 PC enters the
emulation dispatcher; an unchanged native context remains native. The handoff
preserves the SysV red zone.

Native waits need an additional correction. Their suspended context contains
arguments before the syscall result exists. The dispatcher records the actual
result when the original syscall finishes; NtContinue restores it only to the
matching original PC/SP. A synthetic success value would break timeouts. The
extended guest test checks ten actual event timeouts and ten signaled waits,
as well as all original busy, semaphore, mutex, wait-exit and finished-target
checks.

Three complete 421-delivery runs pass on the M2 iPad with Build 34. The extended
441-delivery contract passes on Builds 34 and 35, including preservation of real
`STATUS_TIMEOUT` (`0x102`) and success results. The full guest-memory test also
passes on Build 35 with this path enabled. Local repeated host results and source
hashes are in the [Build 35 record](evidence/ipad-target-published-suspend-20261008.json).

A bounded Solitaire run with the updated HLE libraries, native suspension and
normal memory tracking reaches the game entry and a Vulkan device, then faults
in `UnityGfxDeviceWorker` on a read of `0x30`. This does not establish playable
output, device interpolation correctness or audio quality. The actual pre-test
runtime hashes, manifest, configuration and library are restored. Build 35 stays
installed with the new native path default-off. Further game qualification is
required before enabling it as a general default.

## Asynchronous AVX state follow-up

A separate synthetic busy-thread test exposed lost upper YMM halves after a
handler executed `vzeroall`. The original four-register case lost eight of its
16 uint64 lanes on both the local Wine host and Build 35. The legacy Windows
`CONTEXT` carries FXSAVE/XMM state but needs an extended state path for AVX;
see Microsoft's [XState context interface](https://learn.microsoft.com/en-us/windows/win32/debug/working-with-xstate-context).
On this iPad runtime, `GetEnabledXStateFeatures` reports zero even though FEX
advertises and executes AVX. The HLE follow-up therefore preserves live guest
upper halves at the first assembly instruction of the redirected entry.

Patch 0031 uses `APS5_PRESERVE_ASYNC_AVX=1` and a CPU feature gate, default-off.
It initializes GCC's feature detector, resolves `NtContinue` before suspension,
uses the guest context's 832-byte floating-state area for low/high registers,
and copies handler changes back. Clearing the AVX state bit initializes only
the upper halves. An assembly tail call restores all upper halves immediately
before the existing native continuation, so neither C++ nor a `vzeroupper` can
intervene. Self-delivery and cooperative APC calls keep their existing ABI call
semantics; this patch qualifies redirected busy-thread state rather than every
possible nested native interruption. FEX sources are unchanged.

The physical iPad passes 543 deliveries: the original extended 441 plus 100
preservation, one handler-edit and one AVX-reset delivery. Every vector stage
checks all 16 YMM registers, including the saved handler context and restored
values. The local host passes its 441 cases but does not advertise AVX; its
extended vector checks explicitly skip. All 31 AnyPS5 patches apply from the
pinned source, match the root checkout and reverse to the pristine source.

A freshly prepared private game candidate resolves all 47 explicit HLE
requirements. Its 60-second iPad test uses normal memory tracking and both
opt-in exception paths. All three screenshots remain white. A multisampling
draw is rejected, and the graphics worker later faults reading `0x2` at game
RVA `0x1426ac4`. The earlier fault was reading `0x30` at another instruction;
the changed site does not establish a causal fix or playable rendering. Actual
pre-test runtime hashes, manifest, configuration and library are restored.
Audio is not qualified. See the [AVX and game record](evidence/ipad-async-avx-context-20261008.json).

## Asynchronous integer-flag follow-up: Build 37

A separate live-register probe preserved all 15 general registers but lost PF
and AF in both the delivered context and resumed guest code on Build 35. The
host comparison preserved them. The new AnyPS5/Madeira bridge reads the target's
reconstructed pinned CPU-state snapshot only after suspension and exact guest
PC/SP matching. It reconstructs architectural flags without leaking packed
NZCV bytes into reserved EFLAGS bits. A small guest continuation uses `popfq`
to avoid the lossy ARM64EC return conversion. It preserves general/vector state,
the red zone and the existing real native wait-result handling; C++ receives DF
clear while interrupted guest code receives the handler-selected DF value.

No new FEX source is generated or changed. A project-owned layout checker reads
actual FEX headers and is required by the native library build. All 32 AnyPS5
patches apply from the pin, match the root checkout, and reverse to the pin.
Madeira/Wine/FEX reverse-series checks also pass. Build 37 is development-signed
and installed; its packaged ntdll, xtajit64, winevulkan and win32u PE modules are
unchanged from Build 35. The new native Unix bridge and raw HLE kernel are rebuilt.

The final physical-device runs pass 553 deliveries with AVX and 451 without it,
including ten full integer-state cases and handler mutations. The host passes
451 cases with AVX explicitly skipped; it does not exercise the private native
bridge. CPU correctness evidence does not qualify arbitrary nested native
interruptions or TF/debug stepping.

A freshly relinked Solitaire package passes the same 47-dependency audit. Its
bounded 60-second device run, with normal memory tracking and all three opt-in
CPU paths, remains white at 15, 35 and 60 seconds. The graphics worker dereferences
a null object vtable and reads `0x20` at game RVA `0x1426827`; the rejected
8-sample draw remains. A changed fault site does not establish causality or
playable output. All 52 pre-test runtime-file hashes, manifest, library and
configuration are restored. The native options remain default-off in the
installed Build 37. See [source and result hashes](evidence/ipad-async-integer-flags-20261008.json).

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

## Same-day source and AVX follow-up

AnyPS5 main advanced by another 200 commits to `8388121a`. All 33 patches
were reconciled against that snapshot without losing newer HTTP2/NP, UTF16,
graphics tests or exception synchronization. Pristine application, 70-file
source equality and reversal pass. The fresh native host tools pass 30 tests,
111 production interpolation cases pass, and four sanitizer contract groups
pass. This source snapshot is not the installed HLE runtime; see the
[follow-up evidence](evidence/anyps5-followup-20261008.json).

Build43 contains a local FEX correction for stale upper YMM state at a
protected store. Per-instruction partial flushes previously omitted the upper
halves on NEON hosts. Pending VEX.128 zeroing or full AVX replacement could
therefore be invisible to the exception snapshot. The iOS-only mask now
commits those writes before another guest instruction can fault.

The unchanged six-case reproducer, which failed on Build42 with normal
multiblock/MAXINST5000 settings, passes on Build43 with those same settings:
six faults, no resumed-state, snapshot, query or data errors. The 24-case
nested regression passes with 12 nested faults and 108 correctly rejected
requests; the 153-case cross-page regression also passes. The same-process
read probe passes 13 cases without any callback. All four exit Wine with code0.
Configuration and library bytes are restored and own processes are verified
closed. See the [matched device evidence](evidence/ipad-pending-avx-fix-20261008.json).
This does not qualify FEX-2610, game frame rate, or the still-missing eight-sample
renderer path.
