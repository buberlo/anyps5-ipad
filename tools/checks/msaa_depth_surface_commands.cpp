// Command capture test for the actual DepthSurface.cpp allocation/cache logic.
// Vulkan and neighboring command/texture services are explicit mocks: this does
// not execute a GPU, qualify guest upload/readback or prove render-pass ordering.
#include "prx/libSceAgcDriver/Graphics/include/DepthSurface.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
#include "prx/libSceAgcDriver/Graphics/include/TextureFormat.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace AgcDriver::Graphics;
namespace {
std::uintptr_t serial = 100;
template<typename T> T handle() {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(++serial);
    else return static_cast<T>(++serial);
}
struct Image {
    VkDevice owner;
    VkImageCreateInfo info;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    std::vector<bool> cleared;
};
std::map<VkImage, Image> images;
std::map<VkImageView, std::pair<VkImage, VkImageSubresourceRange>> views;
std::map<VkDeviceMemory, VkDevice> allocations;
VkFormatFeatureFlags features = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
VkImageFormatProperties support{{4096, 4096, 1}, 1, 2,
                               VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT, 1ull << 30u};
VkDeviceSize requiredMemory = 4096;
VkResult queryResult = VK_SUCCESS;
std::string_view failingFunction;
unsigned failingViewIndex = 0;
unsigned calls = 0, imageCreations = 0, viewCreations = 0, clears = 0, barriers = 0;
VkImageCreateFlags lastQueryFlags = 0;
VkImageUsageFlags lastQueryUsage = 0;

void VKAPI_PTR formatProperties(VkPhysicalDevice, VkFormat, VkFormatProperties* output) {
    *output = {}; output->optimalTilingFeatures = features;
}
VkResult VKAPI_PTR imageProperties(VkPhysicalDevice, VkFormat, VkImageType type, VkImageTiling tiling,
                                  VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* output) {
    assert(type == VK_IMAGE_TYPE_2D && tiling == VK_IMAGE_TILING_OPTIMAL);
    lastQueryFlags = flags; lastQueryUsage = usage; *output = support;
    return queryResult;
}
VkResult VKAPI_PTR createImage(VkDevice device, const VkImageCreateInfo* info,
                              const VkAllocationCallbacks*, VkImage* output) {
    ++calls;
    if (failingFunction == "vkCreateImage") return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    assert(info->flags == lastQueryFlags && info->usage == lastQueryUsage && info->pNext == nullptr);
    assert(info->arrayLayers <= 2 && (info->samples == 1 || info->samples == 2 || info->samples == 4));
    *output = handle<VkImage>();
    images.emplace(*output, Image{device, *info, VK_NULL_HANDLE,
                                 std::vector<bool>(info->arrayLayers * info->samples)});
    ++imageCreations;
    return VK_SUCCESS;
}
void VKAPI_PTR imageRequirements(VkDevice, VkImage image, VkMemoryRequirements* output) {
    assert(images.contains(image)); *output = {requiredMemory, 256, 1};
}
VkResult VKAPI_PTR allocateMemory(VkDevice device, const VkMemoryAllocateInfo* info,
                                 const VkAllocationCallbacks*, VkDeviceMemory* output) {
    if (failingFunction == "vkAllocateMemory") return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    assert(info->allocationSize == requiredMemory && info->memoryTypeIndex == 0);
    *output = handle<VkDeviceMemory>(); allocations.emplace(*output, device); return VK_SUCCESS;
}
VkResult VKAPI_PTR bindMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize offset) {
    if (failingFunction == "vkBindImageMemory") return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    assert(allocations.at(memory) == device && images.at(image).owner == device && offset == 0);
    images.at(image).memory = memory; return VK_SUCCESS;
}
VkResult VKAPI_PTR createView(VkDevice device, const VkImageViewCreateInfo* info,
                             const VkAllocationCallbacks*, VkImageView* output) {
    assert(images.at(info->image).owner == device && info->viewType == VK_IMAGE_VIEW_TYPE_2D);
    if (failingFunction == "vkCreateImageView" && info->subresourceRange.baseArrayLayer == failingViewIndex)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    assert(info->subresourceRange.layerCount == 1 && info->subresourceRange.baseArrayLayer < images.at(info->image).info.arrayLayers);
    *output = handle<VkImageView>(); views.emplace(*output, std::pair{info->image, info->subresourceRange});
    ++viewCreations; return VK_SUCCESS;
}
void VKAPI_PTR destroyView(VkDevice device, VkImageView view, const VkAllocationCallbacks*) {
    assert(images.at(views.at(view).first).owner == device); views.erase(view);
}
void VKAPI_PTR destroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks*) {
    assert(images.at(image).owner == device);
    for (const auto& [view, state] : views) { (void)view; assert(state.first != image); }
    images.erase(image);
}
void VKAPI_PTR freeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks*) {
    assert(allocations.at(memory) == device);
    for (const auto& [image, state] : images) { (void)image; assert(state.memory != memory); }
    allocations.erase(memory);
}
void VKAPI_PTR pipelineBarrier(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags,
                               VkDependencyFlags, std::uint32_t, const VkMemoryBarrier*,
                               std::uint32_t, const VkBufferMemoryBarrier*, std::uint32_t count,
                               const VkImageMemoryBarrier* transitions) {
    ++barriers;
    for (unsigned i = 0; i < count; ++i) {
        const auto& transition = transitions[i];
        const auto& image = images.at(transition.image);
        assert(transition.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && transition.newLayout == VK_IMAGE_LAYOUT_GENERAL);
        assert(transition.subresourceRange.baseArrayLayer == 0 && transition.subresourceRange.layerCount == image.info.arrayLayers);
    }
}
void VKAPI_PTR clearImage(VkCommandBuffer, VkImage image, VkImageLayout layout,
                         const VkClearDepthStencilValue* clear, std::uint32_t count,
                         const VkImageSubresourceRange* ranges) {
    assert(layout == VK_IMAGE_LAYOUT_GENERAL && clear->depth == 0.75f && clear->stencil == 19 && count == 1);
    auto& state = images.at(image);
    for (unsigned i = 0; i < count; ++i) {
        assert(ranges[i].aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));
        assert(ranges[i].baseArrayLayer == 0 && ranges[i].layerCount == state.info.arrayLayers);
        for (unsigned layer = ranges[i].baseArrayLayer; layer < ranges[i].baseArrayLayer + ranges[i].layerCount; ++layer)
            for (unsigned sample = 0; sample < state.info.samples; ++sample) state.cleared.at(layer * state.info.samples + sample) = true;
    }
    ++clears;
}
PFN_vkVoidFunction VKAPI_PTR deviceFunction(VkDevice, const char* name) {
    if (failingFunction == "missingClear" && std::strcmp(name, "vkCmdClearDepthStencilImage") == 0) return nullptr;
#define ENTRY(api, implementation) if (std::strcmp(name, api) == 0) return reinterpret_cast<PFN_vkVoidFunction>(implementation)
    ENTRY("vkCreateImage", createImage);
    ENTRY("vkGetImageMemoryRequirements", imageRequirements);
    ENTRY("vkAllocateMemory", allocateMemory);
    ENTRY("vkBindImageMemory", bindMemory);
    ENTRY("vkCreateImageView", createView);
    ENTRY("vkDestroyImageView", destroyView);
    ENTRY("vkDestroyImage", destroyImage);
    ENTRY("vkFreeMemory", freeMemory);
    ENTRY("vkCmdPipelineBarrier", pipelineBarrier);
    ENTRY("vkCmdClearDepthStencilImage", clearImage);
#undef ENTRY
    std::fprintf(stderr, "unexpected Vulkan function %s\n", name); std::abort();
}
Context context() {
    Context output{}; output.device = handle<VkDevice>(); output.physical = handle<VkPhysicalDevice>();
    output.deviceProc = deviceFunction; output.formatProperties = formatProperties; output.imageFormatProperties = imageProperties;
    output.memory.memoryTypeCount = 1; output.memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    output.limits.maxFramebufferWidth = output.limits.maxFramebufferHeight = 4096;
    output.sampleLocations = true; output.multisampleArrayImage = true;
    return output;
}
DepthTarget target(unsigned samples) {
    return {0x1000000, 0x5000000, {1920, 1080}, VK_FORMAT_D32_SFLOAT_S8_UINT, 0.75f, 19, samples};
}
template<typename Action> void rejects(Action action, std::string_view message) {
    try { action(); }
    catch (const std::runtime_error& error) { assert(std::string_view(error.what()).find(message) != std::string_view::npos); return; }
    throw std::runtime_error("expected rejection did not occur");
}
void empty() { assert(images.empty() && views.empty() && allocations.empty()); }
}

