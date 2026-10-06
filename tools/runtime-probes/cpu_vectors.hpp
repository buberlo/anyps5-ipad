#pragma once
#include <cstdint>

constexpr unsigned cpu_case_count = 6;
constexpr unsigned cpu_lane_count = 8;
static constexpr const char* cpu_case_names[cpu_case_count] = {
    "add_upper_lower", "permute_cross_lane", "shift_variable",
    "signed_compare_blend", "gather", "unaligned_load_store"
};

inline void cpu_reference(std::uint32_t seed, std::uint32_t out[cpu_case_count][cpu_lane_count]) {
    std::uint32_t a[8], b[8], table[32];
    for (unsigned i = 0; i < 8; ++i) {
        a[i] = (seed + i * 0x1020304u) ^ (i % 2 ? 0x80000000u : 0u);
        b[i] = seed * 3u + i * 17u;
    }
    for (unsigned i = 0; i < 32; ++i) table[i] = seed ^ (i * 0x1234567u);
    for (unsigned i = 0; i < 8; ++i) {
        out[0][i] = a[i] + b[i];
        out[1][i] = a[(i + 5) % 8];
        out[2][i] = i == 7 ? 0u : a[i] << (i * 5u);
        out[3][i] = static_cast<std::int32_t>(a[i]) > static_cast<std::int32_t>(b[i]) ? a[i] : b[i];
        out[4][i] = table[(i * 3 + 1) % 32];
        out[5][i] = a[i];
    }
}

extern "C" void cpu_avx2(std::uint32_t seed, std::uint32_t out[cpu_case_count][cpu_lane_count]);
