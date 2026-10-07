/* Independent MEM_WRITE_WATCH retry/reset qualification, without game data.
 * Check both halves of two AVX registers and SysV red-zone scratch across
 * transparent write faults. Also verify every byte of the allocation and
 * coverage of the written pages. Coarser host-page dirty reporting is allowed.
 * The runtime must also exit 0; a printed summary alone is insufficient.
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define REGION_BYTES 0x10000u
#define ROUNDS 128u
extern __attribute__((sysv_abi)) uint64_t watched_store(void *, const void *, void *);
__asm__(
    ".text\n.global watched_store\nwatched_store:\n"
    "vmovdqu (%rsi), %ymm0\nvmovdqu 32(%rsi), %ymm1\n"
    "movabsq $0x13579bdf2468ace0, %r8\n"
    "movq %r8, -8(%rsp)\nmovq %r8, -16(%rsp)\n"
    "movq %r8, -24(%rsp)\nmovq %r8, -32(%rsp)\n"
    "movq %r8, -40(%rsp)\nmovq %r8, -48(%rsp)\n"
    "movq %r8, -56(%rsp)\nmovq %r8, -64(%rsp)\n"
    "movq %r8, -72(%rsp)\nmovq %r8, -80(%rsp)\n"
    "movq %r8, -88(%rsp)\nmovq %r8, -96(%rsp)\n"
    "movq %r8, -104(%rsp)\nmovq %r8, -112(%rsp)\n"
    "movq %r8, -120(%rsp)\nmovq %r8, -128(%rsp)\n"
    "vmovdqu %ymm0, (%rdi)\nvmovdqu %ymm1, 32(%rdi)\n"
    "vmovdqu %ymm0, (%rdx)\nvmovdqu %ymm1, 32(%rdx)\n"
    "xorq %rax, %rax\nmovq $16, %rcx\nleaq -128(%rsp), %r9\n"
    "watched_scratch_check:\n"
    "cmpq %r8, (%r9)\nje watched_scratch_ok\nincq %rax\n"
    "watched_scratch_ok:\naddq $8, %r9\ndecq %rcx\njnz watched_scratch_check\n"
    "vzeroupper\nret\n"
);

static int query(unsigned char *region, size_t offset, int expect_write, int reset) {
    void *pages[REGION_BYTES / 4096];
    ULONG_PTR count = sizeof(pages) / sizeof(pages[0]);
    ULONG granularity = 0;
    UINT status = GetWriteWatch(reset ? WRITE_WATCH_FLAG_RESET : 0,
                               region, REGION_BYTES, pages, &count, &granularity);
    if (status || !granularity || count > sizeof(pages) / sizeof(pages[0])) return 1;
    if (!expect_write) return count ? 2 : 0;
    for (size_t byte = offset; byte < offset + 64; ++byte) {
        int covered = 0;
        for (ULONG_PTR i = 0; i < count; ++i) {
            uintptr_t p = (uintptr_t)pages[i];
            if (p < (uintptr_t)region || p >= (uintptr_t)region + REGION_BYTES ||
                p % granularity) return 3;
            if ((uintptr_t)region + byte >= p && (uintptr_t)region + byte < p + granularity)
                covered = 1;
        }
        if (!covered) return 4;
    }
    return 0;
}

struct writer {
    HANDLE ready, done;
    unsigned char *destination;
    uint64_t pattern[8], preserved[8], scratch_errors;
};
static DWORD WINAPI write_thread(void *argument) {
    struct writer *writer = argument;
    for (unsigned round = 0; round < ROUNDS; ++round) {
        if (WaitForSingleObject(writer->ready, INFINITE) != WAIT_OBJECT_0) return 1;
        writer->scratch_errors = watched_store(writer->destination, writer->pattern, writer->preserved);
        if (!SetEvent(writer->done)) return 2;
    }
    return 0;
}

static int concurrent_checks(unsigned *checks) {
    struct writer writers[4];
    HANDLE threads[4], done[4];
    unsigned char expected[REGION_BYTES];
    unsigned char *region = VirtualAlloc((void *)UINT64_C(0x7400000000), REGION_BYTES,
        MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE);
    if ((uintptr_t)region != UINT64_C(0x7400000000)) return 20;
    for (unsigned i = 0; i < 4; ++i) {
        writers[i].ready = CreateEventA(NULL, FALSE, FALSE, NULL);
        done[i] = writers[i].done = CreateEventA(NULL, FALSE, FALSE, NULL);
        writers[i].destination = region + i * 0x4000 + 0xff0;
        if (!writers[i].ready || !done[i]) return 21;
        threads[i] = CreateThread(NULL, 0, write_thread, &writers[i], 0, NULL);
        if (!threads[i]) return 22;
    }
    unsigned failures = 0;
    for (unsigned round = 0; round < ROUNDS; ++round) {
        for (size_t i = 0; i < REGION_BYTES; ++i)
            region[i] = expected[i] = (unsigned char)(i * 37 + round * 19 + 11);
        if (ResetWriteWatch(region, REGION_BYTES)) return 23;
        if (query(region, 0, 0, 0)) return 24;
        for (unsigned i = 0; i < 4; ++i) {
            for (unsigned j = 0; j < 8; ++j)
                writers[i].pattern[j] = UINT64_C(0xfedcba9876543210) ^
                    ((uint64_t)(j + 1) << 56) ^ ((uint64_t)i << 32) ^ round;
            memset(writers[i].preserved, 0, sizeof(writers[i].preserved));
            memcpy(expected + i * 0x4000 + 0xff0, writers[i].pattern, sizeof(writers[i].pattern));
            if (!SetEvent(writers[i].ready)) return 25;
        }
        if (WaitForMultipleObjects(4, done, TRUE, 10000) != WAIT_OBJECT_0) return 26;
        for (unsigned i = 0; i < 4; ++i) {
            int failure = writers[i].scratch_errors ? 1 : 0;
            if (memcmp(writers[i].preserved, writers[i].pattern, sizeof(writers[i].pattern))) failure |= 2;
            if (memcmp(region, expected, REGION_BYTES)) failure |= 4;
            if (query(region, i * 0x4000 + 0xff0, 1, i == 3)) failure |= 8;
            ++*checks;
            if (failure) {
                ++failures;
                printf("[write-watch] concurrent=1 round=%u writer=%u failure=%d scratch=%llu\n",
                    round, i, failure, (unsigned long long)writers[i].scratch_errors);
                fflush(stdout);
            }
        }
        if (query(region, 0, 0, 0) || memcmp(region, expected, REGION_BYTES)) ++failures;
    }
    if (WaitForMultipleObjects(4, threads, TRUE, 10000) != WAIT_OBJECT_0) return 27;
    for (unsigned i = 0; i < 4; ++i) {
        DWORD code;
        if (!GetExitCodeThread(threads[i], &code) || code) return 28;
        CloseHandle(threads[i]);
        CloseHandle(writers[i].ready);
        CloseHandle(done[i]);
    }
    VirtualFree(region, 0, MEM_RELEASE);
    printf("[write-watch] concurrent=1 checks=%u failures=%u\n", *checks, failures);
    fflush(stdout);
    return failures ? 1 : 0;
}

int main(void) {
    unsigned checks = 0, failures = 0;
    unsigned char expected[REGION_BYTES];
    uint64_t pattern[8], preserved[8];
    const size_t offsets[] = {0, 0xff0, 0x3ff0, 0x7ff1, 0xbfe1, REGION_BYTES - 64};
    for (unsigned watch = 0; watch < 2; ++watch) {
        unsigned char *region = VirtualAlloc((void *)UINT64_C(0x7400000000), REGION_BYTES,
            MEM_RESERVE | MEM_COMMIT | (watch ? MEM_WRITE_WATCH : 0), PAGE_READWRITE);
        if ((uintptr_t)region != UINT64_C(0x7400000000)) return 10;
        for (unsigned round = 0; round < ROUNDS; ++round) {
            size_t offset = offsets[round % (sizeof(offsets) / sizeof(offsets[0]))];
            for (size_t i = 0; i < REGION_BYTES; ++i)
                region[i] = expected[i] = (unsigned char)(i * 37 + round * 19 + 11);
            if (watch && ResetWriteWatch(region, REGION_BYTES)) return 11;
            if (watch && query(region, offset, 0, 0)) return 12;
            for (unsigned i = 0; i < 8; ++i)
                pattern[i] = UINT64_C(0x0123456789abcdef) ^ ((uint64_t)(i + 1) << 56) ^ round;
            memset(preserved, 0, sizeof(preserved));
            uint64_t scratch_errors = watched_store(region + offset, pattern, preserved);
            memcpy(expected + offset, pattern, sizeof(pattern));
            int failure = scratch_errors ? 1 : 0;
            if (memcmp(preserved, pattern, sizeof(pattern))) failure |= 2;
            if (memcmp(region, expected, REGION_BYTES)) failure |= 4;
            if (watch && query(region, offset, 1, 1)) failure |= 8;
            if (watch && query(region, offset, 0, 0)) failure |= 16;
            if (memcmp(region, expected, REGION_BYTES)) failure |= 32;
            ++checks;
            if (failure) {
                ++failures;
                printf("[write-watch] watch=%u round=%u offset=%llx failure=%d scratch=%llu\n",
                    watch, round, (unsigned long long)offset, failure,
                    (unsigned long long)scratch_errors);
                fflush(stdout);
                break;
            }
        }
        VirtualFree(region, 0, MEM_RELEASE);
        printf("[write-watch] watch=%u checks=%u failures=%u\n", watch, checks, failures);
        fflush(stdout);
        if (failures) break;
    }
    if (!failures) {
        int result = concurrent_checks(&checks);
        if (result) {
            ++failures;
            printf("[write-watch] concurrent_result=%d\n", result);
        }
    }
    printf("[write-watch] checks=%u failures=%u\n", checks, failures);
    fflush(stdout);
    return failures ? 1 : 0;
}
