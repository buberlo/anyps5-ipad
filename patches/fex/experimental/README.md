# Historical FEX experiment

`0003-win32-virtualprotect.patch` records the FEX-2608 allocator-hook experiment.
**FEX-2610 already contains that upstream correction. Do not apply this patch to
the current pin.** The default two-patch series preserves the canonical fix.

The Windows API returns nonzero on success and requires a previous-protection
pointer. The old hook violated both requirements; the Linux hook was unaffected.
Historical device evidence remains associated with its original runtime hashes.
