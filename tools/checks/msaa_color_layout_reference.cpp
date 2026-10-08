// Verify the production color layout against the independently linked AMD AddrLib.
#include "prx/libSceAgcDriver/Graphics/include/ColorTargetLayout.hpp"
#include <addrinterface.h>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include <vector>

using AgcDriver::Graphics::ColorTargetLayout;
using AgcDriver::Graphics::ColorTileMode;

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
    throw std::runtime_error("invalid color layout accepted");
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
    for (const auto bytes : {1u, 2u, 4u, 8u, 16u}) {
        for (const auto samples : {1u, 2u, 4u, 8u}) {
            for (const auto extent : {std::array{1u, 1u}, std::array{257u, 129u},
                                     std::array{3840u, 2160u}, std::array{16384u, 16384u}}) {
                const auto width = extent[0], height = extent[1];
                const ColorTargetLayout actual(width, height, ColorTileMode::RenderTarget, bytes, samples);
                ADDR2_COMPUTE_SURFACE_INFO_INPUT info{};
                info.size = sizeof(info);
                info.flags.color = 1;
                info.swizzleMode = ADDR_SW_64KB_R_X;
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
                assert(actual.BlocksPerRow() == reference.pitch / reference.blockWidth);
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
        rejects([&] { ColorTargetLayout(2, 2, ColorTileMode::RenderTarget, 4, samples); });
    }
    for (const auto mode : {ColorTileMode::Linear, ColorTileMode::Standard4KB, ColorTileMode::Standard64KB}) {
        rejects([&] { ColorTargetLayout(2, 2, mode, 4, 8); });
    }
    assert(AddrDestroy(library.hLib) == ADDR_OK);
    std::printf("AMD-reference color layouts=%zu addresses=%zu sample_round_trips=%zu errors=0\n",
                layouts, addresses, roundTrips);
}
