/* Independent x64 -> native CRT -> x64 VEH -> native retry qualification.
 * Covers SysV nonvolatile registers, individual 16-KiB protection faults,
 * private memory and two views of the same section. No game data is used.
 * The final summary is not a lifecycle pass: also require process exit 0.
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>

typedef void *(__cdecl *copy_fn)(void *, const void *, size_t);
typedef void *(__cdecl *fill_fn)(void *, int, size_t);
static unsigned char *region, *alias;
static volatile LONG faults, invalid_faults;
static ULONG_PTR last_rip;

static LONG CALLBACK handle_fault(EXCEPTION_POINTERS *info) {
    EXCEPTION_RECORD *r = info->ExceptionRecord;
    if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2 ||
        r->ExceptionInformation[1] < (ULONG_PTR)region ||
        r->ExceptionInformation[1] >= (ULONG_PTR)region + 0x10000)
        return EXCEPTION_CONTINUE_SEARCH;
    last_rip = info->ContextRecord->Rip;
    if (r->ExceptionInformation[0] != 1) {
        InterlockedIncrement(&invalid_faults);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    DWORD old;
    void *page = (void *)(r->ExceptionInformation[1] & ~(ULONG_PTR)0x3fff);
    if (!VirtualProtect(page, 0x4000, PAGE_READWRITE, &old)) return EXCEPTION_CONTINUE_SEARCH;
    InterlockedIncrement(&faults);
    return EXCEPTION_CONTINUE_EXECUTION;
}

__attribute__((sysv_abi,noinline)) static int write_and_check(copy_fn copy, fill_fn fill,
                                                    const unsigned char *source, size_t size, int use_fill) {
    register uint64_t r12 __asm__("r12") = UINT64_C(0x1217121712171217);
    register uint64_t r13 __asm__("r13") = UINT64_C(0x1337133713371337);
    register uint64_t r14 __asm__("r14") = UINT64_C(0x1417141714171417);
    register uint64_t r15 __asm__("r15") = UINT64_C(0x1517151715171517);
    unsigned char *destination = region + 0x3ff9;
    DWORD old;
    if (!VirtualProtect(region, 0x10000, PAGE_READONLY, &old)) return 1;
    LONG before = faults;
    __asm__ volatile("" : "+r"(r12), "+r"(r13), "+r"(r14), "+r"(r15) : : "memory");
    void *result = use_fill ? fill(destination, 0xa5, size) : copy(destination, source, size);
    __asm__ volatile("" : "+r"(r12), "+r"(r13), "+r"(r14), "+r"(r15) : : "memory");
    LONG expected_faults = (LONG)((0x3ff9 + size - 1) / 0x4000 + 1);
    if (result != destination || faults != before + expected_faults || invalid_faults) return 2;
    if (r12 != UINT64_C(0x1217121712171217) || r13 != UINT64_C(0x1337133713371337) ||
        r14 != UINT64_C(0x1417141714171417) || r15 != UINT64_C(0x1517151715171517)) return 3;
    for (size_t i = 0; i < size; ++i)
        if (destination[i] != (use_fill ? 0xa5 : source[i]) ||
            (alias && alias[0x3ff9 + i] != destination[i])) return 4;
    return 0;
}

int main(void) {
    HMODULE crt = LoadLibraryA("ucrtbase.dll");
    if (!crt) return 10;
    copy_fn copy = (copy_fn)GetProcAddress(crt, "memcpy");
    fill_fn fill = (fill_fn)GetProcAddress(crt, "memset");
    if (!copy || !fill) return 11;
    region = VirtualAlloc((void *)UINT64_C(0x7400000000), 0x10000,
                          MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    unsigned char *source = VirtualAlloc(NULL, 0x8000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if ((uintptr_t)region != UINT64_C(0x7400000000) || !source) return 12;
    for (size_t i = 0; i < 0x8000; ++i) source[i] = (unsigned char)(i * 37 + 11);
    void *handler = AddVectoredExceptionHandler(1, handle_fault);
    if (!handler) return 13;
    const size_t sizes[] = {1, 7, 16, 31, 128, 512, 4096, 16384, 32768};
    unsigned checks = 0;
    HANDLE section = NULL;
    for (unsigned shared = 0; shared < 2; ++shared) {
        if (shared) {
            VirtualFree(region, 0, MEM_RELEASE);
            region = NULL;
            section = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0x10000, NULL);
            if (!section) return 14;
            region = MapViewOfFileEx(section, FILE_MAP_ALL_ACCESS, 0, 0, 0x10000,
                                    (void *)UINT64_C(0x7400000000));
            alias = MapViewOfFile(section, FILE_MAP_READ, 0, 0, 0x10000);
            if ((uintptr_t)region != UINT64_C(0x7400000000) || !alias) return 15;
        }
        for (unsigned mode = 0; mode < 2; ++mode) {
            for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
                int failure = write_and_check(copy, fill, source, sizes[i], mode);
                ++checks;
                printf("[native-write-fault] shared=%u mode=%s size=%llu failure=%d faults=%ld rip=%llx\n",
                       shared, mode ? "memset" : "memcpy", (unsigned long long)sizes[i], failure,
                       (long)faults, (unsigned long long)last_rip);
                fflush(stdout);
                if (failure) return 20 + failure;
            }
        }
    }
    RemoveVectoredExceptionHandler(handler);
    UnmapViewOfFile(alias);
    UnmapViewOfFile(region);
    CloseHandle(section);
    VirtualFree(source, 0, MEM_RELEASE);
    printf("[native-write-fault] checks=%u faults=%ld failures=0\n", checks, (long)faults);
    fflush(stdout);
    return 0;
}
