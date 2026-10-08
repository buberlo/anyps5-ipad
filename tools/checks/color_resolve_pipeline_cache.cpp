// Actual production CachedPipeline/Pipeline, resolve factory and canonical
// rectangle shaders. Vulkan object creation is a counted mock. ShaderResources
// is an explicit immutable-layout boundary adapter, not a resource/GPU test.
#include "prx/libSceAgcDriver/Graphics/include/ColorResolve.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Pipeline.hpp"
#include "prx/libSceAgcDriver/Graphics/include/VertexInput.hpp"
#include "prx/libSceAgcDriver/Graphics/include/BufferPool.hpp"
#include "prx/libSceAgcDriver/Graphics/include/SplitDrawShaderValidation.hpp"
#include <spirv/unified1/spirv.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <type_traits>

using namespace AgcDriver::Graphics;
using namespace ShaderRecompiler;
namespace {
unsigned checks = 0, creates = 0, destroys = 0;
std::uintptr_t nextHandle = 0x100;
std::map<std::uintptr_t, VkDevice> objects;
std::map<VkShaderModule, std::vector<std::uint32_t>> modules;
template<class H> H handle(std::uintptr_t value) {
    if constexpr (std::is_pointer_v<H>) return reinterpret_cast<H>(value);
    else return static_cast<H>(value);
}
template<class H> std::uintptr_t bits(H value) {
    if constexpr (std::is_pointer_v<H>) return reinterpret_cast<std::uintptr_t>(value);
    else return static_cast<std::uintptr_t>(value);
}
void check(bool value, const char* reason) { ++checks; if (!value) throw std::runtime_error(reason); }
template<class H> H make(VkDevice device) { auto value = handle<H>(++nextHandle); objects.emplace(bits(value), device); return value; }
template<class H> void destroy(VkDevice device, H value) {
    check(objects.contains(bits(value)) && objects.at(bits(value)) == device, "destroyed a missing/foreign mock Vulkan object");
    objects.erase(bits(value));
}
void retireDevice(VkDevice device) {
    // Vulkan destroys all device children when an old device goes away. Cache
    // Abandon must subsequently forget them, not destroy their stale handles.
    std::erase_if(objects, [&](const auto& entry) { return entry.second == device; });
    std::erase_if(modules, [&](const auto& entry) { return !objects.contains(bits(entry.first)); });
}
VKAPI_ATTR VkResult VKAPI_CALL createModule(VkDevice device, const VkShaderModuleCreateInfo* info, const VkAllocationCallbacks*, VkShaderModule* result) {
    check(info->codeSize >= 20 && info->codeSize % 4 == 0 && info->pCode[0] == 0x07230203u, "mock received no real shader code");
    *result = make<VkShaderModule>(device);
    modules[*result].assign(info->pCode, info->pCode + info->codeSize / 4);
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroyModule(VkDevice device, VkShaderModule value, const VkAllocationCallbacks*) { destroy(device, value); modules.erase(value); }
VKAPI_ATTR VkResult VKAPI_CALL createLayout(VkDevice device, const VkPipelineLayoutCreateInfo* info, const VkAllocationCallbacks*, VkPipelineLayout* result) {
    check(info->setLayoutCount == 1 && info->pSetLayouts != nullptr, "immutable layout adapter was not consumed");
    *result = make<VkPipelineLayout>(device); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroyLayout(VkDevice device, VkPipelineLayout value, const VkAllocationCallbacks*) { destroy(device, value); }
VKAPI_ATTR VkResult VKAPI_CALL createPass(VkDevice device, const VkRenderPassCreateInfo* info, const VkAllocationCallbacks*, VkRenderPass* result) {
    check(info->attachmentCount == 1 && info->subpassCount == 1 && info->pAttachments[0].loadOp == VK_ATTACHMENT_LOAD_OP_LOAD, "resolve load/render-pass state changed");
    *result = make<VkRenderPass>(device); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroyPass(VkDevice device, VkRenderPass value, const VkAllocationCallbacks*) { destroy(device, value); }
VKAPI_ATTR VkResult VKAPI_CALL createPipelines(VkDevice device, VkPipelineCache, std::uint32_t count, const VkGraphicsPipelineCreateInfo* info, const VkAllocationCallbacks*, VkPipeline* result) {
    check(count == 1 && info->stageCount == 4 && info->pTessellationState && info->pTessellationState->patchControlPoints == 3, "actual resolve graphics stages changed");
    for (unsigned stage = 0; stage < info->stageCount; ++stage) check(modules.contains(info->pStages[stage].module), "pipeline references a stale shader module");
    *result = make<VkPipeline>(device); ++creates; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroyPipeline(VkDevice device, VkPipeline value, const VkAllocationCallbacks*) { destroy(device, value); ++destroys; }
PFN_vkVoidFunction VKAPI_CALL proc(VkDevice, const char* name) {
    static const std::map<std::string_view, PFN_vkVoidFunction> functions{
        {"vkCreateShaderModule", reinterpret_cast<PFN_vkVoidFunction>(createModule)},
        {"vkDestroyShaderModule", reinterpret_cast<PFN_vkVoidFunction>(destroyModule)},
        {"vkCreatePipelineLayout", reinterpret_cast<PFN_vkVoidFunction>(createLayout)},
        {"vkDestroyPipelineLayout", reinterpret_cast<PFN_vkVoidFunction>(destroyLayout)},
        {"vkCreateRenderPass", reinterpret_cast<PFN_vkVoidFunction>(createPass)},
        {"vkDestroyRenderPass", reinterpret_cast<PFN_vkVoidFunction>(destroyPass)},
        {"vkCreateGraphicsPipelines", reinterpret_cast<PFN_vkVoidFunction>(createPipelines)},
        {"vkDestroyPipeline", reinterpret_cast<PFN_vkVoidFunction>(destroyPipeline)}
    };
    auto entry = functions.find(name); return entry == functions.end() ? nullptr : entry->second;
}
std::vector<std::uint32_t> read(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate); check(bool(file), "missing original vertex SPIR-V");
    auto size = file.tellg(); check(size >= 20 && size % 4 == 0, "invalid vertex SPIR-V size");
    std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4); file.seekg(0); file.read(reinterpret_cast<char*>(words.data()), size); check(bool(file), "vertex SPIR-V read failed"); return words;
}
Context context(VkDevice device) {
    Context c{}; c.device = device; c.deviceProc = proc; c.tessellationShader = true;
    c.limits.maxTessellationPatchSize = 32; c.limits.maxColorAttachments = 8;
    c.limits.maxPushConstantsSize = 128; c.limits.framebufferColorSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    return c;
}
SpirvTarget target() {
    SpirvTarget t{}; t.spirvVersion = 0x00010300u; t.vulkanVersion = VK_API_VERSION_1_1;
    static constexpr std::uint32_t capabilities[]{spv::CapabilityShader,spv::CapabilityTessellation};
    t.supportedCapabilities = capabilities;
    t.tessellation = TessellationTargetLimits{32,128,128,128,4096,128,128}; return t;
}
State state() {
    State s{}; s.rectList = true; s.hasColorTarget = true; s.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    ColorTarget c{}; c.extent = {32,24}; c.format = VK_FORMAT_R8G8B8A8_UNORM; c.samples = c.fragments = 1;
    s.colors.push_back(c); s.color = c; s.samples = SampleConfiguration{}; s.blends.resize(1); s.blends[0].colorWriteMask = 15; return s;
}
ColorTarget source(unsigned samples, std::uint64_t address = 0x100000u, unsigned width = 32) {
    ColorTarget c{}; c.address = c.surfaceAddress = address; c.extent = {width,24};
    c.format = VK_FORMAT_R8G8B8A8_UNORM; c.elementBytes = 4; c.samples = c.fragments = samples; c.tileMode = ColorTileMode::RenderTarget; return c;
}
struct DrawStages {
    RecompileResult vertex, fragment; RectListShaders rectangle;
    DrawStages(const std::vector<std::uint32_t>& code, unsigned samples, unsigned binding = 0) {
        vertex.spirv = code; vertex.variantId = 100;
        if (binding) { DescriptorBinding d{}; d.binding = binding - 1; d.kind = DescriptorKind::StorageBuffer; d.count = 1; vertex.bindings.push_back(d); }
        fragment = BuildColorResolveFragment(source(samples), vertex);
        // The factory can reserve any unused binding. Drop the reservation
        // metadata from this original resource-free VS before validation.
        vertex.bindings.clear();
        rectangle = BuildRectListShaders(vertex, fragment, target());
    }
    std::array<CompiledShader,4> shaders(bool tagged = true) const {
        return {{{ShaderStage::Vertex,&vertex,0}, {ShaderStage::TessellationControl,&rectangle.control,0},
                 {ShaderStage::TessellationEvaluation,&rectangle.evaluation,0},
                 {ShaderStage::Fragment,&fragment,0,tagged ? PipelineShaderIdentity::ColorResolveFragment : PipelineShaderIdentity::None}}};
    }
};
}

// Explicit layout-only boundary adapter. No descriptors, guest mappings, GPU
// uploads or resource-cache implementations are substituted into the cache.
namespace AgcDriver::Graphics {
GuestBufferMemory::GuestBufferMemory(const Context& c) : context(c) {}
GuestBufferMemory::~GuestBufferMemory() = default;
Buffer::~Buffer() = default;
BufferPool::BufferPool(const Context&) {}
BufferPool::~BufferPool() = default;
ShaderResources::ShaderResources(const Context& c, std::span<const CompiledShader> stages, const ColorTarget&, std::uint64_t, std::size_t, std::span<const GuestMemorySnapshot>) : context(c), guestMemory(c) {
    _layout = handle<VkDescriptorSetLayout>(0x40);
    for (const auto& stage : stages) for (const auto& b : stage.program->bindings) {
        layoutKey.insert(layoutKey.end(), {b.binding, static_cast<std::uint32_t>(b.kind), b.count, static_cast<std::uint32_t>(VulkanStage(stage.stage))});
    }
}
ShaderResources::~ShaderResources() = default;
VkDescriptorSetLayout ShaderResources::Layout() const { return _layout; }
}

int main(int argc, char** argv) {
    try {
        check(argc == 2, "one vertex fixture required");
        const auto code = read(argv[1]); const auto* option = std::getenv("APS5_CACHE_COLOR_RESOLVE_PIPELINES");
        const bool enabled = option && std::strcmp(option,"1") == 0;
        auto c = context(handle<VkDevice>(1)); const auto s = state(); const VertexInputLayout input{};
        DrawStages stages(code,2); const auto all = stages.shaders();
        check(SplitRectListShaderRejection(all,target()).empty(), "production canonical rectangle proof failed");
        check(ValidateShaders(all,s,c.subgroup,false,false,false,false,false,true).contains(0), "production shader inspection failed");
        ShaderResources resources(c,all,s.color,0,0);
        auto get = [&](const DrawStages& value, bool tag = true) {
            auto shaders = value.shaders(tag); ShaderResources current(c,shaders,s.color,0,0);
            return CachedPipeline(c,s,input,current,shaders);
        };
        auto first = get(stages), repeat = get(stages);
        check(enabled ? first == repeat : first != repeat, "exact opt-in pipeline reuse differs");
        if (!enabled) {
            auto ordinary = stages; ordinary.fragment.variantId = 200;
            auto normal1 = get(ordinary,false), normal2 = get(ordinary,false);
            check(normal1 == normal2, "default-off changed ordinary known-variant pipeline reuse");
            normal1.reset(); normal2.reset();
            first.reset(); repeat.reset(); ClearCachedPipelines(c.device);
            check(objects.empty(), "default private pipeline leaked objects");
            std::printf("production pipeline cache exact_option=%s enabled=0 creates=%u checks=%u PASS\n",option ? option : "unset",creates,checks); return 0;
        }
        check(creates == 1, "identical resolve rebuilt Vulkan objects");
        auto relocated = stages;
        relocated.fragment = BuildColorResolveFragment(source(2,0x200000u,63),relocated.vertex);
        check(relocated.fragment.spirv.Words() == stages.fragment.spirv.Words(), "dynamic descriptor address/extent changed code");
        check(get(relocated) == first, "dynamic source metadata prevented safe pipeline reuse");
        for (unsigned samples : {4u,8u}) {
            DrawStages other(code,samples);
            check(SplitRectListShaderRejection(other.shaders(),target()).empty() &&
                  ValidateShaders(other.shaders(),s,c.subgroup,false,false,false,false,false,true).contains(0), "sample variant fails production shader inspection");
            check(get(other) != first, "sample count aliases the compiled resolve code");
        }
        DrawStages rebound(code,2,6);
        check(SplitRectListShaderRejection(rebound.shaders(),target()).empty() &&
              ValidateShaders(rebound.shaders(),s,c.subgroup,false,false,false,false,false,true).contains(0), "binding variant fails production shader inspection");
        check(get(rebound) != first, "patched image/fault binding aliases a pipeline");
        // Exact-word mutation controls test cache isolation, not admission by the
        // canonical validator. The SPIR-V generator header is nonsemantic.
        for (bool control : {false,true}) {
            auto changed = stages;
            auto& spirv = control ? changed.rectangle.control.spirv : changed.rectangle.evaluation.spirv;
            auto words = spirv.Words(); words[2] ^= 1u; spirv = std::move(words);
            check(get(changed) != first, "generated tessellation words were omitted from identity");
        }
        auto unknown1 = get(stages,false), unknown2 = get(stages,false);
        check(unknown1 != unknown2 && unknown1 != first, "ordinary unknown fragment entered the pipeline cache");
        unknown1.reset(); unknown2.reset();
        auto wrongTag = stages.shaders(false); wrongTag[0].pipelineIdentity = PipelineShaderIdentity::ColorResolveFragment;
        auto wrong1 = CachedPipeline(c,s,input,resources,wrongTag), wrong2 = CachedPipeline(c,s,input,resources,wrongTag);
        check(wrong1 != wrong2, "wrong-stage factory tag entered the cache"); wrong1.reset(); wrong2.reset();
        auto unknownVertex = stages; unknownVertex.vertex.variantId = 0;
        auto uv1 = get(unknownVertex), uv2 = get(unknownVertex);
        check(uv1 != uv2, "ordinary unknown vertex entered the factory pipeline cache"); uv1.reset(); uv2.reset();
        // Direct cache-policy negative controls intentionally skip full shader
        // validation; production Draw rejects these invalid stage/tag tuples.
        auto duplicate = stages.shaders(); duplicate[1] = duplicate[3];
        auto dup1 = CachedPipeline(c,s,input,resources,duplicate), dup2 = CachedPipeline(c,s,input,resources,duplicate);
        check(dup1 != dup2, "duplicate factory tags entered the pipeline cache"); dup1.reset(); dup2.reset();
        auto notRect = s; notRect.rectList = false; notRect.stages.tessellation = TessellationConfiguration{3,4};
        auto nr1 = CachedPipeline(c,notRect,input,resources,all), nr2 = CachedPipeline(c,notRect,input,resources,all);
        check(nr1 != nr2, "a factory tag outside the resolve rectangle state entered the cache"); nr1.reset(); nr2.reset();
        check(stages.fragment.variantId == 0 && stages.rectangle.control.variantId == 0 && stages.rectangle.evaluation.variantId == 0,
              "pipeline identity incorrectly enabled resource/recipe variant admission");
        // The cache's actual LRU use_count gate retains a pipeline held by a
        // recorded owner. This is shared ownership qualification, not a Recorder
        // submission/fence test, which remains in the full production suite.
        std::weak_ptr<Pipeline> kept = first;
        auto unownedStages = stages; unownedStages.vertex.variantId = 900;
        auto victim = get(unownedStages); std::weak_ptr<Pipeline> unowned = victim; victim.reset();
        for (unsigned i=0;i<260;++i) { auto many = stages; many.vertex.variantId = 1000+i; get(many); }
        check(!kept.expired() && get(stages) == first, "LRU evicted a still-owned pipeline");
        check(unowned.expired(), "bounded LRU retained an old unowned pipeline");
        auto otherContext = context(handle<VkDevice>(2)); ShaderResources otherResources(otherContext,all,s.color,0,0);
        retireDevice(c.device);
        auto other = CachedPipeline(otherContext,s,input,otherResources,all); check(other != first, "cross-device pipeline reuse");
        first.reset(); repeat.reset(); check(kept.expired(), "abandoned old pipeline retained an owner");
        const auto heldLayout = other->Layout(); ClearCachedPipelines(otherContext.device);
        check(objects.contains(bits(heldLayout)), "cache removal destroyed a caller-owned pipeline before completion");
        other.reset(); check(objects.empty(), "final mock device objects leaked");
        // Reused VkDevice handles are distinct through the real buffer-pool
        // weak identity; Pipeline itself must not keep its pool alive.
        auto pooled = context(handle<VkDevice>(3)); pooled.bufferPool = std::make_shared<BufferPool>(pooled);
        ShaderResources poolResources(pooled,all,s.color,0,0);
        auto old = CachedPipeline(pooled,s,input,poolResources,all); auto previousPool = std::weak_ptr<BufferPool>(pooled.bufferPool);
        pooled.bufferPool.reset(); retireDevice(pooled.device);
        pooled.bufferPool = std::make_shared<BufferPool>(pooled);
        // poolResources retains the old context until scope end; an identity
        // mismatch (not expiration) is enough to reject the stale cache entry.
        auto fresh = CachedPipeline(pooled,s,input,poolResources,all); check(old != fresh, "same handle with a new pool reused a stale device pipeline");
        ClearCachedPipelines(pooled.device); old.reset(); fresh.reset();
        check(objects.empty(), "pooled mock objects leaked");
        std::printf("production pipeline cache exact_option=1 enabled=1 creates=%u destroys=%u checks=%u PASS; resource/recipe variant IDs remain zero\n",creates,destroys,checks);
    } catch (const std::exception& e) { std::fprintf(stderr,"pipeline cache fixture failed: %s\n",e.what()); return 1; }
}
