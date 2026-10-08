# Exception and protected-memory CPU state

Asynchronous `sceKernelRaiseException` delivery and ordinary Windows access
violations use different paths. Passing the asynchronous tests did not qualify
write-watch handling. A protected store can enter libc's VEH through FEX's
ARM64EC exception dispatcher without preserving all x86 state in `CONTEXT`.

## Ordinary protected-store repair

On the M2 iPad, the independent protected-store probe found correct general
registers and lower XMM halves, but lost upper halves of all sixteen YMM
registers, parity/auxiliary flags, and the direction flag. An interrupted DF=1
could also enter C++ with DF still set. The default-path six-case probe reported
103 errors. Its handler deliberately clears vector registers so accidental
preservation cannot pass the test.

`APS5_VEH_CONTEXT_BRIDGE=1` enables a paired native Madeira/libc bridge. Both
components must be rebuilt; updating only the app or only libc is insufficient.
The default remains disabled while game compatibility is unresolved.

1. Before the native `NtContinueEx` hands an access violation to
   `KiUserExceptionDispatcher`, Madeira identifies the current thread's FEX
   frame. It requires the exception PC/SP to match the reconstructed guest
   PC/SP, rejects native EC code and copies the missing state.
2. A private `NtQueryInformationThread` class, `0x7fff4151`, exposes a versioned
   328-byte snapshot to that same thread, using the current-thread pseudo handle.
   Exception code, access direction, fault address, PC and SP must match the
   latest snapshot. The transport does not expose the native CPU-frame pointer.
3. libc fills the missing flags before its existing access-violation handler.
   Heap and write-watch handling, protection changes, and dirty tracking remain
   enabled. Before C++ runs, the native dispatcher clears DF while retaining its
   original value in the snapshot.
4. For a handled exception, libc prepares restoration with the snapshot token
   and the handler's final PC/SP/flags. Native `NtContinueEx` restores PF, AF,
   DF and the sixteen upper YMM halves immediately before resuming. Wine retains
   the normal general-register/lower-XMM context path.

The snapshot state is thread-local and bounded to eight slots. Prepared outer
continuations are not evicted by a nested capture. A continuation must still
belong to the current FEX CPU frame; a changed frame is rejected instead of
writing through an obsolete pointer. Once libc detects the native provider, a
handled x86 access violation without a matching snapshot or with a failed preparation terminates the process rather
than silently continuing with corrupt CPU state. Hosts without this provider
retain their ordinary Windows exception path. Native ARM64EC accesses also use
the standard context path: `RtlIsEcCode` classifies the context PC and original
exception address, including Wine's relocated CRT code. They must not restore
a stale x86 CPU frame. This is a private pinned-runtime interface, not a general Windows `CONTEXT` replacement.

`APS5_VEH_CONTEXT_TRACE=1` enables at most 64 capture/restore messages per thread.
It is disabled by default. Failure reports remain enabled.

## Verification

Build the synthetic probe with `scripts/build-runtime-probes.sh` after applying
patches, or use a Windows x64 GCC toolchain:

```sh
x86_64-w64-mingw32-gcc -std=gnu11 -O2 -Wall -Wextra -Werror -static-libgcc \
  -Iupstreams/Madeira/build/ntdll-unix \
  tools/runtime-probes/protected_store_state_probe.c \
  -o protected-store-state-probe.exe
```

On the paired iPad runtime, set `APS5_VEH_CONTEXT_BRIDGE=1` before Wine starts.
The test checks 24 outer faults and twelve nested faults. It verifies all fifteen
general registers, seven arithmetic/direction flags, lower/upper halves of all
sixteen YMM registers, handler changes, DF=0 on callback entry, retried store data,
and 108 rejected malformed-length, wrong-token or invalid-flags requests.
A host without AVX or the native bridge does not qualify those cases.

`GuestMemory.cpp` separately exercises the real libc handler with native CRT
`memcpy` and `memset`, using nine transfer sizes across protected 16-KiB pages,
both shared aliases, dirty collection, re-arming and release. All 36 cases pass
on the physical iPad with the bridge enabled; the ordinary guest-memory checks
remain enabled as well.

`scripts/check-async-flags-layout.py` compiles against the actual pinned FEX
headers before a native build. It verifies the frame prefix, AVX-high offset and
size, flag fields, RSP index, and the transport layout. No FEX implementation is
changed by this bridge. Future dependency updates must pass this check again.

The [paired bridge evidence](evidence/ipad-protected-store-state-20261008.json)
records the binaries, source hashes, positive/negative probes, and game limits.

The separate asynchronous opt-in path uses `APS5_X64_SIGNAL_SUSPEND=1`,
`APS5_PRESERVE_ASYNC_AVX=1` and `APS5_PRESERVE_ASYNC_FLAGS=1`; its 1,004 device
deliveries are documented in the [asynchronous state record](evidence/ipad-async-integer-flags-20261008.json).
The full guest-memory suite remains a separate check of reservations, aliases,
protection, locking, write-watch and release. Neither suite replaces the other.

## Graphics qualification remains separate

The fixed-function interpolation path retains validation of supported RDNA I/J
programs. All 111 host guard cases and the prior fourteen complete private
SPIR-V replays pass. Unrecognized interpolation programs and unsupported sample
counts remain explicit errors. No new interpolation approximation is introduced
by the CPU-state bridge.

Solitaire still has an unresolved game-side null-pointer fault in
`UnityGfxDeviceWorker`; the Build 38 and Build 40 bridge runs read `0x30`
at game RVA `0x1461222`
and stayed on the launch screen. Earlier runs rejected an 8-sample draw on the
M2, whose queried Vulkan attachment counts are 1, 2 and 4. The CPU test repair
therefore does not establish rendered or playable Solitaire.
