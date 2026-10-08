// Original fixtures. Actual production state decoder, shader factory and tiled
// transfers are linked; the native macOS memory boundary is an explicit adapter.
// This is neither GPU execution nor Wine GuestMemory runtime qualification.
#include "prx/libSceAgcDriver/Graphics/include/ColorResolve.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ColorTargetTransfer.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestTextureResource.hpp"
#include <spirv/unified1/spirv.hpp>
#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

using namespace AgcDriver;
using namespace AgcDriver::Graphics;
namespace {
unsigned cases = 0, values = 0;
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
template<class F> void rejects(F action, const char* reason) {
    ++cases;
    try { action(); } catch (const std::runtime_error& e) {
        check(std::string(e.what()).find(reason) != std::string::npos, e.what()); return;
    }
    throw std::runtime_error(std::string("missing rejection: ") + reason);
}
struct Allocation {
    std::shared_ptr<std::byte> memory;
    std::size_t bytes;
    explicit Allocation(std::size_t size) : bytes(size) {
        void* raw = nullptr; check(posix_memalign(&raw, 65536, size) == 0, "test allocation failed");
        memory = {static_cast<std::byte*>(raw), std::free};
        std::memset(memory.get(), 0x5a, bytes);
    }
    std::uint64_t address() const { return reinterpret_cast<std::uintptr_t>(memory.get()); }
};
ColorTarget color(unsigned width, unsigned height, unsigned samples, std::uint64_t address) {
    ColorTarget result{};
    result.address = result.surfaceAddress = address;
    result.extent = result.surfaceExtent = {width, height};
    result.samples = result.fragments = samples;
    result.tileMode = ColorTileMode::RenderTarget;
    result.format = VK_FORMAT_R8G8B8A8_UNORM;
    result.componentMapping = 0xe4;
    result.elementBytes = 4;
    result.depth = result.mipCount = 1;
    result.bytes = ColorTargetLayout(width, height, result.tileMode, 4, samples).Bytes();
    return result;
}
void writeColor(QueueState& q, unsigned slot, const ColorTarget& c) {
    const unsigned first = 0x318 + 15u * slot;
    for (unsigned i = 0; i < 15; ++i) q.context[first + i] = 0;
    q.context[first] = static_cast<unsigned>(c.address >> 8u);
    q.context[first + 4] = 0x8028;
    const auto log = std::countr_zero(c.samples);
    q.context[first + 5] = (log << 12u) | (log << 15u);
    q.context[0x390 + slot] = static_cast<unsigned>(c.address >> 40u);
    q.context[0x3a8 + slot] = 0;
    q.context[0x3b0 + slot] = ((c.extent.width - 1u) << 14u) | (c.extent.height - 1u);
    q.context[0x3b8 + slot] = 0x9000000u | (27u << 14u);
}
QueueState queue(const ColorResolvePass& pass) {
    QueueState q;
    q.context = {
        {0x2d5, 0x2000}, {0x1b6, 0}, {0x207, 0}, {0x200, 0}, {0x203, 0x10},
        {0x2dc, 0xaa00}, {0x292, 0}, {0x293, 0}, {0x80, 0}, {0x8d, 0}, {0x83, 0xffff}, {0x8c, 0xa},
        {0x2f9, 0x2d}, {0x313, 0x6000}, {0x30e, 0xffffffff}, {0x30f, 0xffffffff},
        {0x206, 0x43f}, {0x204, 0x80000}, {0x205, 0x240},
        {0x8e, 0xf}, {0x8f, 0xf}, {0x202, 0xcc0030}, {0x1c4, 0}, {0x1c5, 4}, {0x1c3, 4}, {0x1e0, 0},
        {0xc, 0}, {0xd, 0x7fff7fff}, {0x81, 0x80000000}, {0x82, 0x7fff7fff},
        {0x90, 0x80000000}, {0x91, 0x7fff7fff}, {0x94, 0x80000000}, {0x95, 0x7fff7fff},
        {0x000, 0x20}, {0x10b, 0x333}, {0x10c, 0xffffff00}, {0x1b3, 0}, {0x1b4, 0}
    };
    q.userConfig[0x242] = 7; q.userConfig[0x24b] = 0;
    q.shader[0x008] = 1; q.shader[0x009] = 0;
    const unsigned log = std::countr_zero(pass.source.samples);
    q.context[0x201] = log | (log << 4u) | (log << 8u) | (log << 12u) | (1u << 20u);
    q.context[0x2f8] = log | (7u << 13u) | (log << 20u);
    for (unsigned pixel = 0; pixel < 4; ++pixel) {
        q.context[0x2fe + 4u * pixel] = 0x6ef32a95u;
        q.context[0x2ff + 4u * pixel] = 0x731fd6b1u;
    }
    writeColor(q, 0, pass.source); writeColor(q, 1, pass.destination);
    for (const auto [offset, number] : std::array<std::pair<unsigned, float>, 8>{{
        {0x10f, pass.source.extent.width / 2.0f}, {0x110, pass.source.extent.width / 2.0f},
        {0x111, -static_cast<float>(pass.source.extent.height) / 2.0f}, {0x112, pass.source.extent.height / 2.0f},
        {0x113, 1}, {0x114, 0}, {0xb4, 0}, {0xb5, 1}}}) q.context[offset] = std::bit_cast<unsigned>(number);
    return q;
}
void factory(const ColorTarget& source, const std::filesystem::path& output) {
    using namespace ShaderRecompiler;
    for (const unsigned binding : {0u, 6u}) {
        RecompileResult vs{};
        if (binding) { DescriptorBinding used{}; used.binding = binding - 1; vs.bindings.push_back(used); }
        const auto result = BuildColorResolveFragment(source, vs);
        check(result.bindings.size() == 1, "resolve descriptor count");
        const auto& b = result.bindings.front();
        check(b.binding == binding && b.descriptorSet == 0 && b.count == 1 && b.kind == DescriptorKind::SampledImage && b.role == DescriptorRole::GuestImages,
              "resolve descriptor identity");
        check(b.imageShape == DescriptorImageShape::Image2DMsaaArray && b.imageLogicalSamples == std::vector{source.samples} &&
              b.imageNativeSamples == std::vector{std::min(source.samples, 4u)} && b.imageWritten == std::vector{false}, "resolve sample metadata");
        const auto decoded = DecodeTextureResource(b.guestDescriptor);
        check(decoded.baseAddress == source.address && decoded.width == source.extent.width && decoded.height == source.extent.height &&
              decoded.samples == source.samples && decoded.fragments == source.samples && decoded.mipCount == 1 && decoded.lastLevel == 0,
              "resolve descriptor production decode");
        unsigned found = 0;
        for (std::size_t i = 5; i < result.spirv.size();) {
            const auto n = result.spirv[i] >> 16u;
            check(n > 0 && n <= result.spirv.size() - i, "malformed resolve SPIRV");
            if ((result.spirv[i] & 0xffffu) == spv::OpDecorate && n == 4 && result.spirv[i + 2] == spv::DecorationBinding) {
                check(result.spirv[i + 3] == binding, "resolve embedded binding differs"); ++found;
            }
            i += n;
        }
        check(found == 1, "resolve SPIRV binding count");
        if (!output.empty()) {
            std::filesystem::create_directories(output);
            std::ofstream file(output / ("factory-" + std::to_string(source.samples) + "-" + std::to_string(binding) + ".spv"), std::ios::binary);
            file.write(reinterpret_cast<const char*>(result.spirv.data()), result.spirv.size() * 4);
            check(file.good(), "factory SPIRV output failed");
        }
        ++cases;
    }
}
void transfers(unsigned width, unsigned height, unsigned samples, const std::filesystem::path& output) {
    const auto sourceBytes = ColorTargetLayout(width, height, ColorTileMode::RenderTarget, 4, samples).Bytes();
    Allocation backing(sourceBytes + 65536);
    // Shared base deliberately aliases source/destination allocation. Copying
    // source into linear storage before any destination write preserves input.
    const ColorResolvePass pass{color(width, height, samples, backing.address()), color(width, height, 1, backing.address())};
    ValidateColorResolve(pass);
    std::vector<std::byte> source(static_cast<std::size_t>(width) * height * samples * 4u), destination(static_cast<std::size_t>(width) * height * 4u);
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(width) * height; ++pixel)
        for (unsigned sample = 0; sample < samples; ++sample)
            for (unsigned channel = 0; channel < 4; ++channel)
                source[(pixel * samples + sample) * 4 + channel] = static_cast<std::byte>((pixel * 17 + sample * 37 + channel * 43) & 255);
    // All byte values in every sample; independent known cases include exact
    // means, half-rounding boundaries and sensitivity to the final logical layer.
    for (unsigned value = 0; value < 256 && value < width * height; ++value)
        for (unsigned sample = 0; sample < samples; ++sample)
            for (unsigned channel = 0; channel < 4; ++channel)
                source[(value * samples + sample) * 4 + channel] = static_cast<std::byte>(value);
    WriteColorTarget(pass.source, source);
    std::vector<std::byte> captured(source.size()); ReadColorTarget(pass.source, captured);
    check(source == captured, "production multisample transfer changed sample bytes");
    ColorResolveReference(pass, captured, destination);
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(width) * height; ++pixel)
        for (unsigned channel = 0; channel < 4; ++channel) {
            double sum = 0; for (unsigned sample = 0; sample < samples; ++sample) sum += std::to_integer<unsigned>(source[(pixel * samples + sample) * 4 + channel]);
            check(std::to_integer<unsigned>(destination[pixel * 4 + channel]) == static_cast<unsigned>(std::floor(sum / samples + 0.5)), "independent scalar mean mismatch"); ++values;
        }
    const ColorTargetLayout layout(width, height, ColorTileMode::RenderTarget, 4, 1);
    const auto before = std::vector<std::byte>(backing.memory.get(), backing.memory.get() + backing.bytes);
    WriteColorTarget(pass.destination, destination);
    std::vector<std::byte> resolved(destination.size()); ReadColorTarget(pass.destination, resolved);
    check(resolved == destination, "aliased destination production round trip differs");
    std::vector<bool> touched(backing.bytes, false);
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x)
        for (unsigned channel = 0; channel < 4; ++channel) touched[layout.Offset(x, y, 0) + channel] = true;
    for (std::size_t i = 0; i < backing.bytes; ++i)
        if (!touched[i]) check(backing.memory.get()[i] == before[i], "destination padding, alias suffix or guard changed");
    // Every sample must contribute; e.g. 255 in only last logical sample.
    std::fill(captured.begin(), captured.end(), std::byte{});
    captured[(samples - 1) * 4] = std::byte{255};
    ColorResolveReference(pass, captured, destination);
    check(std::to_integer<unsigned>(destination[0]) == (255u + samples / 2u) / samples, "last logical sample ignored");
    for (unsigned numerator = 0; numerator <= samples; ++numerator) {
        std::fill(captured.begin(), captured.end(), std::byte{});
        for (unsigned sample = 0; sample < numerator; ++sample) captured[sample * 4] = std::byte{1};
        ColorResolveReference(pass, captured, destination);
        check(std::to_integer<unsigned>(destination[0]) == (numerator >= samples / 2u), "half-rounding boundary changed");
    }
    const auto q = queue(pass);
    const auto decoded = DecodeColorResolvePass(q); check(decoded.has_value(), "resolve mode not routed");
    check(decoded->source.samples == samples && decoded->destination.samples == 1, "resolve targets misdecoded");
    const auto raster = ColorResolveRasterQueue(q, *decoded);
    const auto state = DecodeState(raster, true);
    check(state.rectList && state.samples.count == 1 && state.colors.size() == 1 && state.color.samples == 1 && state.color.address == pass.destination.address,
          "resolve normalized raster mismatch");
    check(!state.depthTest && !state.depthWrite && !state.stencilTest && state.viewport.height < 0 && state.scissor.extent.width == width && state.scissor.extent.height == height,
          "resolve raster geometry or depth changed");
    auto normal = q; normal.context[0x202] = 0xcc0010; check(!DecodeColorResolvePass(normal), "normal draw mistaken for resolve");
    auto sparse = q;
    for (const auto offset : {0x328u, 0x329u, 0x32du, 0x32eu, 0x32fu, 0x330u, 0x331u, 0x332u, 0x333u, 0x334u, 0x335u, 0x3a9u}) sparse.context.erase(offset);
    const auto sparsePass = DecodeColorResolvePass(sparse);
    check(sparsePass.has_value() && DecodeState(ColorResolveRasterQueue(sparse, *sparsePass), true).color.address == pass.destination.address,
          "unused resolve destination words incorrectly required");
    auto modified = q; modified.context[0x292] = 1; check(DecodeColorResolvePass(modified).has_value(), "resolve unnecessarily depends on raster MSAA_ENABLE");
    factory(pass.source, output);
    ++cases;
    const auto rejectQueue = [&](unsigned reg, unsigned value, const char* reason) { auto altered = q; altered.context[reg] = value; rejects([&] { DecodeColorResolvePass(altered); }, reason); };
    rejectQueue(0x202, 0x0030, "standard ROP"); rejectQueue(0x200, 1, "depth or stencil");
    rejectQueue(0x8e, 3, "all four"); rejectQueue(0x8f, 0xff, "all four");
    rejectQueue(0x292, 4, "raster flags"); rejectQueue(0x32b, 0x48028, "truncating");
    rejectQueue(0x3b1, ((width - 1) << 14) | height, "dimensions disagree");
    rejectQueue(0x32c, 1u << 12u | 1u << 15u, "single-sampled");
    rejectQueue(0x31d, std::countr_zero(samples) << 12u, "EQAA");
    rejectQueue(0x201, 0, "EQAA");
    rejectQueue(0x31c, 0x10008028, "multisampled DCC");
    rejectQueue(0x31c, 0xa028, "multisampled CMASK");
    modified = q; modified.userConfig[0x242] = 4; rejects([&] { DecodeColorResolvePass(modified); }, "rect-list");
    auto invalid = pass; invalid.destination.format = VK_FORMAT_R8G8B8A8_SRGB; rejects([&] { ValidateColorResolve(invalid); }, "RGBA8_UNORM");
    invalid = pass; invalid.destination.componentMapping ^= 1; rejects([&] { ValidateColorResolve(invalid); }, "RGBA8_UNORM");
    invalid = pass; invalid.destination.tileMode = ColorTileMode::Linear; rejects([&] { ValidateColorResolve(invalid); }, "R64KB_X");
    invalid = pass; invalid.source.bytes--; rejects([&] { ValidateColorResolve(invalid); }, "allocation size");
    invalid = pass; invalid.destination.address++; rejects([&] { ValidateColorResolve(invalid); }, "guest range");
    invalid = pass; invalid.destination.depthSlice = 1; rejects([&] { ValidateColorResolve(invalid); }, "array or volume");
    invalid = pass; invalid.destination.dccAddress = 65536; rejects([&] { ValidateColorResolve(invalid); }, "compressed");
    std::fill(destination.begin(), destination.end(), std::byte{0x63});
    const auto unchanged = destination;
    rejects([&] { ColorResolveReference(invalid, captured, destination); }, "compressed");
    check(destination == unchanged, "rejected resolve changed destination");
}
void privateRegisters(const char* path) {
    QueueState q; q.context.clear(); q.shader.clear(); q.userConfig.clear();
    std::ifstream input(path); check(input.good(), "private register fixture not found");
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream row(line); std::string bank; unsigned offset, value;
        if (!(row >> bank >> std::hex >> offset >> value)) continue;
        if (bank == "context") q.context[offset] = value;
        if (bank == "shader") q.shader[offset] = value;
        if (bank == "user-config" || bank == "uconfig") q.userConfig[offset] = value;
    }
    Allocation source(96u * 1024u * 1024u), dest(16u * 1024u * 1024u);
    q.context[0x318] = static_cast<unsigned>(source.address() >> 8u); q.context[0x390] = static_cast<unsigned>(source.address() >> 40u);
    q.context[0x327] = static_cast<unsigned>(dest.address() >> 8u); q.context[0x391] = static_cast<unsigned>(dest.address() >> 40u);
    const auto pass = DecodeColorResolvePass(q);
    check(pass && pass->source.samples == 8 && pass->destination.samples == 1 && pass->source.extent.width == 1920 && pass->source.extent.height == 1080,
          "private captured resolve not decoded");
    const auto raster = DecodeState(ColorResolveRasterQueue(q, *pass), true);
    check(raster.samples.count == 1 && raster.color.address == dest.address() && raster.rectList, "private captured resolve raster differs");
    std::puts("Private captured CB_RESOLVE state decoded with test-owned memory; no game image or shader copied.");
}
}
namespace AgcDriver::Graphics {
std::uint64_t DepthSliceBytes(VkExtent2D, std::uint32_t) { throw std::logic_error("unexpected legacy depth slice"); }
}
int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1 ? argv[1] : "";
        for (const unsigned samples : {2u, 4u, 8u})
            for (const auto [width, height] : std::array<std::pair<unsigned, unsigned>, 2>{{{17, 19}, {257, 129}}}) transfers(width, height, samples, output);
        if (argc > 2) privateRegisters(argv[2]);
        std::printf("Production CB resolve state/factory + adapted native transfers: %u cases; %u scalar channel values; samples2/4/8, all256 values, rounding, alias guards and rejections passed. No GPU/runtime claim.\n", cases, values);
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}