// Explicit test boundaries. These mock scheduling and an unrelated Texture path,
// not the DepthSurface implementation being tested. Texture must never be reached
// by the unsupported MSAA descriptor tests.
namespace AgcDriver::Graphics {
Recorder* Recorder::Active() { return nullptr; }
VkCommandBuffer Recorder::Commands(VkAccessFlags*) { throw std::runtime_error("mock Recorder path was not expected"); }
void Recorder::CountBarriers(CommandClass, std::uint32_t) {}
CommandBatch::CommandBatch(const Context& context) : context(context) { commands = handle<VkCommandBuffer>(); }
CommandBatch::~CommandBatch() = default;
VkCommandBuffer CommandBatch::Handle() const { return commands; }
void CommandBatch::SubmitAndWait() {}
void RecordMemoryBarrier(const Context& context, VkCommandBuffer commands, VkPipelineStageFlags sourceStage,
                         VkPipelineStageFlags destinationStage, VkAccessFlags sourceAccess, VkAccessFlags destinationAccess) {
    const VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, sourceAccess, destinationAccess};
    context.Resolved(&DeviceFunctions::cmdPipelineBarrier, "vkCmdPipelineBarrier")(commands, sourceStage, destinationStage, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}
VkFormat ResolveTextureFormat(std::uint32_t) { throw std::runtime_error("mock texture format path was not expected"); }
Texture::Texture(const Context& context, VkImage, VkFormat, VkImageAspectFlags, VkComponentMapping) : context(context) {
    throw std::runtime_error("mock Texture path was not expected");
}
Texture::~Texture() = default;
}

