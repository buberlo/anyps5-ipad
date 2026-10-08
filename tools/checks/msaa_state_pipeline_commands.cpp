// Original synthetic state and explicit Vulkan recorder. Actual production
// State.cpp/Pipeline.cpp are tested; this does not run shader code or a GPU.
#include "prx/libSceAgcDriver/Graphics/include/Pipeline.hpp"
#include "prx/libSceAgcDriver/Graphics/include/VertexInput.hpp"
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

using namespace AgcDriver;
using namespace AgcDriver::Graphics;

namespace {
alignas(256) std::array<std::byte, 1024 * 1024 + 65536> colorMemory{};
unsigned cases = 0, pipelines = 0, live = 0;
std::uintptr_t nextHandle = 1;
bool failPipeline = false;
struct Recorded {
    unsigned native = 0;
    unsigned mask = 0;
    bool custom = false, initial = false, after = false;
    unsigned depthIndex = 0;
    std::vector<VkSampleLocationEXT> positions;
    std::vector<VkSampleCountFlagBits> attachments;
} recorded;

template<class T> T handle() { ++live; return reinterpret_cast<T>(nextHandle++); }
template<class F> void rejects(F action, const char* reason) {
    ++cases;
    try { action(); }
    catch (const std::runtime_error& error) {
        if (std::string(error.what()).find(reason) == std::string::npos) throw std::runtime_error(std::string("wanted ") + reason + "; got " + error.what());
        return;
    }
    throw std::runtime_error(std::string("missing rejection: ") + reason);
}

VkResult VKAPI_CALL createModule(VkDevice, const VkShaderModuleCreateInfo*, const VkAllocationCallbacks*, VkShaderModule* out) { *out = handle<VkShaderModule>(); return VK_SUCCESS; }
void VKAPI_CALL destroyModule(VkDevice, VkShaderModule, const VkAllocationCallbacks*) { --live; }
VkResult VKAPI_CALL createLayout(VkDevice, const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*, VkPipelineLayout* out) { *out = handle<VkPipelineLayout>(); return VK_SUCCESS; }
void VKAPI_CALL destroyLayout(VkDevice, VkPipelineLayout, const VkAllocationCallbacks*) { --live; }
VkResult VKAPI_CALL createPass(VkDevice, const VkRenderPassCreateInfo* info, const VkAllocationCallbacks*, VkRenderPass* out) {
    recorded.attachments.clear();
    for (unsigned i = 0; i < info->attachmentCount; ++i) recorded.attachments.push_back(info->pAttachments[i].samples);
    assert(info->subpassCount == 1 && info->pSubpasses[0].pDepthStencilAttachment != nullptr);
    assert(info->pSubpasses[0].pDepthStencilAttachment->attachment == 1);
    *out = handle<VkRenderPass>(); return VK_SUCCESS;
}
void VKAPI_CALL destroyPass(VkDevice, VkRenderPass, const VkAllocationCallbacks*) { --live; }
VkResult VKAPI_CALL createPipeline(VkDevice, VkPipelineCache, std::uint32_t count, const VkGraphicsPipelineCreateInfo* info, const VkAllocationCallbacks*, VkPipeline* out) {
    ++pipelines;
    assert(count == 1);
    const auto& samples = *info->pMultisampleState;
    recorded.native = samples.rasterizationSamples;
    recorded.mask = samples.pSampleMask ? *samples.pSampleMask : 1;
    recorded.custom = samples.pNext != nullptr;
    recorded.positions.clear();
    assert(samples.sampleShadingEnable == VK_FALSE && samples.alphaToCoverageEnable == VK_FALSE);
    if (samples.pNext) {
        const auto& positions = *static_cast<const VkPipelineSampleLocationsStateCreateInfoEXT*>(samples.pNext);
        assert(positions.sType == VK_STRUCTURE_TYPE_PIPELINE_SAMPLE_LOCATIONS_STATE_CREATE_INFO_EXT);
        assert(positions.sampleLocationsEnable == VK_TRUE);
        const auto& locations = positions.sampleLocationsInfo;
        assert(locations.sampleLocationsPerPixel == samples.rasterizationSamples);
        assert(locations.sampleLocationGridSize.width == 1 && locations.sampleLocationGridSize.height == 1);
        assert(locations.sampleLocationsCount == recorded.native);
        recorded.positions.assign(locations.pSampleLocations, locations.pSampleLocations + locations.sampleLocationsCount);
    }
    if (failPipeline) return VK_ERROR_INITIALIZATION_FAILED;
    *out = handle<VkPipeline>(); return VK_SUCCESS;
}
void VKAPI_CALL destroyPipeline(VkDevice, VkPipeline, const VkAllocationCallbacks*) { --live; }
VkResult VKAPI_CALL createFramebuffer(VkDevice, const VkFramebufferCreateInfo* info, const VkAllocationCallbacks*, VkFramebuffer* out) {
    assert(info->attachmentCount == 2 && info->layers == 1);
    *out = handle<VkFramebuffer>(); return VK_SUCCESS;
}
void VKAPI_CALL destroyFramebuffer(VkDevice, VkFramebuffer, const VkAllocationCallbacks*) { --live; }
void VKAPI_CALL beginPass(VkCommandBuffer, const VkRenderPassBeginInfo* info, VkSubpassContents) {
    recorded.initial = recorded.after = false;
    if (!info->pNext) { assert(recorded.native == 1); return; }
    const auto& locations = *static_cast<const VkRenderPassSampleLocationsBeginInfoEXT*>(info->pNext);
    assert(locations.sType == VK_STRUCTURE_TYPE_RENDER_PASS_SAMPLE_LOCATIONS_BEGIN_INFO_EXT);
    assert(locations.attachmentInitialSampleLocationsCount == 1 && locations.postSubpassSampleLocationsCount == 1);
    const auto& initial = locations.pAttachmentInitialSampleLocations[0];
    const auto& after = locations.pPostSubpassSampleLocations[0];
    recorded.depthIndex = initial.attachmentIndex;
    assert(initial.attachmentIndex == 1 && after.subpassIndex == 0);
    for (const auto* positions : {&initial.sampleLocationsInfo, &after.sampleLocationsInfo}) {
        assert(positions->sampleLocationsPerPixel == recorded.native && positions->sampleLocationsCount == recorded.native);
        for (unsigned i = 0; i < recorded.native; ++i) {
            assert(positions->pSampleLocations[i].x == recorded.positions[i].x);
            assert(positions->pSampleLocations[i].y == recorded.positions[i].y);
        }
    }
    recorded.initial = recorded.after = true;
}
void VKAPI_CALL bindPipeline(VkCommandBuffer, VkPipelineBindPoint, VkPipeline) {}
void VKAPI_CALL viewport(VkCommandBuffer, std::uint32_t, std::uint32_t, const VkViewport*) {}
void VKAPI_CALL scissor(VkCommandBuffer, std::uint32_t, std::uint32_t, const VkRect2D*) {}
void VKAPI_CALL depthBias(VkCommandBuffer, float, float, float) {}
void VKAPI_CALL depthBounds(VkCommandBuffer, float, float) {}
PFN_vkVoidFunction VKAPI_CALL proc(VkDevice, const char* name) {
#define ENTRY(n, f) if (std::strcmp(name, n) == 0) return reinterpret_cast<PFN_vkVoidFunction>(f)
    ENTRY("vkCreateShaderModule", createModule); ENTRY("vkDestroyShaderModule", destroyModule);
    ENTRY("vkCreatePipelineLayout", createLayout); ENTRY("vkDestroyPipelineLayout", destroyLayout);
    ENTRY("vkCreateRenderPass", createPass); ENTRY("vkDestroyRenderPass", destroyPass);
    ENTRY("vkCreateGraphicsPipelines", createPipeline); ENTRY("vkDestroyPipeline", destroyPipeline);
    ENTRY("vkCreateFramebuffer", createFramebuffer); ENTRY("vkDestroyFramebuffer", destroyFramebuffer);
    ENTRY("vkCmdBeginRenderPass", beginPass); ENTRY("vkCmdBindPipeline", bindPipeline);
    ENTRY("vkCmdSetViewport", viewport); ENTRY("vkCmdSetScissor", scissor);
    ENTRY("vkCmdSetDepthBias", depthBias); ENTRY("vkCmdSetDepthBounds", depthBounds);
#undef ENTRY
    throw std::runtime_error(std::string("unmocked Vulkan function: ") + name);
}

Context context() {
    Context result{};
    result.device = reinterpret_cast<VkDevice>(0x9000);
    result.deviceProc = proc;
    result.limits.maxColorAttachments = 8;
    result.limits.maxFramebufferWidth = result.limits.maxFramebufferHeight = 4096;
    result.limits.framebufferNoAttachmentsSampleCounts = 7;
    result.limits.framebufferColorSampleCounts = result.limits.framebufferDepthSampleCounts = result.limits.framebufferStencilSampleCounts = 7;
    result.sampleLocations = true;
    result.sampleLocationProperties.sampleLocationSampleCounts = 7;
    result.sampleLocationProperties.maxSampleLocationGridSize = {1, 1};
    result.sampleLocationProperties.sampleLocationSubPixelBits = 4;
    result.sampleLocationProperties.sampleLocationCoordinateRange[0] = 0;
    result.sampleLocationProperties.sampleLocationCoordinateRange[1] = 1;
    return result;
}

QueueState state(unsigned samples) {
    QueueState q;
    q.context = {
        {0x2d5, 0x2000}, {0x1b6, 0}, {0x207, 0}, {0x200, 0x771}, {0x203, 0x10},
        {0x2dc, 0xaa00}, {0x292, 2}, {0x293, 0}, {0x80, 0}, {0x8d, 0}, {0x83, 0xffff}, {0x8c, 0xa},
        {0x2f9, 0x2d}, {0x313, 0x6000}, {0x30e, 0xffffffff}, {0x30f, 0xffffffff},
        {0x206, 0x43f}, {0x204, 0x80000}, {0x205, 0x240},
        {0x8e, 0xf}, {0x8f, 0xf}, {0x202, 0xcc0010}, {0x1c4, 0}, {0x1c5, 9}, {0x1c3, 4},
        {0x31c, 0x8028}, {0x31b, 0}, {0x3b0, (63u << 14u) | 63u}, {0x3b8, 0x9000000u | (27u << 14u)}, {0x1e0, 0},
        {0xc, 0}, {0xd, 0x400040}, {0x81, 0x80000000}, {0x82, 0x400040},
        {0x90, 0x80000000}, {0x91, 0x400040}, {0x94, 0x80000000}, {0x95, 0x400040},
        {0x000, 0x22}, {0x002, 0}, {0x007, 0x003f003f}, {0x00a, 0}, {0x00b, 0x3f800000},
        {0x011, 0x20000181}, {0x012, 0x2000}, {0x013, 0x3000}, {0x014, 0x2000}, {0x015, 0x3000},
        {0x10b, 0x333}, {0x10c, 0xffffff00},
        {0x1b3, 0}, {0x1b4, 0}
    };
    q.userConfig[0x242] = 4;
    q.shader[0x008] = 1; q.shader[0x009] = 0;
    const unsigned log = std::countr_zero(samples);
    q.context[0x292] = samples == 1 ? 2u : 3u;
    q.context[0x010] = 3u | (log << 2u) | (24u << 4u);
    q.context[0x201] = log | (log << 4u) | (log << 8u) | (log << 12u) | (1u << 20u);
    q.context[0x2f8] = samples == 1 ? 0u : log | (7u << 13u) | (log << 20u);
    q.context[0x31d] = (log << 12u) | (log << 15u);
    for (unsigned pixel = 0; pixel < 4; ++pixel) {
        // Original synthetic nibble coordinates, separate from game data.
        q.context[0x2fe + 4u * pixel] = 0x6ef32a95u;
        q.context[0x2ff + 4u * pixel] = 0x731fd6b1u;
    }
    const auto address = (reinterpret_cast<std::uintptr_t>(colorMemory.data()) + 65535u) & ~std::uintptr_t{65535u};
    q.context[0x318] = static_cast<std::uint32_t>(address >> 8u);
    q.context[0x390] = static_cast<std::uint32_t>(address >> 40u);
    for (const auto [offset, number] : std::array<std::pair<unsigned, float>, 8>{{{0x10f, 32}, {0x110, 32}, {0x111, 32}, {0x112, 32}, {0x113, 1}, {0x114, 0}, {0xb4, 0}, {0xb5, 1}}})
        q.context[offset] = std::bit_cast<unsigned>(number);
    return q;
}

void checkState() {
    for (unsigned count : {1u, 2u, 4u, 8u}) {
        auto q = state(count);
        std::vector<RegisterRead> reads;
        RegisterReadLog() = &reads;
        const auto decoded = DecodeState(q, true);
        const auto reason = DrawRejection(q, false, true);
        RegisterReadLog() = nullptr;
        assert(reason.empty());
        assert(decoded.samples.count == count && decoded.samples.nativeCount == std::min(count, 4u) && decoded.samples.groups == (count == 8 ? 2 : 1));
        assert(decoded.color.samples == count && decoded.depth->samples == count);
        const std::array<std::array<unsigned, 2>, 8> reference{{{13, 1}, {2, 10}, {11, 7}, {6, 14}, {9, 3}, {14, 5}, {7, 9}, {11, 15}}};
        if (count > 1) for (unsigned sample = 0; sample < count; ++sample) {
            assert(decoded.samples.positions[sample].x == reference[sample][0] / 16.0f);
            assert(decoded.samples.positions[sample].y == reference[sample][1] / 16.0f);
        }
        for (auto read : reads) assert(DrawKeyCovers(read));
        if (count == 1) assert(DecodeState(q).samples.count == 1);
        else {
            assert(!DrawRejection(q, false).empty());
            rejects([&] { DecodeState(q); }, "multisampled");
        }
        ++cases;
    }
    auto q = state(8);
    auto change = [&](unsigned offset, unsigned value, const char* reason) {
        auto modified = q; modified.context[offset] = value;
        rejects([&] { DecodeState(modified, true); }, reason);
    };
    change(0x2f8, 4u | (4u << 20u), "above eight");
    change(0x2f8, 3u | (2u << 20u), "exposed");
    change(0x2f8, q.context.at(0x2f8) | 0x04000000u, "coverage conversion");
    change(0x2f8, q.context.at(0x2f8) & ~0x1e000u, "maximum sample distance");
    change(0x201, 0, "EQAA");
    change(0x292, 2, "MSAA_ENABLE");
    change(0x1c4, 9, "sample-mask export");
    change(0x302, q.context.at(0x302) ^ 1u, "pixel quad");
    change(0x30e, 0xfffffffeu, "sample masks");
    change(0x31d, 2u << 12u | 2u << 15u, "color target sample count");
    change(0x010, 3u | (2u << 2u) | (24u << 4u), "depth target sample count");
    change(0x010, q.context.at(0x010) | 0x10000u, "mipmapped depth");
    change(0x010, 3u | (3u << 2u) | (25u << 4u), "SW_64KB_Z_X");
    change(0x002, 1, "arrays or slices");
    change(0x000, 0x20, "guest-plane import");
    auto nonclear = q; nonclear.context[0x000] = 0x20;
    assert(DrawRejection(nonclear, false, true).find("guest-plane import") != std::string::npos);
    auto compressed = q; compressed.context[0x011] = 24u << 4u | 1u; compressed.context[0x000] &= ~0x20u;
    rejects([&] { DecodeState(compressed, true); }, "compressed stencil");
    auto z = q; z.context[0x010] |= 0x20000000u; z.context[0x200] |= 2u; z.context[0x000] &= ~2u;
    rejects([&] { DecodeState(z, true); }, "HTILE");
    auto missing = q; missing.context.erase(0x30b);
    rejects([&] { DecodeState(missing, true); }, "missing register");
}

void checkPipeline() {
    const auto c = context();
    ShaderResources resources;
    VertexInputLayout input;
    ShaderRecompiler::RecompileResult vertex{}, fragment{};
    // Module contents are intentionally outside this explicit Vulkan mock's
    // qualification scope. IDs ensure the actual cache path is exercised.
    vertex.variantId = 17; fragment.variantId = 19;
    const std::array shaders{CompiledShader{ShaderRecompiler::ShaderStage::Vertex, &vertex, 0}, CompiledShader{ShaderRecompiler::ShaderStage::Fragment, &fragment, 0}};
    for (unsigned count : {1u, 2u, 4u, 8u}) {
        const auto s = DecodeState(state(count), true);
        for (unsigned group = 0; group < s.samples.groups; ++group) {
            auto pipeline = CachedPipeline(c, s, input, resources, shaders, VK_IMAGE_LAYOUT_GENERAL, group);
            assert(recorded.native == s.samples.nativeCount && recorded.custom == (count > 1));
            assert(recorded.attachments.size() == 2);
            assert(std::all_of(recorded.attachments.begin(), recorded.attachments.end(), [&](auto value) { return value == s.samples.nativeCount; }));
            if (count > 1) for (unsigned i = 0; i < s.samples.nativeCount; ++i) {
                assert(recorded.positions[i].x == s.samples.positions[group * s.samples.nativeCount + i].x);
                assert(recorded.positions[i].y == s.samples.positions[group * s.samples.nativeCount + i].y);
            }
            const unsigned old = pipelines;
            assert(CachedPipeline(c, s, input, resources, shaders, VK_IMAGE_LAYOUT_GENERAL, group) == pipeline);
            assert(pipelines == old);
            const std::array views{reinterpret_cast<VkImageView>(0x40), reinterpret_cast<VkImageView>(0x50)};
            Framebuffer framebuffer(c, VK_NULL_HANDLE, views, {64, 64});
            pipeline->Begin(reinterpret_cast<VkCommandBuffer>(0x10), framebuffer, {64, 64}, s);
            assert(recorded.initial == (count > 1) && recorded.after == (count > 1));
            ++cases;
        }
    }
    auto s = DecodeState(state(8), true);
    const auto old = pipelines;
    auto first = CachedPipeline(c, s, input, resources, shaders, VK_IMAGE_LAYOUT_GENERAL, 0);
    auto second = CachedPipeline(c, s, input, resources, shaders, VK_IMAGE_LAYOUT_GENERAL, 1);
    assert(first != second && old == pipelines);
    s.samples.positions[0].x += 1.0f / 16.0f;
    assert(CachedPipeline(c, s, input, resources, shaders, VK_IMAGE_LAYOUT_GENERAL, 0) != first);
    assert(pipelines == old + 1);
    rejects([&] { first->Continue(reinterpret_cast<VkCommandBuffer>(0x10), s); }, "sample pattern");
    s = DecodeState(state(8), true);
    rejects([&] { Pipeline(c, s, input, resources, shaders, VK_IMAGE_LAYOUT_GENERAL, 2); }, "sample group");
    auto reduced = c; reduced.sampleLocations = false;
    rejects([&] { Pipeline(reduced, s, input, resources, shaders); }, "sample-location capabilities");
    reduced = c; reduced.sampleLocationProperties.sampleLocationSubPixelBits = 3;
    rejects([&] { Pipeline(reduced, s, input, resources, shaders); }, "sample-location capabilities");
    reduced = c; reduced.sampleLocationProperties.sampleLocationSampleCounts = 3;
    rejects([&] { Pipeline(reduced, s, input, resources, shaders); }, "sample-location capabilities");
    reduced = c; reduced.limits.framebufferStencilSampleCounts = 3;
    rejects([&] { Pipeline(reduced, s, input, resources, shaders); }, "stencil sample count");
    const unsigned allocatedBeforePortability = live;
    // Core Vulkan operations may be absent on an enumerated portability device.
    auto portableState = DecodeState(state(1), true);
    reduced = c; reduced.triangleFans = false;
    portableState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    rejects([&] { Pipeline(reduced, portableState, input, resources, shaders); }, "triangle fans");
    portableState = DecodeState(state(1), true);
    portableState.blends[0].blendEnable = VK_TRUE;
    portableState.blends[0].srcColorBlendFactor = VK_BLEND_FACTOR_CONSTANT_ALPHA;
    reduced = c; reduced.constantAlphaColorBlendFactors = false;
    rejects([&] { Pipeline(reduced, portableState, input, resources, shaders); }, "constant-alpha");
    portableState.blends[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    rejects([&] { Pipeline(reduced, portableState, input, resources, shaders); }, "constant-alpha");
    portableState = DecodeState(state(1), true);
    portableState.stencilTest = true;
    portableState.stencilBack.reference ^= 1u;
    reduced = c; reduced.separateStencilMaskRef = false;
    rejects([&] { Pipeline(reduced, portableState, input, resources, shaders); }, "separate front/back stencil masks or reference");
    portableState.stencilBack = portableState.stencilFront;
    portableState.stencilBack.writeMask ^= 1u;
    rejects([&] { Pipeline(reduced, portableState, input, resources, shaders); }, "separate front/back stencil masks or reference");
    assert(live == allocatedBeforePortability);
    s.samples.mask = 127;
    rejects([&] { Pipeline(c, s, input, resources, shaders); }, "partial pipeline sample masks");
    s = DecodeState(state(8), true);
    s.depth->samples = 4;
    rejects([&] { Pipeline(c, s, input, resources, shaders); }, "depth sample count mismatch");
    s = DecodeState(state(8), true);
    s.colors[0].fragments = 4;
    rejects([&] { Pipeline(c, s, input, resources, shaders); }, "color sample count mismatch");
    s = DecodeState(state(8), true);
    s.samples.positions[0].x = -0.1f;
    rejects([&] { Pipeline(c, s, input, resources, shaders); }, "outside device coordinate range");
    s = DecodeState(state(8), true);
    const unsigned allocated = live;
    failPipeline = true;
    rejects([&] { Pipeline(c, s, input, resources, shaders); }, "vkCreateGraphicsPipelines");
    failPipeline = false;
    assert(allocated == live);
    first.reset(); second.reset(); ClearCachedPipelines(c.device);
    assert(live == 0);
}

void privateRegisters(const char* path) {
    QueueState q; q.context.clear(); q.shader.clear(); q.userConfig.clear();
    std::ifstream file(path); assert(file.good());
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream stream(line); std::string bank; unsigned offset, value;
        if (!(stream >> bank >> std::hex >> offset >> value)) continue;
        if (bank == "context") q.context[offset] = value;
        if (bank == "shader") q.shader[offset] = value;
        if (bank == "user-config" || bank == "uconfig") q.userConfig[offset] = value;
    }
    // Give the production range checker a real test-owned allocation. The
    // captured image bytes are neither required nor imported into this test.
    void* memory = nullptr; assert(posix_memalign(&memory, 65536, 96u * 1024u * 1024u) == 0);
    const auto address = reinterpret_cast<std::uintptr_t>(memory);
    q.context[0x318] = static_cast<std::uint32_t>(address >> 8u);
    q.context[0x390] = static_cast<std::uint32_t>(address >> 40u);
    const auto decoded = DecodeState(q, true);
    assert(decoded.samples.count == 8 && decoded.depth->samples == 8 && decoded.color.fragments == 8);
    assert(decoded.stencilTest && !decoded.depthTest && !decoded.depthWrite && decoded.stencilFront.writeMask == 255);
    assert(DrawRejection(q, false, true).empty());
    std::free(memory);
    std::puts("Private captured first draw: sample configuration + color/stencil state decoded; no game image copied.");
}
}

namespace AgcDriver::Graphics {
// Unreached single-sample slice helper; any accidental MSAA fallback is fatal.
std::uint64_t DepthSliceBytes(VkExtent2D, std::uint32_t) { throw std::runtime_error("unreached legacy depth-slice adapter"); }
}

int main(int argc, char** argv) {
    checkState(); checkPipeline();
    if (argc == 2) privateRegisters(argv[1]);
    std::printf("Production state/pipeline structures: %u cases; sample counts 1/2/4/8, cache group/pattern isolation and failure cleanup passed.\n", cases);
}
