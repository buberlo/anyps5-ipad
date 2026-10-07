# Opt-in FEX patches

`scripts/apply-patches.sh` applies only `patches/fex/*.patch`. Files in this
directory stay off the default stack. Apply one by hand, on a throwaway
checkout, before an on-device experiment:

```sh
git -C upstreams/FEX apply ../../patches/fex/experimental/0003-win32-virtualprotect.patch
```

## 0003-win32-virtualprotect.patch

Upstream FEX-2608 `25f202171` (`AllocatorHooks: Amend VirtualProtect for Windows`).
Win32 `VirtualProtect` returns nonzero on success and rejects a null previous-protection
pointer, so the unpatched Windows hook both fails the call and reports success.
The iOS ARM64EC module uses this hook. The qualified Dreaming Sarah build ran
with the failing hook, so this patch is not in the default series: a successful
protect can change JIT and guest page permissions. The Linux `mprotect` hook
in the same header is already correct and is not modified.