int main() {
    auto c = context();
    unsigned configurations = 0;
    for (unsigned samples : {1u, 2u, 4u, 8u}) {
        auto t = target(samples); const auto oldImages = imageCreations, oldClears = clears;
        const auto first = DepthSurfaceView(c, t);
        assert(imageCreations == oldImages + 1 && clears == oldClears + 1);
        const auto image = views.at(first).first;
        const auto& state = images.at(image);
        assert(state.info.samples == (samples == 8 ? 4u : samples));
        assert(state.info.arrayLayers == (samples == 8 ? 2u : 1u));
        assert(state.info.flags == (samples == 1 ? 0u : VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT));
        assert(state.cleared.size() == samples);
        for (bool cleared : state.cleared) assert(cleared);
        assert(DepthSurfaceView(c, t) == first && imageCreations == oldImages + 1 && clears == oldClears + 1);
        if (samples == 8) {
            const auto second = DepthSurfaceView(c, t, 1);
            assert(second != first && views.at(second).first == image && views.at(second).second.baseArrayLayer == 1);
            assert(DepthSurfaceView(c, t, 1) == second && imageCreations == oldImages + 1 && clears == oldClears + 1);
        }
        rejects([&] { DepthSurfaceView(c, t, samples == 8 ? 2u : 1u); }, "group out of range");
        ++configurations;
    }
    assert(images.size() == 4); // Sample counts may not alias the same cached surface.
    GuestTextureResource resource{}; resource.baseAddress = target(8).stencilAddress;
    const std::array<std::uint32_t, 8> words{};
    rejects([&] { DepthSurfaceTexture(c, words, resource, {}); }, "sampling multisample");
    auto other = context(); (void)DepthSurfaceView(other, target(8), 1);
    assert(images.size() == 5); ClearDepthSurfaces(c.device); assert(images.size() == 1);
    ClearDepthSurfaces(other.device); empty();

    // An MSAA descriptor cannot reinterpret a cached single-sample plane either.
    auto single = target(1); single.address += 0x8000000; single.stencilAddress += 0x8000000;
    (void)DepthSurfaceView(c, single);
    resource.baseAddress = single.stencilAddress;
    for (unsigned type : {14u, 15u}) {
        std::array<std::uint32_t, 8> msaaWords{}; msaaWords[3] = type << 28u;
        rejects([&] { DepthSurfaceTexture(c, msaaWords, resource, {}); }, "sampling multisample");
    }
    ClearDepthSurfaces(c.device); empty();

    // Neither failed capability checks nor partial construction may insert a cache
    // entry, leak resources, or erase another device's successfully cached image.
    unsigned rejections = 0;
    const auto invalid = [&](auto action, std::string_view message) { rejects(action, message); empty(); ++rejections; };
    for (unsigned count : {0u, 3u, 16u}) invalid([&] { DepthSurfaceView(c, target(count)); }, "sample count");
    c.sampleLocations = false; invalid([&] { DepthSurfaceView(c, target(8)); }, "enabled VK_EXT_sample_locations");
    c.sampleLocations = true; c.multisampleArrayImage = false;
    invalid([&] { DepthSurfaceView(c, target(8)); }, "multisample depth/stencil array"); c.multisampleArrayImage = true;
    const auto oldFeatures = features;
    for (const auto feature : {VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, VK_FORMAT_FEATURE_TRANSFER_DST_BIT}) {
        features &= ~feature;
        invalid([&] { DepthSurfaceView(c, target(8)); }, feature == VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ? "cannot be an attachment" : feature == VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT ? "cannot be sampled" : "cannot be cleared");
        features = oldFeatures;
    }
    c.imageFormatProperties = nullptr;
    invalid([&] { DepthSurfaceView(c, target(8)); }, "missing depth/stencil image capability query"); c.imageFormatProperties = imageProperties;
    support.sampleCounts = VK_SAMPLE_COUNT_1_BIT;
    invalid([&] { DepthSurfaceView(c, target(8)); }, "device image limits"); support.sampleCounts |= VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT;
    support.maxArrayLayers = 1;
    invalid([&] { DepthSurfaceView(c, target(8)); }, "device image limits"); support.maxArrayLayers = 2;
    support.maxExtent.width = 1000;
    invalid([&] { DepthSurfaceView(c, target(8)); }, "device image limits"); support.maxExtent.width = 4096;
    support.maxResourceSize = requiredMemory - 1;
    invalid([&] { DepthSurfaceView(c, target(8)); }, "maximum device image resource size"); support.maxResourceSize = 1ull << 30u;
    auto zero = target(8); zero.extent.width = 0;
    invalid([&] { DepthSurfaceView(c, zero); }, "framebuffer limits");
    queryResult = VK_ERROR_FORMAT_NOT_SUPPORTED;
    invalid([&] { DepthSurfaceView(c, target(8)); }, "Vulkan result"); queryResult = VK_SUCCESS;
    for (const auto failure : {"vkCreateImage", "vkAllocateMemory", "vkBindImageMemory", "vkCreateImageView"}) {
        failingFunction = failure;
        invalid([&] { DepthSurfaceView(c, target(8)); }, "Vulkan result");
    }
    failingViewIndex = 1; // Rollback must also free the first successfully made view.
    invalid([&] { DepthSurfaceView(c, target(8)); }, "Vulkan result");
    failingFunction = "missingClear"; const auto oldBarriers = barriers;
    invalid([&] { DepthSurfaceView(c, target(8)); }, "missing Vulkan function"); assert(barriers == oldBarriers);
    failingFunction = {};
    // A failed construction on one device preserves another device's live cache.
    const auto otherView = DepthSurfaceView(other, target(8), 1);
    failingFunction = "vkCreateImageView"; failingViewIndex = 1;
    rejects([&] { DepthSurfaceView(c, target(8)); }, "Vulkan result");
    assert(images.size() == 1 && views.size() == 2 && allocations.size() == 1 && views.contains(otherView));
    failingFunction = {};
    const auto recovered = DepthSurfaceView(c, target(8), 1); assert(views.at(recovered).second.baseArrayLayer == 1);
    ClearDepthSurfaces(c.device); assert(images.size() == 1 && views.contains(otherView));
    ClearDepthSurfaces(other.device); empty();
    c.sampleLocations = false; c.multisampleArrayImage = false;
    (void)DepthSurfaceView(c, target(1)); ClearDepthSurfaces(c.device); empty();
    std::printf("DepthSurface production command capture: configurations=%u cache/device isolation=pass rejections=%u rollback=pass errors=0 (no GPU execution)\n", configurations, rejections);
}
