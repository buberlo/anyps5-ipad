#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <immintrin.h>

struct Object { uint64_t guard; uint64_t padding[7]; };
__attribute__((sysv_abi,noinline)) uint64_t TraceAnchor(struct Object *owner, struct Object **a, struct Object **b, struct Object **c) {
    uint64_t result = owner->guard;
    for (unsigned i=0;i<7;++i) result ^= a[i]->guard ^ b[i]->guard ^ c[i]->guard;
    SleepEx(0, TRUE);
    return result;
}
int main(void) {
    struct Object *objects = VirtualAlloc((void*)0x7400000000ULL, 0x4000, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if (!objects || (uintptr_t)objects != 0x7400000000ULL) { puts("[arg-probe] allocation failed"); return 2; }
    struct Object *source[24], *a[7], *b[7], *c[7];
    uint64_t expected = 0x5a5a123456789abcULL;
    objects[23].guard = expected;
    for (unsigned i=0;i<21;++i) { objects[i].guard=0x9876540000000000ULL+i*0x100001ULL; source[i]=objects+i; expected ^= objects[i].guard; }
    source[21]=source[22]=source[23]=objects+23;
    for (unsigned i=0;i<7;++i) { a[i]=source[i]; b[i]=source[i+7]; c[i]=source[i+14]; }
    // A real unaligned 256-bit pointer copy, including both high 64-bit halves.
    __m256i copy=_mm256_loadu_si256((const __m256i *)(source+7));
    _mm256_storeu_si256((__m256i *)b,copy);
    unsigned failures=0;
    register uint64_t root12 asm("r12")=0x1217121712171217ULL;
    register uint64_t root13 asm("r13")=0x1337133713371337ULL;
    register uint64_t root14 asm("r14")=0x1417141714171417ULL;
    register uint64_t root15 asm("r15")=0x1517151715171517ULL;
    for (unsigned run=0;run<10;++run) {
        asm volatile("" : "+r"(root12),"+r"(root13),"+r"(root14),"+r"(root15) : : "memory");
        uint64_t got=TraceAnchor(objects+23,a,b,c);
        asm volatile("" : "+r"(root12),"+r"(root13),"+r"(root14),"+r"(root15) : : "memory");
        if(got!=expected || root12!=0x1217121712171217ULL || root13!=0x1337133713371337ULL || root14!=0x1417141714171417ULL || root15!=0x1517151715171517ULL) ++failures;
        for(unsigned i=0;i<7;++i) if(a[i]!=source[i] || b[i]!=source[i+7] || c[i]!=source[i+14]) ++failures;
    }
    printf("[arg-probe] calls=10 pointer_checks=210 failures=%u checksum=%llx\n",failures,(unsigned long long)expected);
    VirtualFree(objects,0,MEM_RELEASE);
    return failures?1:0;
}
