// Original offscreen resolve proof: production shader factory, rectangle stages
// and sample transfers. Its isolated Vulkan pipeline is not a full guest draw.
#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include "prx/libSceAgcDriver/Graphics/include/ColorSampleTransfer.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ColorResolve.hpp"
#include "fixed_resolve_vertex_spv.h"
#include <spirv/unified1/spirv.hpp>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <string_view>
#include <vector>

using namespace AgcDriver::Graphics;
namespace {
struct HostBuffer {
    VkDevice device;
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    void* mapped{};
    HostBuffer(const Context& context, VkDeviceSize bytes) : device(context.device) {
        try {
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            info.size = bytes;
            info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            Check(vkCreateBuffer(device, &info, nullptr, &buffer), "probe vkCreateBuffer");
            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(device, buffer, &requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = context.MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            Check(vkAllocateMemory(device, &allocation, nullptr, &memory), "probe vkAllocateMemory");
            Check(vkBindBufferMemory(device, buffer, memory, 0), "probe vkBindBufferMemory");
            Check(vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapped), "probe vkMapMemory");
        } catch (...) { release(); throw; }
    }
    ~HostBuffer() { release(); }
    void release() {
        if (mapped) vkUnmapMemory(device, memory);
        if (buffer) vkDestroyBuffer(device, buffer, nullptr);
        if (memory) vkFreeMemory(device, memory, nullptr);
    }
};
std::uint32_t Pattern(unsigned x, unsigned y, unsigned sample) {
    return ((x * 29u + y * 131u + sample * 17u) & 255u) |
        (((x * 73u ^ y * 149u ^ sample * 37u) & 255u) << 8) |
        ((((x + y + sample) * 111u) & 255u) << 16) |
        ((255u - ((x * 31u + y * 17u + sample * 13u) & 255u)) << 24);
}
void ImageBarrier(VkCommandBuffer commands, const RenderTarget& image,
                  VkImageLayout before, VkImageLayout after,
                  VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                  VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = before; barrier.newLayout = after;
    barrier.srcAccessMask = srcAccess; barrier.dstAccessMask = dstAccess;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.Image();
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, image.Groups()};
    vkCmdPipelineBarrier(commands, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
void SeedImage(VkCommandBuffer commands, const RenderTarget& image, const HostBuffer& seed,
               VkDeviceSize offset, VkImageLayout after) {
    VkBufferMemoryBarrier visible{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    visible.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT; visible.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    visible.srcQueueFamilyIndex = visible.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    visible.buffer = seed.buffer; visible.offset = offset; visible.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 1, &visible, 0, nullptr);
    ImageBarrier(commands, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region{};
    region.bufferOffset = offset;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {image.Extent().width, image.Extent().height, 1};
    vkCmdCopyBufferToImage(commands, seed.buffer, image.Image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    if (after != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        ImageBarrier(commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, after,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
}
void CopyImage(VkCommandBuffer commands, const RenderTarget& image, const HostBuffer& output,
               VkDeviceSize offset, VkImageLayout before, VkPipelineStageFlags stage, VkAccessFlags access) {
    ImageBarrier(commands, image, before, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 stage, VK_PIPELINE_STAGE_TRANSFER_BIT, access, VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.bufferOffset = offset;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {image.Extent().width, image.Extent().height, 1};
    vkCmdCopyImageToBuffer(commands, image.Image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, output.buffer, 1, &region);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = output.buffer; host.offset = offset; host.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 0, nullptr, 1, &host, 0, nullptr);
}
struct ResolvePipeline {
    VkDevice device;
    VkExtent2D extent;
    VkRenderPass renderPass{};
    VkFramebuffer framebuffer{};
    VkDescriptorSetLayout descriptorLayout{};
    VkDescriptorPool descriptorPool{};
    VkDescriptorSet descriptors{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
    std::array<VkShaderModule, 4> modules{};
    ResolvePipeline(VkDevice device, const RenderTarget& destination, const RenderTarget& source,
                    const HostBuffer& fault, const ShaderRecompiler::RecompileResult& vertex,
                    const ShaderRecompiler::RecompileResult& fragment,
                    const ShaderRecompiler::RectListShaders& rectangle) : device(device), extent(destination.Extent()) {
        try {
            const std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
                {fragment.bindings.at(0).binding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
                {rectangle.control.bindings.at(0).binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, nullptr}
            }};
            VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            dl.bindingCount = bindings.size(); dl.pBindings = bindings.data();
            Check(vkCreateDescriptorSetLayout(device, &dl, nullptr, &descriptorLayout), "probe vkCreateDescriptorSetLayout");
            const std::array<VkDescriptorPoolSize, 2> sizes{{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}}};
            VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            dp.maxSets = 1; dp.poolSizeCount = sizes.size(); dp.pPoolSizes = sizes.data();
            Check(vkCreateDescriptorPool(device, &dp, nullptr, &descriptorPool), "probe vkCreateDescriptorPool");
            VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            da.descriptorPool = descriptorPool; da.descriptorSetCount = 1; da.pSetLayouts = &descriptorLayout;
            Check(vkAllocateDescriptorSets(device, &da, &descriptors), "probe vkAllocateDescriptorSets");
            const VkDescriptorImageInfo image{VK_NULL_HANDLE, source.SampledView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            const VkDescriptorBufferInfo buffer{fault.buffer, 0, 256};
            std::array<VkWriteDescriptorSet, 2> writes{};
            for (auto& w : writes) { w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w.dstSet = descriptors; w.descriptorCount = 1; }
            writes[0].dstBinding = bindings[0].binding; writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; writes[0].pImageInfo = &image;
            writes[1].dstBinding = bindings[1].binding; writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[1].pBufferInfo = &buffer;
            vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
            const VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, 16};
            VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            pl.setLayoutCount = 1; pl.pSetLayouts = &descriptorLayout; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &push;
            Check(vkCreatePipelineLayout(device, &pl, nullptr, &layout), "probe vkCreatePipelineLayout");
            const VkAttachmentDescription attachment{0, destination.Format(), VK_SAMPLE_COUNT_1_BIT,
                VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            const VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &reference;
            VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
            rp.attachmentCount = 1; rp.pAttachments = &attachment; rp.subpassCount = 1; rp.pSubpasses = &subpass;
            Check(vkCreateRenderPass(device, &rp, nullptr, &renderPass), "probe vkCreateRenderPass");
            const auto view = destination.View();
            VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fb.renderPass = renderPass; fb.attachmentCount = 1; fb.pAttachments = &view;
            fb.width = extent.width; fb.height = extent.height; fb.layers = 1;
            Check(vkCreateFramebuffer(device, &fb, nullptr, &framebuffer), "probe vkCreateFramebuffer");
            const std::array<const ShaderRecompiler::RecompileResult*, 4> code{&vertex, &rectangle.control, &rectangle.evaluation, &fragment};
            const std::array<VkShaderStageFlagBits, 4> types{VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};
            std::array<VkPipelineShaderStageCreateInfo, 4> stages{};
            for (unsigned i = 0; i < code.size(); ++i) {
                const auto& words = code[i]->spirv.Words();
                VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
                sm.codeSize = words.size()*4; sm.pCode = words.data();
                Check(vkCreateShaderModule(device, &sm, nullptr, &modules[i]), "probe vkCreateShaderModule");
                stages[i] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[i].stage = types[i]; stages[i].module = modules[i]; stages[i].pName = "main";
            }
            VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            ia.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
            VkPipelineTessellationStateCreateInfo ts{VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO};
            ts.patchControlPoints = 3;
            const VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
            const VkRect2D scissor{{0, 0}, extent};
            VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            vp.viewportCount = 1; vp.pViewports = &viewport; vp.scissorCount = 1; vp.pScissors = &scissor;
            VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1;
            VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
            ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineColorBlendAttachmentState ba{};
            ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            bs.attachmentCount = 1; bs.pAttachments = &ba;
            VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            gp.stageCount = stages.size(); gp.pStages = stages.data(); gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia;
            gp.pTessellationState = &ts; gp.pViewportState = &vp; gp.pRasterizationState = &rs;
            gp.pMultisampleState = &ms; gp.pColorBlendState = &bs; gp.layout = layout; gp.renderPass = renderPass;
            Check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeline), "probe vkCreateGraphicsPipelines");
        } catch (...) { release(); throw; }
    }
    ~ResolvePipeline() { release(); }
    void release() {
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        for (auto module : modules) if (module) vkDestroyShaderModule(device, module, nullptr);
        if (framebuffer) vkDestroyFramebuffer(device, framebuffer, nullptr);
        if (renderPass) vkDestroyRenderPass(device, renderPass, nullptr);
        if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
        if (descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        if (descriptorLayout) vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
    }
    void Record(VkCommandBuffer commands, const std::array<float, 4>& bounds) const {
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin.renderPass = renderPass; begin.framebuffer = framebuffer; begin.renderArea = {{0,0},extent};
        vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &descriptors, 0, nullptr);
        vkCmdPushConstants(commands, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 16, bounds.data());
        vkCmdDraw(commands, 3, 1, 0, 0);
        vkCmdEndRenderPass(commands);
    }
};
}
int main() {
    VkInstance instance{};
    VkDevice device{};
    VkCommandPool pool{};
    VkFence fence{};
    int status = 1;
    bool submitted = false;
    try {
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "AnyPS5 fixed color resolve proof";
        application.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &application;
        Check(vkCreateInstance(&info, nullptr, &instance), "probe vkCreateInstance");
        std::uint32_t count{};
        Check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "probe vkEnumeratePhysicalDevices count");
        Require(count != 0, "probe has no Vulkan device");
        std::vector<VkPhysicalDevice> devices(count);
        Check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "probe vkEnumeratePhysicalDevices");
        auto physical = devices.front();
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        Require(properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU, "probe requires a GPU");
        Check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr), "probe device extensions count");
        std::vector<VkExtensionProperties> extensions(count);
        Check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data()), "probe device extensions");
        bool portability = false;
        for (const auto& extension : extensions) portability |= std::string_view(extension.extensionName) == "VK_KHR_portability_subset";
        VkPhysicalDeviceFeatures available{};
        vkGetPhysicalDeviceFeatures(physical, &available);
        Require(available.tessellationShader && available.vertexPipelineStoresAndAtomics,
                "resolve probe requires tessellation and vertex-pipeline storage atomics");
        VkPhysicalDeviceFeatures enabled{};
        enabled.tessellationShader = VK_TRUE;
        enabled.vertexPipelineStoresAndAtomics = VK_TRUE;
        VkPhysicalDevicePortabilitySubsetFeaturesKHR portable{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR};
        if (portability) {
            VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            features.pNext = &portable;
            vkGetPhysicalDeviceFeatures2(physical, &features);
            Require(portable.multisampleArrayImage == VK_TRUE, "probe lacks multisample array images");
            portable = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR};
            portable.multisampleArrayImage = VK_TRUE;
        }
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, queues.data());
        std::uint32_t family = UINT32_MAX;
        for (std::uint32_t i = 0; i < count; ++i) {
            if (queues[i].queueCount && (queues[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) { family = i; break; }
        }
        Require(family != UINT32_MAX, "probe lacks graphics/compute queue");
        const float priority = 1;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceInfo.pEnabledFeatures = &enabled;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        const char* portableName = "VK_KHR_portability_subset";
        if (portability) { deviceInfo.pNext = &portable; deviceInfo.enabledExtensionCount = 1; deviceInfo.ppEnabledExtensionNames = &portableName; }
        Check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "probe vkCreateDevice");
        Context context{};
        context.device = device;
        context.physical = physical;
        context.deviceProc = vkGetDeviceProcAddr;
        context.formatProperties = vkGetPhysicalDeviceFormatProperties;
        context.imageFormatProperties = vkGetPhysicalDeviceImageFormatProperties;
        context.limits = properties.limits;
        context.multisampleArrayImage = true;
        context.tessellationShader = true;
        vkGetPhysicalDeviceMemoryProperties(physical, &context.memory);
        vkGetDeviceQueue(device, family, 0, &context.queue);
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex = family;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        Check(vkCreateCommandPool(device, &poolInfo, nullptr, &pool), "probe vkCreateCommandPool");
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        Check(vkCreateFence(device, &fenceInfo, nullptr, &fence), "probe vkCreateFence");
        VkCommandBufferAllocateInfo commandsInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        commandsInfo.commandPool = pool;
        commandsInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandsInfo.commandBufferCount = 1;
        VkCommandBuffer commands{};
        Check(vkAllocateCommandBuffers(device, &commandsInfo, &commands), "probe vkAllocateCommandBuffers");
        const std::array<std::uint32_t, 2> caps{spv::CapabilityShader, spv::CapabilityTessellation};
        ShaderRecompiler::SpirvTarget target{};
        target.vulkanVersion = VK_API_VERSION_1_1;
        target.spirvVersion = 0x00010300u;
        target.supportedCapabilities = caps;
        const auto& l = properties.limits;
        target.tessellation = ShaderRecompiler::TessellationTargetLimits{
            l.maxTessellationPatchSize, l.maxTessellationControlPerVertexInputComponents,
            l.maxTessellationControlPerVertexOutputComponents, l.maxTessellationControlPerPatchOutputComponents,
            l.maxTessellationControlTotalOutputComponents, l.maxTessellationEvaluationInputComponents,
            l.maxTessellationEvaluationOutputComponents};
        std::printf("[fixed-color-resolve] gpu=%s tessellation=1 storage_atomics=1\n", properties.deviceName);
        std::uint64_t errors = 0, channels = 0, preserved = 0, nativeChannels = 0, ties = 0, nonTies = 0, integers = 0;
        unsigned cases = 0, nativeCases = 0;
        constexpr unsigned width = 17, height = 19;
        const auto offset = std::max<VkDeviceSize>(256, context.limits.minStorageBufferOffsetAlignment);
        constexpr auto pixelBytes = VkDeviceSize(width) * height * 4;
        for (unsigned samples : {2u, 4u, 8u}) for (bool partial : {false, true}) {
            ColorTarget sourceColor{};
            sourceColor.address = sourceColor.surfaceAddress = 0x10000;
            sourceColor.extent = {width, height};
            sourceColor.format = VK_FORMAT_R8G8B8A8_UNORM;
            sourceColor.elementBytes = 4;
            sourceColor.samples = sourceColor.fragments = samples;
            sourceColor.bytes = pixelBytes * samples;
            sourceColor.tileMode = ColorTileMode::RenderTarget;
            RenderTarget sourceTarget(context, sourceColor, false);
            ColorSampleTransfer transfer(context, sourceTarget, sourceColor);
            auto destinationColor = sourceColor;
            destinationColor.samples = destinationColor.fragments = 1;
            destinationColor.bytes = pixelBytes;
            RenderTarget destination(context, destinationColor, false);
            std::unique_ptr<RenderTarget> native;
            if (samples != 8) native = std::make_unique<RenderTarget>(context, destinationColor, false);
            HostBuffer packed(context, offset + transfer.PackedBytes() + 256);
            HostBuffer seed(context, offset + pixelBytes + 256);
            HostBuffer readback(context, offset + pixelBytes + 256);
            HostBuffer nativeReadback(context, offset + pixelBytes + 256);
            HostBuffer fault(context, 256);
            std::memset(packed.mapped, 0xa5, static_cast<std::size_t>(offset + transfer.PackedBytes() + 256));
            std::memset(seed.mapped, 0xa5, static_cast<std::size_t>(offset + pixelBytes + 256));
            std::memset(readback.mapped, 0xa5, static_cast<std::size_t>(offset + pixelBytes + 256));
            std::memset(nativeReadback.mapped, 0xa5, static_cast<std::size_t>(offset + pixelBytes + 256));
            std::memset(fault.mapped, 0, 256);
            std::vector<std::uint32_t> input(width * height * samples), seedPixels(width * height);
            for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
                seedPixels[y * width + x] = Pattern(x, y, 91);
                for (unsigned sample = 0; sample < samples; ++sample) {
                    auto word = Pattern(x, y, sample);
                    if (sample == 0) for (unsigned channel = 0; channel < 4; ++channel) {
                        // The unmodified affine sample sequence always has a half tie.
                        // Vary one sample to also test integer and non-tie means.
                        const auto selector = (x + y*3 + channel) % 3;
                        const auto delta = selector == 0 ? 0u : samples/2 + (selector == 2 ? 1u : 0u);
                        const auto shift = channel*8;
                        const auto value = (((word >> shift) & 255u) + delta) & 255u;
                        word = (word & ~(255u << shift)) | (value << shift);
                    }
                    // Both extrema lie inside full and partial rectangles.
                    if (y == 4 && x == 5) word = 0;
                    if (y == 4 && x == 6) word = 0xffffffffu;
                    input[(y * width + x) * samples + sample] = word;
                }
            }
            std::memcpy(static_cast<unsigned char*>(packed.mapped) + offset, input.data(), input.size() * 4);
            std::memcpy(static_cast<unsigned char*>(seed.mapped) + offset, seedPixels.data(), pixelBytes);
            ShaderRecompiler::RecompileResult vertex{};
            vertex.spirv = std::vector<std::uint32_t>(std::begin(FIXED_RESOLVE_VERTEX_SPV), std::end(FIXED_RESOLVE_VERTEX_SPV));
            vertex.pushConstants.resize(16);
            const auto fragment = BuildColorResolveFragment(sourceColor, vertex);
            const auto rectangle = ShaderRecompiler::BuildRectListShaders(vertex, fragment, target);
            ResolvePipeline pipeline(device, destination, sourceTarget, fault, vertex, fragment, rectangle);
            const unsigned x0 = partial ? 4 : 0, y0 = partial ? 3 : 0;
            const unsigned x1 = partial ? 13 : width, y1 = partial ? 15 : height;
            const std::array<float, 4> bounds{2.f*x0/width-1.f, 2.f*y0/height-1.f,
                                             2.f*x1/width-1.f, 2.f*y1/height-1.f};
            Check(vkResetCommandBuffer(commands, 0), "probe vkResetCommandBuffer");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            Check(vkBeginCommandBuffer(commands, &begin), "probe vkBeginCommandBuffer");
            transfer.RecordUpload(commands, {packed.buffer, offset, transfer.PackedBytes(), VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_WRITE_BIT},
                {VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0},
                {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT});
            SeedImage(commands, destination, seed, offset, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            if (native) SeedImage(commands, *native, seed, offset, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkBufferMemoryBarrier faultVisible{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            faultVisible.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
            faultVisible.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            faultVisible.srcQueueFamilyIndex = faultVisible.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            faultVisible.buffer = fault.buffer; faultVisible.offset = 0; faultVisible.size = 256;
            vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT,
                0, 0, nullptr, 1, &faultVisible, 0, nullptr);
            pipeline.Record(commands, bounds);
            CopyImage(commands, destination, readback, offset, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
            if (native) {
                ImageBarrier(commands, sourceTarget, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
                VkImageResolve region{};
                region.srcSubresource = region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.srcOffset = region.dstOffset = {static_cast<int>(x0), static_cast<int>(y0), 0};
                region.extent = {x1-x0, y1-y0, 1};
                vkCmdResolveImage(commands, sourceTarget.Image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  native->Image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                CopyImage(commands, *native, nativeReadback, offset, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            }
            VkBufferMemoryBarrier hostFault = faultVisible;
            hostFault.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            hostFault.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                0, 0, nullptr, 1, &hostFault, 0, nullptr);
            Check(vkEndCommandBuffer(commands), "probe vkEndCommandBuffer");
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1; submit.pCommandBuffers = &commands;
            Check(vkResetFences(device, 1, &fence), "probe vkResetFences");
            Check(vkQueueSubmit(context.queue, 1, &submit, fence), "probe vkQueueSubmit");
            submitted = true;
            const auto wait = vkWaitForFences(device, 1, &fence, VK_TRUE, 10000000000ull);
            if (wait != VK_SUCCESS) vkDeviceWaitIdle(device);
            submitted = false;
            Check(wait, "probe vkWaitForFences");
            std::uint64_t caseErrors = 0;
            const auto* actual = static_cast<const unsigned char*>(readback.mapped) + offset;
            const auto* nativeActual = static_cast<const unsigned char*>(nativeReadback.mapped) + offset;
            for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) for (unsigned c = 0; c < 4; ++c) {
                const auto index = (y * width + x) * 4 + c;
                const bool covered = x >= x0 && x < x1 && y >= y0 && y < y1;
                unsigned sum = 0;
                for (unsigned sample = 0; sample < samples; ++sample)
                    sum += (input[(y * width + x) * samples + sample] >> (8*c)) & 255u;
                const unsigned expected = covered ? (sum + samples/2) / samples : (seedPixels[y*width+x] >> (8*c)) & 255u;
                const bool tie = covered && sum % samples == samples/2;
                const unsigned tolerance = tie ? 1 : 0;
                const int delta = std::abs(int(actual[index]) - int(expected));
                if (delta > int(tolerance)) {
                    if (caseErrors < 3) std::printf("[fixed-color-resolve] mismatch samples=%u partial=%u x=%u y=%u c=%u expected=%u actual=%u tolerance=%u\n", samples,partial,x,y,c,expected,actual[index],tolerance);
                    ++caseErrors;
                }
                if (native && std::abs(int(nativeActual[index])-int(actual[index])) > int(tolerance)) ++caseErrors;
                ++channels;
                if (!covered) ++preserved;
                if (tie) ++ties;
                if (covered && !tie) ++nonTies;
                if (covered && sum % samples == 0) ++integers;
                if (native) ++nativeChannels;
            }
            for (const HostBuffer* output : {&readback, native ? &nativeReadback : &readback}) {
                const auto* guards = static_cast<const unsigned char*>(output->mapped);
                for (VkDeviceSize i = 0; i < offset; ++i) if (guards[i] != 0xa5) ++caseErrors;
                for (VkDeviceSize i = offset+pixelBytes; i < offset+pixelBytes+256; ++i) if (guards[i] != 0xa5) ++caseErrors;
            }
            if (*static_cast<const std::uint32_t*>(fault.mapped) != 0) ++caseErrors;
            errors += caseErrors;
            ++cases; if (native) ++nativeCases;
            std::printf("[fixed-color-resolve] samples=%u native=%u groups=%u partial=%u comparison=%s errors=%llu\n",
                samples, static_cast<unsigned>(sourceTarget.NativeSamples()), sourceTarget.Groups(), partial,
                native ? "scalar+vkCmdResolveImage" : "scalar-eight-sample-average",
                static_cast<unsigned long long>(caseErrors));
        }
        Require(ties > 0 && nonTies > 0 && integers > 0, "resolve probe lacks required rounding coverage");
        std::printf("[fixed-color-resolve] cases=%u channels=%llu preserved_channels=%llu native_cases=%u native_channels=%llu rounding_ties=%llu non_tie_means=%llu integer_means=%llu errors=%llu status=%s\n",
            cases,static_cast<unsigned long long>(channels),static_cast<unsigned long long>(preserved),nativeCases,
            static_cast<unsigned long long>(nativeChannels),static_cast<unsigned long long>(ties),static_cast<unsigned long long>(nonTies),
            static_cast<unsigned long long>(integers),static_cast<unsigned long long>(errors),errors?"FAIL":"PASS");
        status = errors ? 1 : 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "[fixed-color-resolve] failed: %s\n", error.what()); }
    if (device) {
        if (submitted) vkDeviceWaitIdle(device);
        if (fence) vkDestroyFence(device, fence, nullptr);
        if (pool) vkDestroyCommandPool(device, pool, nullptr);
        vkDestroyDevice(device, nullptr);
    }
    if (instance) vkDestroyInstance(instance, nullptr);
    return status;
}
