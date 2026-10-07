#include <stdint.h>
#include <stdio.h>

/* Compare real x86 instructions with an independent integer flags oracle.
 * In particular, add/sub with -128 must never become an invalid ARM immediate.
 */
struct outcome { uint64_t result, flags; };
typedef struct outcome (*operation)(uint64_t);
#define OP(name, instruction, immediate, width, reg) \
    __attribute__((noinline)) static struct outcome name(uint64_t input) { \
        uint64_t result = input, flags; \
        __asm__ volatile(instruction width " $" immediate ",%" reg "0; pushfq; popq %1" \
                         : "+r"(result), "=&r"(flags) : : "cc", "memory"); \
        return (struct outcome){result, flags}; \
    }
OP(add64_neg128, "add", "-128", "q", "")
OP(sub64_neg128, "sub", "-128", "q", "")
OP(add64_pos128, "add", "128", "q", "")
OP(sub64_pos128, "sub", "128", "q", "")
OP(add64_large, "add", "74565", "q", "")
OP(sub64_large, "sub", "74565", "q", "")
OP(add32_neg128, "add", "-128", "l", "k")
OP(sub32_neg128, "sub", "-128", "l", "k")
OP(add32_pos128, "add", "128", "l", "k")
OP(sub32_pos128, "sub", "128", "l", "k")

static struct outcome reference(uint64_t input, int64_t immediate, unsigned bits, int subtract) {
    uint64_t mask = bits == 64 ? UINT64_MAX : UINT32_MAX;
    uint64_t sign = UINT64_C(1) << (bits - 1);
    uint64_t a = input & mask, b = (uint64_t)immediate & mask;
    uint64_t r = (subtract ? a - b : a + b) & mask;
    unsigned parity = 0;
    for (unsigned i = 0; i < 8; ++i) parity ^= (unsigned)((r >> i) & 1);
    uint64_t flags = (subtract ? a < b : r < a);
    flags |= (uint64_t)!parity << 2;
    flags |= (a ^ b ^ r) & 0x10;
    flags |= (uint64_t)(r == 0) << 6;
    flags |= (uint64_t)((r & sign) != 0) << 7;
    uint64_t overflow = (subtract ? a ^ b : ~(a ^ b)) & (a ^ r) & sign;
    flags |= (uint64_t)(overflow != 0) << 11;
    return (struct outcome){r, flags};
}

int main(void) {
    const struct { const char *name; operation run; int64_t immediate; unsigned bits; int subtract; } ops[] = {
        {"add64_neg128", add64_neg128, -128, 64, 0}, {"sub64_neg128", sub64_neg128, -128, 64, 1},
        {"add64_pos128", add64_pos128, 128, 64, 0}, {"sub64_pos128", sub64_pos128, 128, 64, 1},
        {"add64_large", add64_large, 74565, 64, 0}, {"sub64_large", sub64_large, 74565, 64, 1},
        {"add32_neg128", add32_neg128, -128, 32, 0}, {"sub32_neg128", sub32_neg128, -128, 32, 1},
        {"add32_pos128", add32_pos128, 128, 32, 0}, {"sub32_pos128", sub32_pos128, 128, 32, 1}
    };
    const uint64_t inputs[] = {0, 1, 127, 128, 129, 255, 256, UINT32_MAX,
        UINT64_C(0x7fffffff), UINT64_C(0x80000000), UINT64_C(0xffffff80),
        UINT64_C(0x7fffffffffffff80), UINT64_C(0x7fffffffffffffff),
        UINT64_C(0x8000000000000000), UINT64_C(0xffffffffffffff80), UINT64_MAX};
    unsigned checks = 0, failures = 0;
    for (unsigned i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        for (unsigned j = 0; j < sizeof(inputs) / sizeof(inputs[0]); ++j) {
            struct outcome got = ops[i].run(inputs[j]);
            struct outcome want = reference(inputs[j], ops[i].immediate, ops[i].bits, ops[i].subtract);
            ++checks;
            if (got.result != want.result || (got.flags & 0x8d5) != want.flags) {
                ++failures;
                printf("[negative-arithmetic] mismatch op=%s input=%llx result=%llx expected=%llx flags=%llx expected-flags=%llx\n",
                       ops[i].name, (unsigned long long)inputs[j], (unsigned long long)got.result,
                       (unsigned long long)want.result, (unsigned long long)(got.flags & 0x8d5),
                       (unsigned long long)want.flags);
            }
        }
    }
    printf("[negative-arithmetic] checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
