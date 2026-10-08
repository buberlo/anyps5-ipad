// Original, offscreen exact-S8 sample transfer and unchanged-Z test. Runs production
// StencilSampleTransfer code; it does not run a guest draw/shader/cache/resolve path.
#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include "prx/libSceAgcDriver/Graphics/include/StencilSampleTransfer.hpp"
#include "StencilSampleDepthProbe_spv.h"
#include <algorithm>
#include <cmath>
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
            info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
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

struct DepthImage {
    VkDevice device;
    VkImage image{};
    VkDeviceMemory memory{};
    VkFormat format;
    unsigned nativeSamples, groups;
    std::array<VkImageView, 2> attachments{};
    VkImageView depthView{};
    DepthImage(const Context& context, VkExtent2D extent, VkFormat format, unsigned samples)
        : device(context.device), format(format), nativeSamples(std::min(samples, 4u)), groups(samples/nativeSamples) {
        try {
            VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            info.flags = VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT;
            info.imageType = VK_IMAGE_TYPE_2D;
            info.format = format;
            info.extent = {extent.width, extent.height, 1};
            info.mipLevels = 1;
            info.arrayLayers = groups;
            info.samples = static_cast<VkSampleCountFlagBits>(nativeSamples);
            info.tiling = VK_IMAGE_TILING_OPTIMAL;
            info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            Check(vkCreateImage(device, &info, nullptr, &image), "probe vkCreateImage depth/stencil");
            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(device, image, &requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = context.MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            Check(vkAllocateMemory(device, &allocation, nullptr, &memory), "probe vkAllocateMemory depth/stencil");
            Check(vkBindImageMemory(device, image, memory, 0), "probe vkBindImageMemory depth/stencil");
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = image;
            view.format = format;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};
            for (unsigned group = 0; group < groups; ++group) {
                view.subresourceRange.baseArrayLayer = group;
                Check(vkCreateImageView(device, &view, nullptr, &attachments[group]), "probe vkCreateImageView attachment");
            }
            view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, groups};
            Check(vkCreateImageView(device, &view, nullptr, &depthView), "probe vkCreateImageView depth sampling");
        } catch (...) { release(); throw; }
    }
    ~DepthImage() { release(); }
    void release() {
        if (depthView) vkDestroyImageView(device, depthView, nullptr);
        for (auto view : attachments) if (view) vkDestroyImageView(device, view, nullptr);
        if (image) vkDestroyImage(device, image, nullptr);
        if (memory) vkFreeMemory(device, memory, nullptr);
    }
};
struct DepthReadback {
    VkDevice device;
    VkDescriptorSetLayout descriptors{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
    VkDescriptorPool pool{};
    VkDescriptorSet set{};
    VkBuffer output;
    DepthReadback(const Context& context, VkImageView image, VkBuffer output) : device(context.device), output(output) {
        VkShaderModule module{};
        try {
            const std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
                {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}}};
            VkDescriptorSetLayoutCreateInfo descriptorInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            descriptorInfo.bindingCount = 2;
            descriptorInfo.pBindings = bindings.data();
            Check(vkCreateDescriptorSetLayout(device, &descriptorInfo, nullptr, &descriptors), "probe depth reader descriptors");
            const VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
            VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &descriptors;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &push;
            Check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout), "probe depth reader layout");
            VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            shader.codeSize = sizeof(STENCIL_DEPTH_PROBE_SPV);
            shader.pCode = STENCIL_DEPTH_PROBE_SPV;
            Check(vkCreateShaderModule(device, &shader, nullptr, &module), "probe depth reader shader");
            VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            compute.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            compute.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            compute.stage.module = module;
            compute.stage.pName = "main";
            compute.layout = layout;
            Check(vkCreateComputePipelines(device, context.pipelineCache, 1, &compute, nullptr, &pipeline), "probe depth reader pipeline");
            vkDestroyShaderModule(device, module, nullptr);
            module = VK_NULL_HANDLE;
            const std::array<VkDescriptorPoolSize, 2> sizes{{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}}};
            VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            poolInfo.maxSets = 1;
            poolInfo.poolSizeCount = 2;
            poolInfo.pPoolSizes = sizes.data();
            Check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool), "probe depth reader pool");
            VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocate.descriptorPool = pool;
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &descriptors;
            Check(vkAllocateDescriptorSets(device, &allocate, &set), "probe depth reader set");
            const VkDescriptorImageInfo sampled{VK_NULL_HANDLE, image, VK_IMAGE_LAYOUT_GENERAL};
            const VkDescriptorBufferInfo buffer{output, 0, VK_WHOLE_SIZE};
            std::array<VkWriteDescriptorSet, 2> writes{};
            writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &sampled, nullptr, nullptr};
            writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &buffer, nullptr};
            vkUpdateDescriptorSets(device, 2, writes.data(), 0, nullptr);
        } catch (...) { if (module) vkDestroyShaderModule(device, module, nullptr); release(); throw; }
    }
    ~DepthReadback() { release(); }
    void release() {
        if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
        if (descriptors) vkDestroyDescriptorSetLayout(device, descriptors, nullptr);
    }
    void Record(VkCommandBuffer commands, unsigned width, unsigned samples, unsigned nativeSamples, unsigned count) {
        VkBufferMemoryBarrier buffer{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        buffer.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        buffer.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        buffer.srcQueueFamilyIndex = buffer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        buffer.buffer = output;
        buffer.offset = 0;
        buffer.size = count * sizeof(float);
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &buffer, 0, nullptr);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
        const std::array<unsigned, 4> parameters{width, samples, nativeSamples, count};
        vkCmdPushConstants(commands, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(parameters), parameters.data());
        vkCmdDispatch(commands, (count + 63u) / 64u, 1, 1);
        buffer.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        buffer.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &buffer, 0, nullptr);
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
        application.pApplicationName = "AnyPS5 stencil sample transfer proof";
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
        bool portability = false, sampleLocations = false;
        for (const auto& extension : extensions) {
            portability |= std::string_view(extension.extensionName) == "VK_KHR_portability_subset";
            sampleLocations |= std::string_view(extension.extensionName) == "VK_EXT_sample_locations";
        }
        Require(sampleLocations, "probe requires VK_EXT_sample_locations");
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
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        const std::array<const char*, 2> extensionNames{"VK_EXT_sample_locations", "VK_KHR_portability_subset"};
        deviceInfo.enabledExtensionCount = portability ? 2u : 1u;
        deviceInfo.ppEnabledExtensionNames = extensionNames.data();
        if (portability) deviceInfo.pNext = &portable;
        Check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "probe vkCreateDevice");
        Context context{};
        context.device = device;
        context.physical = physical;
        context.deviceProc = vkGetDeviceProcAddr;
        context.formatProperties = vkGetPhysicalDeviceFormatProperties;
        context.imageFormatProperties = vkGetPhysicalDeviceImageFormatProperties;
        context.limits = properties.limits;
        context.multisampleArrayImage = true;
        context.sampleLocations = true;
        VkPhysicalDeviceProperties2 extendedProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        extendedProperties.pNext = &context.sampleLocationProperties;
        vkGetPhysicalDeviceProperties2(physical, &extendedProperties);
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
        std::uint64_t totalBytes = 0, depthSamples = 0, errors = 0;
        unsigned cases = 0;
        constexpr unsigned width = 17, height = 19;
        std::set<unsigned> values;
        const std::array<VkSampleLocationEXT, 8> positions{{
            {2.f/16, 2.f/16}, {6.f/16, 2.f/16}, {10.f/16, 2.f/16}, {14.f/16, 2.f/16},
            {2.f/16, 10.f/16}, {6.f/16, 10.f/16}, {10.f/16, 10.f/16}, {14.f/16, 10.f/16}}};
        for (unsigned samples : {2u, 4u, 8u}) for (bool alias : {false, true}) {
            DepthImage target(context, {width, height}, VK_FORMAT_D32_SFLOAT_S8_UINT, samples);
            StencilSampleTransfer transfer(context, target.image, target.format,
                std::span(target.attachments.data(), target.groups), {width, height}, samples,
                std::span(positions.data(), samples));
            const auto offset = std::max<VkDeviceSize>(256, context.limits.minStorageBufferOffsetAlignment);
            const auto bytes = offset + transfer.PackedBytes() + 256;
            HostBuffer source(context, bytes);
            std::unique_ptr<HostBuffer> separate;
            if (!alias) separate = std::make_unique<HostBuffer>(context, bytes);
            HostBuffer& output = alias ? source : *separate;
            std::memset(source.mapped, 0xa5, static_cast<std::size_t>(bytes));
            if (!alias) std::memset(output.mapped, 0xa5, static_cast<std::size_t>(bytes));
            std::vector<unsigned char> expected(width * height * samples);
            for (unsigned i = 0; i < expected.size(); ++i) { expected[i] = static_cast<unsigned char>((i * 73u + 19u) & 255u); values.insert(expected[i]); }
            std::memcpy(static_cast<unsigned char*>(source.mapped) + offset, expected.data(), expected.size());
            HostBuffer depthOutput(context, expected.size() * sizeof(float));
            std::memset(depthOutput.mapped, 0xde, expected.size() * sizeof(float));
            DepthReadback reader(context, target.depthView, depthOutput.buffer);
            Check(vkResetCommandBuffer(commands, 0), "probe vkResetCommandBuffer");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            Check(vkBeginCommandBuffer(commands, &begin), "probe vkBeginCommandBuffer");
            VkImageMemoryBarrier initialize{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            initialize.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            initialize.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            initialize.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            initialize.srcQueueFamilyIndex = initialize.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            initialize.image = target.image;
            initialize.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, target.groups};
            vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &initialize);
            const VkClearDepthStencilValue initial{0.625f, 0x3cu};
            vkCmdClearDepthStencilImage(commands, target.image, VK_IMAGE_LAYOUT_GENERAL, &initial, 1, &initialize.subresourceRange);
            const ColorSampleImageAccess attachment{VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
            transfer.RecordUpload(commands, {source.buffer, offset, transfer.PackedBytes(), VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_WRITE_BIT},
                {VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT}, attachment);
            ColorSampleBufferAccess outputBefore{output.buffer, offset, transfer.PackedBytes(), VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_WRITE_BIT};
            if (alias) {
                // Destroy the source buffer's packed values after fragment reads complete.
                // A missing/no-op readback can therefore never pass by observing the input.
                VkBufferMemoryBarrier writable{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
                writable.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                writable.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                writable.srcQueueFamilyIndex = writable.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                writable.buffer = source.buffer;
                writable.offset = offset;
                writable.size = transfer.PackedBytes();
                vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &writable, 0, nullptr);
                vkCmdFillBuffer(commands, source.buffer, offset, transfer.PackedBytes(), 0xfefefefe);
                outputBefore.stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                outputBefore.access = VK_ACCESS_TRANSFER_WRITE_BIT;
            }
            transfer.RecordReadback(commands, outputBefore, attachment,
                {VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT},
                VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
            reader.Record(commands, width, samples, target.nativeSamples, static_cast<unsigned>(expected.size()));
            Check(vkEndCommandBuffer(commands), "probe vkEndCommandBuffer");
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &commands;
            Check(vkResetFences(device, 1, &fence), "probe vkResetFences");
            Check(vkQueueSubmit(context.queue, 1, &submit, fence), "probe vkQueueSubmit");
            submitted = true;
            // Failure must wait idle before local owners unwind, too.
            auto wait = vkWaitForFences(device, 1, &fence, VK_TRUE, 10000000000ull);
            if (wait != VK_SUCCESS) vkDeviceWaitIdle(device);
            submitted = false;
            Check(wait, "probe vkWaitForFences");
            const auto* actual = static_cast<const unsigned char*>(output.mapped) + offset;
            std::uint64_t caseErrors = 0;
            for (std::size_t i = 0; i < expected.size(); ++i) if (actual[i] != expected[i]) {
                if (caseErrors < 3) std::printf("[stencil-sample-transfer] mismatch samples=%u alias=%u byte=%llu expected=%02x actual=%02x\n", samples, alias, static_cast<unsigned long long>(i), expected[i], actual[i]);
                ++caseErrors;
            }
            const auto* guards = static_cast<const unsigned char*>(output.mapped);
            for (VkDeviceSize i = 0; i < offset; ++i) if (guards[i] != 0xa5) ++caseErrors;
            for (VkDeviceSize i = offset + expected.size(); i < offset + transfer.PackedBytes(); ++i) if (guards[i] != (alias ? 0xfe : 0xa5)) ++caseErrors;
            for (VkDeviceSize i = offset + transfer.PackedBytes(); i < bytes; ++i) if (guards[i] != 0xa5) ++caseErrors;
            const auto* z = static_cast<const float*>(depthOutput.mapped);
            std::uint64_t zErrors = 0;
            for (std::size_t i = 0; i < expected.size(); ++i) if (!std::isfinite(z[i]) || std::fabs(z[i] - 0.625f) > 0.000001f) ++zErrors;
            caseErrors += zErrors;
            errors += caseErrors;
            totalBytes += expected.size();
            depthSamples += expected.size();
            ++cases;
            std::printf("[stencil-sample-transfer] samples=%u native=%u groups=%u alias=%u bytes=%llu depth_errors=%llu errors=%llu\n", samples, target.nativeSamples, target.groups, alias,
                static_cast<unsigned long long>(expected.size()), static_cast<unsigned long long>(zErrors), static_cast<unsigned long long>(caseErrors));
        }
        std::printf("[stencil-sample-transfer] cases=%u bytes=%llu depth_samples=%llu s8_values=%zu errors=%llu status=%s\n", cases,
            static_cast<unsigned long long>(totalBytes), static_cast<unsigned long long>(depthSamples), values.size(),
            static_cast<unsigned long long>(errors), errors ? "FAIL" : "PASS");
        status = errors ? 1 : 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "[stencil-sample-transfer] failed: %s\n", error.what()); }
    if (device) {
        if (submitted) vkDeviceWaitIdle(device);
        if (fence) vkDestroyFence(device, fence, nullptr);
        if (pool) vkDestroyCommandPool(device, pool, nullptr);
        vkDestroyDevice(device, nullptr);
    }
    if (instance) vkDestroyInstance(instance, nullptr);
    return status;
}
