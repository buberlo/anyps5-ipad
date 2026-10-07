# Added local and offline API coverage

The local/offline compatibility series covers 43 entry points required by
a Unity/IL2CPP title's import graph. Some are now provided by the refreshed
upstream; patch `0011-local-and-offline-service-apis.patch` preserves the
additional behavior and explicit failure contracts. Export availability and runtime behavior
are separate checks. This patch does not claim general Unity support or successful
game execution, and it does not implement PSN or original console services.

| Area | Behavior and limitations |
| --- | --- |
| File descriptors | `fchmod`, `sceKernelFchmod`, `futimes`, `utimes`: actual host operations, guest errno translation, timestamp validation and socket/invalid-descriptor errors. Windows regular-file operations use handles, so rename does not redirect them. Guest opens share deletion and retain metadata access where permitted; a denied metadata right does not prevent a permitted data-only open. Synthetic directory descriptors remain path-based. Times retain microseconds. Windows permission changes follow its writable/read-only model, and deletion stays pending until the last handle closes; this is not complete Unix filesystem behavior. Existing Windows CRT `stat` still reports whole seconds. |
| Thread scheduling | `scePthreadGetschedparam` / `scePthreadSetschedparam` use the existing priority implementation. FIFO is the supported guest scheduling policy; other policies fail. This does not establish native real-time scheduling. |
| JSON2 | `Object::size`, `InitParameter2` construction and buffer configuration, RTTI allocator configuration and `Initializer::initialize(InitParameter2*)`. Allocator callbacks run with the guest ABI and are copied at initialization. JSON objects, strings and containers use that allocator; each allocation retains its original release callback/context so reinitialization cannot redirect frees. The existing parser reads memory; a file-buffer setting does not add a file parser. The opaque `MemAllocator` vtable interface is not implemented: a non-null allocator fails explicitly, while a null allocator selects default allocation. It must not be confused with the supported callback-based RTTI allocator. |
| Progress dialogs | Increment/value/message APIs validate state and target, retain progress/text and saturate increments at 100. Native progress dialogs remain RUNNING until Close, and expose GetStatus. The existing headless dialog backend is retained: there is still no visible console dialog. |
| Local configuration | Accessibility zoom-follow-focus is disabled for the existing local user. Store-icon layout is retained as configuration. Music-player permission is retained as local state; it does not mute game audio or create a system player. |
| Offline PSN | Entitlement requests/polls and key access return signed-out failures; signaling preparation fails as unavailable. No keys, entitlement success, online request or request ID is fabricated. Reachability unregistration clears retained callback/context pointers. The unavailable request structs stay opaque instead of guessing their layout. |
| Offline SSL / HTTP2 | Certificate/name/PEM APIs reject unknown objects or return no name; they never dereference arbitrary certificate pointers or create synthetic trust material. The empty CA inventory and unsupported certificate loading remain. CookieFlush succeeds for a live offline HTTP2 context's empty cookie inventory and fails for invalid/terminated contexts. No TLS/network compatibility is claimed. |
| Unavailable console features | Reward icons return the existing icon-not-found error. Trophy-list UI, controller-settings UI and challenge activity return a negative ENOSYS **local compatibility result**, not a claimed library-specific console error. VRR calls return unsupported-output-mode; output checks are not disabled. |

The new JSON2 initialization/RTTI signatures and context-bearing SSL call shapes
were checked against local call sites. This is bounded ABI evidence, not a complete
SDK specification. Unavailable service paths intentionally do not access opaque
arguments or fill successful result structures. A title that requires one of these
services can still fail at runtime after its imports resolve.

The file/thread additions were originally compared with AnyPS5 commit
`9ab937b261a5bfdf253dd0f0d3bf371ffbdfb1fa`. The current pin is
`ee391a5614246338aec9cb7a3a3dd4f479aec9f3`; overlapping upstream definitions
have been reconciled while retaining descriptor lifetime, allocator ownership
and unavailable-service error checks.

Host tests also exposed two existing boundary errors. Unsigned conversions now
saturate both signs of overflow, including Windows CRT negative-overflow behavior,
while retaining errno on successful parses. Native guest threads allocate guard
padding outside the reported guest stack; the fully committed/writable stack
check remains enabled.

## Verification

Portable production JSON and offline-service tests run under AddressSanitizer and
UBSan on macOS:

```sh
python3 scripts/test-hle-compatibility.py
```

This does not qualify Windows calling conventions, descriptor behavior or FEX.
The qualified Windows build can run JSON/allocator, offline API, filesystem,
thread-priority and exception tests with:

```sh
APS5_HLE_TESTS=ON scripts/m0-build-anyps5-winlibs.sh
```

Supply the existing additional-target list through `APS5_HLE_TARGETS_FILE` when
building this broader closure. These tests execute synthetic host contracts only,
without game data. Dependency preparation must then use the freshly built HLE
archive. A source-level symbol check or old archive is not a completed dependency
check. Physical-iPad execution remains a further test.
