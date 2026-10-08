// Original, offscreen exact-RGBA8 sample transfer test. Runs real RenderTarget and
// ColorSampleTransfer code; it does not run a guest draw/shader/cache/resolve path.
#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include "prx/libSceAgcDriver/Graphics/include/ColorSampleTransfer.hpp"
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
std::uint32_t Pattern(unsigned x, unsigned y, unsigned sample) {
    return ((x * 29u + y * 131u + sample * 17u) & 255u) |
        (((x * 73u ^ y * 149u ^ sample * 37u) & 255u) << 8) |
        ((((x + y + sample) * 111u) & 255u) << 16) |
        ((255u - ((x * 31u + y * 17u + sample * 13u) & 255u)) << 24);
}
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
        application.pApplicationName = "AnyPS5 color sample transfer proof";
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
        std::uint64_t totalWords = 0, errors = 0;
        unsigned cases = 0;
        constexpr unsigned width = 17, height = 19;
        std::array<std::set<unsigned>, 4> components;
        for (unsigned samples : {2u, 4u, 8u}) for (bool alias : {false, true}) {
            ColorTarget color{};
            color.extent = {width, height};
            color.format = VK_FORMAT_R8G8B8A8_UNORM;
            color.samples = color.fragments = samples;
            color.bytes = width * height * samples * 4;
            RenderTarget target(context, color, false);
            ColorSampleTransfer transfer(context, target, color);
            const auto offset = std::max<VkDeviceSize>(256, context.limits.minStorageBufferOffsetAlignment);
            const auto bytes = offset + transfer.PackedBytes() + 256;
            HostBuffer source(context, bytes);
            std::unique_ptr<HostBuffer> separate;
            if (!alias) separate = std::make_unique<HostBuffer>(context, bytes);
            HostBuffer& output = alias ? source : *separate;
            std::memset(source.mapped, 0xa5, static_cast<std::size_t>(bytes));
            if (!alias) std::memset(output.mapped, 0xa5, static_cast<std::size_t>(bytes));
            std::vector<std::uint32_t> expected(width * height * samples);
            for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) for (unsigned sample = 0; sample < samples; ++sample) {
                auto word = Pattern(x, y, sample);
                expected[(y * width + x) * samples + sample] = word;
                for (unsigned component = 0; component < 4; ++component) components[component].insert((word >> (8 * component)) & 255u);
            }
            std::memcpy(static_cast<unsigned char*>(source.mapped) + offset, expected.data(), static_cast<std::size_t>(transfer.PackedBytes()));
            Check(vkResetCommandBuffer(commands, 0), "probe vkResetCommandBuffer");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            Check(vkBeginCommandBuffer(commands, &begin), "probe vkBeginCommandBuffer");
            const ColorSampleImageAccess attachment{VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
            transfer.RecordUpload(commands, {source.buffer, offset, transfer.PackedBytes(), VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_WRITE_BIT},
                {VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0}, attachment);
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
                {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT},
                VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
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
            const auto* actual = reinterpret_cast<const std::uint32_t*>(static_cast<const unsigned char*>(output.mapped) + offset);
            std::uint64_t caseErrors = 0;
            for (std::size_t i = 0; i < expected.size(); ++i) if (actual[i] != expected[i]) {
                if (caseErrors < 3) std::printf("[color-sample-transfer] mismatch samples=%u alias=%u word=%llu expected=%08x actual=%08x\n", samples, alias, static_cast<unsigned long long>(i), expected[i], actual[i]);
                ++caseErrors;
            }
            const auto* guards = static_cast<const unsigned char*>(output.mapped);
            for (VkDeviceSize i = 0; i < offset; ++i) if (guards[i] != 0xa5) ++caseErrors;
            for (VkDeviceSize i = offset + transfer.PackedBytes(); i < bytes; ++i) if (guards[i] != 0xa5) ++caseErrors;
            errors += caseErrors;
            totalWords += expected.size();
            ++cases;
            std::printf("[color-sample-transfer] samples=%u native=%u groups=%u alias=%u words=%llu errors=%llu\n", samples, static_cast<unsigned>(target.NativeSamples()), target.Groups(), alias,
                static_cast<unsigned long long>(expected.size()), static_cast<unsigned long long>(caseErrors));
        }
        std::printf("[color-sample-transfer] cases=%u words=%llu rgba_component_values=%zu,%zu,%zu,%zu errors=%llu status=%s\n", cases,
            static_cast<unsigned long long>(totalWords), components[0].size(), components[1].size(), components[2].size(), components[3].size(),
            static_cast<unsigned long long>(errors), errors ? "FAIL" : "PASS");
        status = errors ? 1 : 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "[color-sample-transfer] failed: %s\n", error.what()); }
    if (device) {
        if (submitted) vkDeviceWaitIdle(device);
        if (fence) vkDestroyFence(device, fence, nullptr);
        if (pool) vkDestroyCommandPool(device, pool, nullptr);
        vkDestroyDevice(device, nullptr);
    }
    if (instance) vkDestroyInstance(instance, nullptr);
    return status;
}
