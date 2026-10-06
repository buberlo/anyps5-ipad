#include "cpu_vectors.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(__x86_64__) && !defined(CPU_REFERENCE_ONLY)
#include <cpuid.h>
#endif

static void print_lanes(const std::uint32_t* lanes) {
    std::putchar('[');
    for (unsigned i = 0; i < cpu_lane_count; ++i)
        std::printf("%s%u", i ? "," : "", lanes[i]);
    std::putchar(']');
}

int main(int argc, char** argv) {
    std::uint32_t seed = 0x1234u;
    if (argc > 1) {
        char* end = nullptr;
        const unsigned long long parsed = std::strtoull(argv[1], &end, 0);
        if (!argv[1][0] || *end || parsed > UINT32_MAX) return 64;
        seed = static_cast<std::uint32_t>(parsed);
    }
    std::uint32_t expected[cpu_case_count][cpu_lane_count]{};
    std::uint32_t actual[cpu_case_count][cpu_lane_count]{};
    cpu_reference(seed, expected);
#if defined(CPU_REFERENCE_ONLY)
    std::memcpy(actual, expected, sizeof(actual));
    const char* mode = "scalar_reference";
#elif defined(__x86_64__)
    unsigned eax, ebx, ecx, edx;
    bool available = __get_cpuid(1, &eax, &ebx, &ecx, &edx) &&
        (ecx & bit_AVX) && (ecx & bit_OSXSAVE);
    unsigned xcr0_low = 0, xcr0_high = 0;
    if (available) {
        asm volatile("xgetbv" : "=a"(xcr0_low), "=d"(xcr0_high) : "c"(0));
        available = (xcr0_low & 6) == 6;
    }
    available = available && __get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx) && (ebx & bit_AVX2);
    if (!available) {
        std::printf("{\"schema\":1,\"probe\":\"cpu\",\"mode\":\"avx2\",\"status\":\"unavailable\",\"xcr0\":%u}\n", xcr0_low);
        return 2;
    }
    cpu_avx2(seed, actual);
    const char* mode = "avx2";
#else
#error Build a scalar reference on non-x86 hosts with CPU_REFERENCE_ONLY.
#endif
    unsigned failed = 0;
    for (unsigned i = 0; i < cpu_case_count; ++i) {
        const bool pass = std::memcmp(actual[i], expected[i], sizeof(actual[i])) == 0;
        failed += !pass;
        std::printf("{\"schema\":1,\"probe\":\"cpu\",\"mode\":\"%s\",\"seed\":%u,\"case\":\"%s\",\"status\":\"%s\",\"actual\":",
            mode, seed, cpu_case_names[i], pass ? "pass" : "fail");
        print_lanes(actual[i]);
        std::printf(",\"expected\":");
        print_lanes(expected[i]);
        std::puts("}");
    }
    std::printf("{\"schema\":1,\"probe\":\"cpu\",\"mode\":\"%s\",\"summary\":true,\"failed\":%u}\n", mode, failed);
    return failed ? 1 : 0;
}
