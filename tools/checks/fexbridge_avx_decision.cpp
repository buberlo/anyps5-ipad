/* SPDX-License-Identifier: MIT */
/* Compile-check the FEXBridge AVX decision against the pinned FEX headers.
 * This is the expression in patches/madeira/0001-fexbridge-avx.patch.
 * FEXBridge.mm itself is Objective-C++ and is not compiled here.
 *
 * Exit 0 when AVX is enabled (the default, and any value other than "0").
 * Exit 1 when MADEIRA_FEX_AVX=0.
 */
#include <FEXCore/Core/HostFeatures.h>

#include <cstdlib>
#include <cstdio>

/* HostFeatures pulls fextl::vector, whose deallocate references the FEX
 * allocator. This TU does not allocate. The stub lets the decision link
 * without libFEXCore. */
namespace FEXCore::Allocator {
void aligned_free(void*) {}
}

int main() {
    FEXCore::HostFeatures features{};
    features.SupportsAES = true;
    const char* avx = std::getenv("MADEIRA_FEX_AVX");
    features.SupportsAVX = !(avx && avx[0] == '0' && avx[1] == '\0');
    features.SupportsSVE128 = false;
    features.SupportsSVE256 = false;
    if (features.SupportsAVX && features.SupportsAES) features.SupportsAES256 = true;

    const bool sve = features.SupportsSVE();
    std::printf("avx=%d sve=%d aes256=%d env=%s\n",
                features.SupportsAVX ? 1 : 0,
                sve ? 1 : 0,
                features.SupportsAES256 ? 1 : 0,
                avx ? avx : "unset");
    if (sve) return 2;
    if (features.SupportsAVX != features.SupportsAES256) return 3;
    return features.SupportsAVX ? 0 : 1;
}
