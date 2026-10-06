#include "cpu_vectors.hpp"
#include <cstring>
#include <immintrin.h>

// This translation unit alone is compiled with -mavx2. The executable can
// report an unavailable ISA without taking an illegal instruction first.
extern "C" void cpu_avx2(std::uint32_t seed, std::uint32_t out[cpu_case_count][cpu_lane_count]) {
    alignas(32) std::uint32_t a[8], b[8], table[32], indices[8], shifts[8], gather[8];
    for (unsigned i = 0; i < 8; ++i) {
        a[i] = (seed + i * 0x1020304u) ^ (i % 2 ? 0x80000000u : 0u);
        b[i] = seed * 3u + i * 17u;
        indices[i] = (i + 5) % 8;
        shifts[i] = i * 5u;
        gather[i] = (i * 3 + 1) % 32;
    }
    for (unsigned i = 0; i < 32; ++i) table[i] = seed ^ (i * 0x1234567u);
    const auto x = _mm256_load_si256(reinterpret_cast<const __m256i*>(a));
    const auto y = _mm256_load_si256(reinterpret_cast<const __m256i*>(b));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out[0]), _mm256_add_epi32(x, y));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out[1]),
        _mm256_permutevar8x32_epi32(x, _mm256_load_si256(reinterpret_cast<const __m256i*>(indices))));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out[2]),
        _mm256_sllv_epi32(x, _mm256_load_si256(reinterpret_cast<const __m256i*>(shifts))));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out[3]),
        _mm256_blendv_epi8(y, x, _mm256_cmpgt_epi32(x, y)));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out[4]),
        _mm256_i32gather_epi32(reinterpret_cast<const int*>(table),
            _mm256_load_si256(reinterpret_cast<const __m256i*>(gather)), 4));
    // Force a genuinely unaligned 256-bit access across a 32-byte boundary.
    alignas(32) unsigned char unaligned[65];
    std::memcpy(unaligned + 17, a, sizeof(a));
    asm volatile("" : : "r"(unaligned) : "memory");
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out[5]),
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(unaligned + 17)));
    _mm256_zeroupper();
}
