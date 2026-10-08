/* Own deterministic regression: pending upper-YMM changes at a write fault.
 * No game code/data. The update and fault must remain in one JIT block. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aps5_veh_state.h"

static LONG (WINAPI *QueryState)(HANDLE, DWORD, void *, ULONG, ULONG *);
static unsigned Faults, QueryErrors, InvalidFaults, SnapshotErrors;
static int Protected, Mode;
void *Page;
uint64_t Before[12], After[4], Output[12], InputFlags, OutputFlags;
static uint64_t SnapshotFlags, SnapshotUpper[32];
void __attribute__((sysv_abi)) ClearUpper(void);
void __attribute__((sysv_abi)) ReplaceUpper(void);

static LONG CALLBACK repair(EXCEPTION_POINTERS *p) {
    EXCEPTION_RECORD *e = p->ExceptionRecord;
    if (e->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    if (e->NumberParameters < 2 || e->ExceptionInformation[0] != 1 ||
        e->ExceptionInformation[1] != (ULONG_PTR)Page || !Protected) {
        ++InvalidFaults;
        return EXCEPTION_CONTINUE_SEARCH;
    }
    struct aps5_veh_state_info info = {0};
    info.version = 1;
    info.code = e->ExceptionCode;
    info.direction = 1;
    info.rip = p->ContextRecord->Rip;
    info.rsp = p->ContextRecord->Rsp;
    info.address = e->ExceptionInformation[1];
    ULONG returned = 0;
    if (QueryState(GetCurrentThread(), APS5_THREAD_VEH_STATE, &info, sizeof(info), &returned) ||
        returned != sizeof(info) || !info.token || !info.has_avx) {
        ++QueryErrors;
        return EXCEPTION_CONTINUE_SEARCH;
    }
    SnapshotFlags = info.flags;
    memcpy(SnapshotUpper, info.ymm_upper, sizeof(SnapshotUpper));
    for (unsigned i = 0; i < 2; ++i) {
        const uint64_t expected = Mode == 0 ? 0 : After[2+i];
        if (info.ymm_upper[2+i] != expected) ++SnapshotErrors;
    }
    DWORD old;
    if (!VirtualProtect(Page, 0x4000, PAGE_READWRITE, &old)) return EXCEPTION_CONTINUE_SEARCH;
    ++Faults;
    __asm__ volatile("vzeroall" ::: "xmm0", "xmm1", "xmm2", "xmm3",
                     "xmm4", "xmm5", "xmm6", "xmm7", "xmm8", "xmm9",
                     "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15");
    p->ContextRecord->EFlags = info.flags;
    info.operation = 1;
    info.resume_rip = info.rip;
    info.resume_rsp = info.rsp;
    if (QueryState(GetCurrentThread(), APS5_THREAD_VEH_STATE, &info, sizeof(info), &returned)) {
        ++QueryErrors;
        return EXCEPTION_CONTINUE_SEARCH;
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

#define PREFIX \
    "movq Page(%rip), %rdi\n" \
    "vmovdqu Before(%rip), %ymm0\n" \
    "vmovdqu Before+32(%rip), %ymm1\n" \
    "vmovdqu Before+64(%rip), %ymm2\n" \
    "pushq InputFlags(%rip)\n" \
    "popfq\n"
/* No branch, call, POPFQ or fence between the pending update and fault. */
#define SUFFIX \
    "vmovdqu %xmm0, (%rdi)\n" \
    "pushfq\n" \
    "popq OutputFlags(%rip)\n" \
    "cld\n" \
    "vmovdqu %ymm0, Output(%rip)\n" \
    "vmovdqu %ymm1, Output+32(%rip)\n" \
    "vmovdqu %ymm2, Output+64(%rip)\n" \
    "vzeroupper\n" \
    "ret\n"
__asm__(".text\n.global ClearUpper\nClearUpper:\n" PREFIX
        "vmovdqu After(%rip), %xmm1\n" SUFFIX
        ".global ReplaceUpper\nReplaceUpper:\n" PREFIX
        "vmovdqu After(%rip), %ymm1\n" SUFFIX);

int main(int argc, char **argv) {
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--unprotected"))) return 64;
    Protected = argc == 1;
    __builtin_cpu_init();
    if (!__builtin_cpu_supports("avx")) {
        puts("[pending-avx] AVX unavailable; not qualified");
        return 77;
    }
    if (Protected && (!getenv("APS5_VEH_CONTEXT_BRIDGE") ||
        strcmp(getenv("APS5_VEH_CONTEXT_BRIDGE"), "1"))) return 77;
    QueryState = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    if (Protected && !QueryState) return 14;
    Page = VirtualAlloc((void *)UINT64_C(0x7400000000), 0x10000,
                        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if ((uintptr_t)Page != UINT64_C(0x7400000000)) return 10;
    void *handler = AddVectoredExceptionHandler(1, repair);
    if (!handler) return 11;
    for (unsigned i = 0; i < 12; ++i)
        Before[i] = UINT64_C(0xabcdef0102030405) ^ ((uint64_t)(i+1) * UINT64_C(0x0101010101010101));
    for (unsigned i = 0; i < 4; ++i)
        After[i] = UINT64_C(0x1234567890abcdef) ^ ((uint64_t)(i+1) * UINT64_C(0x0202020202020202));
    const uint64_t flags[] = {0x202, 0xad7, 0xed7};
    unsigned errors = 0, cases = 0;
    for (Mode = 0; Mode < 2; ++Mode) for (unsigned f = 0; f < 3; ++f) {
        InputFlags = flags[f];
        memset(Page, 0xa5, 0x4000);
        memset(Output, 0, sizeof(Output));
        DWORD old;
        if (Protected && !VirtualProtect(Page, 0x4000, PAGE_READONLY, &old)) return 12;
        if (Mode == 0) ClearUpper(); else ReplaceUpper();
        unsigned bad = 0;
        for (unsigned i = 0; i < 12; ++i) {
            const uint64_t expected = i/4 != 1 ? Before[i] :
                (Mode == 0 && i%4 >= 2 ? 0 : After[i%4]);
            if (Output[i] != expected) ++bad;
        }
        if ((OutputFlags & 0xcd5) != (InputFlags & 0xcd5)) ++bad;
        if (Protected && (SnapshotFlags & 0xcd5) != (InputFlags & 0xcd5)) ++bad;
        if (memcmp(Page, Before, 16)) ++bad;
        for (unsigned i = 16; i < 0x4000; ++i)
            if (((const unsigned char *)Page)[i] != 0xa5) ++bad;
        errors += bad;
        ++cases;
        printf("[pending-avx] case=%u mode=%s protected=%d errors=%u upper=%llx/%llx snapshot=%llx/%llx\n",
               cases, Mode == 0 ? "clear" : "replace", Protected, bad,
               (unsigned long long)Output[6], (unsigned long long)Output[7],
               (unsigned long long)SnapshotUpper[2], (unsigned long long)SnapshotUpper[3]);
        fflush(stdout);
    }
    RemoveVectoredExceptionHandler(handler);
    VirtualFree(Page, 0, MEM_RELEASE);
    printf("[pending-avx] cases=%u faults=%u errors=%u snapshot_errors=%u query_errors=%u invalid_faults=%u\n",
           cases, Faults, errors, SnapshotErrors, QueryErrors, InvalidFaults);
    fflush(stdout);
    return errors || SnapshotErrors || QueryErrors || InvalidFaults || Faults != (Protected ? cases : 0);
}
