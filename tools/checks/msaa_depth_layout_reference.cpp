// Verify the production depth/stencil layout against the independently linked AMD AddrLib.
#include "prx/libSceAgcDriver/Graphics/include/DepthTargetLayout.hpp"
#include <addrinterface.h>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include <vector>

using AgcDriver::Graphics::DepthTargetLayout;
using AgcDriver::Graphics::DepthPlane;
using AgcDriver::Graphics::DepthLayoutParameters;

static void* ADDR_API allocate(const ADDR_ALLOCSYSMEM_INPUT* input) {
    return std::malloc(input->sizeInBytes);
}
static ADDR_E_RETURNCODE ADDR_API release(const ADDR_FREESYSMEM_INPUT* input) {
    std::free(input->pVirtAddr);
    return ADDR_OK;
}
template<class Function> void rejects(Function function) {
    try { function(); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error("invalid depth/stencil layout accepted");
}

int main() {
    ADDR_CREATE_INPUT create{};
    create.size = sizeof(create);
    create.chipEngine = 13;       // Arctic Islands engine
    create.chipFamily = 0x8f;     // NV
    create.chipRevision = 1;      // non-RB+ Navi10
    create.regValue.gbAddrConfig = 4u | (3u << 6u); // 16 pipes, 256-byte interleave
    create.callbacks.allocSysMem = allocate;
    create.callbacks.freeSysMem = release;
    create.createFlags.fillSizeFields = 1;
    ADDR_CREATE_OUTPUT library{};
    library.size = sizeof(library);
    assert(AddrCreate(&create, &library) == ADDR_OK);
    std::size_t addresses = 0, layouts = 0, roundTrips = 0;
    for (const auto bytes : {1u, 2u, 4u}) {
        for (const auto samples : {1u, 2u, 4u, 8u}) {
            for (const auto extent : {std::array{1u, 1u}, std::array{257u, 129u},
                                     std::array{1920u, 1080u}, std::array{3840u, 2160u}, std::array{16384u, 16384u}}) {
                const auto width = extent[0], height = extent[1];
                const auto plane = static_cast<DepthPlane>(bytes);
                const DepthLayoutParameters parameters{.samples = samples, .fragments = samples};
                const DepthTargetLayout actual(width, height, plane, parameters);
                ADDR2_COMPUTE_SURFACE_INFO_INPUT info{};
                info.size = sizeof(info);
                if (plane == DepthPlane::Stencil8) info.flags.stencil = 1;
                else info.flags.depth = 1;
                info.swizzleMode = ADDR_SW_64KB_Z_X;
                info.resourceType = ADDR_RSRC_TEX_2D;
                info.bpp = bytes * 8u;
                info.width = width;
                info.height = height;
                info.numSlices = info.numMipLevels = 1;
                info.numSamples = info.numFrags = samples;
                ADDR2_COMPUTE_SURFACE_INFO_OUTPUT reference{};
                reference.size = sizeof(reference);
                assert(Addr2ComputeSurfaceInfo(library.hLib, &info, &reference) == ADDR_OK);
                assert(actual.Bytes() == reference.surfSize && actual.Alignment() == reference.baseAlign);
                assert(actual.LinearBytes() == static_cast<std::size_t>(width) * height * bytes * samples);
                assert(actual.Pitch() == reference.pitch && actual.PaddedHeight() == reference.height);
                assert(actual.BlockWidth() == reference.blockWidth && actual.BlockHeight() == reference.blockHeight);
                assert(actual.BlocksPerRow() == reference.pitch / reference.blockWidth);
                assert(actual.Samples() == samples && actual.ElementBytes() == bytes);
                auto explicitPitch = parameters;
                explicitPitch.pitchInElements = reference.pitch;
                const DepthTargetLayout pitched(width, height, plane, explicitPitch);
                assert(pitched.Bytes() == actual.Bytes());
                explicitPitch.pitchInElements = reference.pitch + 1u;
                rejects([&] { DepthTargetLayout(width, height, plane, explicitPitch); });
                explicitPitch.pitchInElements = reference.pitch + reference.blockWidth;
                rejects([&] { DepthTargetLayout(width, height, plane, explicitPitch); });
                if (width == 1920u && samples == 8u) {
                    std::printf("AMD-reference eight-sample %s extent=%ux%u pitch=%u padded_height=%u bytes=%zu alignment=%zu\n",
                                plane == DepthPlane::Stencil8 ? "stencil8" : bytes == 2u ? "depth16" : "depth32",
                                width, height, actual.Pitch(), actual.PaddedHeight(), actual.Bytes(), actual.Alignment());
                }
                ++layouts;
                ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT coord{};
                coord.size = sizeof(coord);
                coord.flags = info.flags;
                coord.swizzleMode = info.swizzleMode;
                coord.resourceType = info.resourceType;
                coord.bpp = info.bpp;
                coord.unalignedWidth = width;
                coord.unalignedHeight = height;
                coord.numSlices = coord.numMipLevels = 1;
                coord.numSamples = coord.numFrags = samples;
                const auto check = [&](unsigned x, unsigned y, unsigned sample) {
                    coord.x = x;
                    coord.y = y;
                    coord.sample = sample;
                    ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT expected{};
                    expected.size = sizeof(expected);
                    assert(Addr2ComputeSurfaceAddrFromCoord(library.hLib, &coord, &expected) == ADDR_OK);
                    const auto actualOffset = actual.Offset(x, y, sample);
                    if (expected.bitPosition != 0 || actualOffset != expected.addr) {
                        std::fprintf(stderr, "address mismatch bytes=%u samples=%u extent=%ux%u xy=%u,%u sample=%u actual=%zu reference=%llu\n",
                                     bytes, samples, width, height, x, y, sample, actualOffset,
                                     static_cast<unsigned long long>(expected.addr));
                        std::abort();
                    }
                    ++addresses;
                };
                if (width <= 257u) {
                    std::vector<std::byte> linear(actual.LinearBytes());
                    std::vector<std::byte> tiled(actual.Bytes(), std::byte{0x5a});
                    std::vector<std::byte> result(linear.size());
                    std::vector<bool> visited(tiled.size() / bytes);
                    for (unsigned y = 0; y < height; ++y) {
                        for (unsigned x = 0; x < width; ++x) {
                            for (unsigned sample = 0; sample < samples; ++sample) {
                                check(x, y, sample);
                                const auto offset = actual.Offset(x, y, sample);
                                assert(offset % bytes == 0 && offset + bytes <= tiled.size() && !visited[offset / bytes]);
                                visited[offset / bytes] = true;
                                const auto index = ((static_cast<std::size_t>(y) * width + x) * samples + sample) * bytes;
                                for (unsigned byte = 0; byte < bytes; ++byte) {
                                    linear[index + byte] = std::byte((y * 11u + x * 31u + sample * 17u + byte * 7u) & 255u);
                                }
                            }
                        }
                    }
                    actual.Tile(linear, tiled);
                    actual.Detile(tiled, result);
                    assert(result == linear);
                    for (std::size_t i = 0; i < tiled.size(); ++i) {
                        if (!visited[i / bytes]) assert(tiled[i] == std::byte{0x5a});
                    }
                    rejects([&] { actual.Tile(std::span(linear).first(linear.size() - 1), tiled); });
                    rejects([&] { actual.Detile(std::span(tiled).first(tiled.size() - 1), result); });
                    ++roundTrips;
                } else {
                    // Check every pixel/sample of the actual required Z32/S8 target.
                    if (width == 1920u && samples == 8u && (bytes == 1u || bytes == 4u)) {
                        for (unsigned y = 0; y < height; ++y) {
                            for (unsigned x = 0; x < width; ++x) {
                                for (unsigned sample = 0; sample < samples; ++sample) check(x, y, sample);
                            }
                        }
                    }
                    // Include exact macroblock boundaries and far-edge coordinates.
                    for (unsigned sample = 0; sample < samples; ++sample) {
                        check(0, 0, sample);
                        check(width - 1, height - 1, sample);
                        check(reference.blockWidth, reference.blockHeight, sample);
                        check(reference.blockWidth - 1, reference.blockHeight - 1, sample);
                    }
                    unsigned random = 0x21465837u;
                    for (unsigned n = 0; n < 4096; ++n) {
                        random = random * 1664525u + 1013904223u;
                        const auto x = random % width;
                        random = random * 1664525u + 1013904223u;
                        const auto y = random % height;
                        check(x, y, n % samples);
                    }
                }
                rejects([&] { actual.Offset(width, 0); });
                rejects([&] { actual.Offset(0, height); });
                rejects([&] { actual.Offset(0, 0, samples); });
            }
        }
    }
    for (const auto samples : {0u, 3u, 16u}) {
        rejects([&] { DepthTargetLayout(2, 2, DepthPlane::Depth32, {.samples = samples, .fragments = samples}); });
    }
    for (const auto plane : {static_cast<DepthPlane>(0u), static_cast<DepthPlane>(3u), static_cast<DepthPlane>(8u)}) {
        rejects([&] { DepthTargetLayout(2, 2, plane); });
    }
    for (const auto extent : {std::array{0u, 1u}, std::array{1u, 0u}, std::array{16385u, 1u}, std::array{1u, 16385u}}) {
        rejects([&] { DepthTargetLayout(extent[0], extent[1], DepthPlane::Stencil8); });
    }
    rejects([&] { DepthTargetLayout(2, 2, DepthPlane::Depth32, {.samples = 8, .fragments = 4}); });
    rejects([&] { DepthTargetLayout(2, 2, DepthPlane::Depth32, {.samples = 4, .fragments = 0}); });
    for (const auto mode : {0u, 16u, 25u, 27u, 0xffffffffu}) {
        rejects([&] { DepthTargetLayout(2, 2, DepthPlane::Depth32, {.swizzleMode = mode}); });
    }
    rejects([&] { DepthTargetLayout(2, 2, DepthPlane::Depth32, {.pipeBankXor = 1}); });
    rejects([&] { DepthTargetLayout(2, 2, DepthPlane::Depth32, {.compressionEnabled = true}); });
    assert(AddrDestroy(library.hLib) == ADDR_OK);
    std::printf("AMD-reference depth_stencil layouts=%zu addresses=%zu sample_round_trips=%zu errors=0\n",
                layouts, addresses, roundTrips);
}
