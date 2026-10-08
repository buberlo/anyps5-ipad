#include "render_target_fixture.h"
#include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"
#include <cstdio>
#include <memory>

extern "C" VkResult aps5_create_color_target(VkPhysicalDevice physical, VkDevice device,
    uint32_t width, uint32_t height, uint32_t logical_samples, VkBool32 array_enabled,
    void **owner, VkImage *image, VkImageView *first, VkImageView *second, VkImageView *sampled) {
    try {
        AgcDriver::Graphics::Context context{};
        context.device = device;
        context.physical = physical;
        context.deviceProc = vkGetDeviceProcAddr;
        context.formatProperties = vkGetPhysicalDeviceFormatProperties;
        context.imageFormatProperties = vkGetPhysicalDeviceImageFormatProperties;
        context.multisampleArrayImage = array_enabled == VK_TRUE;
        vkGetPhysicalDeviceMemoryProperties(physical, &context.memory);
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        context.limits = properties.limits;
        AgcDriver::Graphics::ColorTarget color{};
        color.extent = {width, height};
        color.format = VK_FORMAT_R32_SFLOAT;
        color.bytes = static_cast<std::size_t>(width) * height * 4u * logical_samples;
        color.samples = color.fragments = logical_samples;
        auto target = std::make_unique<AgcDriver::Graphics::RenderTarget>(context, color, false);
        if (target->NativeSamples() != VK_SAMPLE_COUNT_4_BIT || target->Groups() != (logical_samples == 8u ? 2u : 1u)) {
            std::fprintf(stderr, "[msaa-split] production resource mapping differs\n");
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        *image = target->Image();
        *first = target->View();
        *second = target->Groups() == 2u ? target->View(1u) : VK_NULL_HANDLE;
        *sampled = target->SampledView();
        *owner = target.release();
        std::printf("[msaa-split] production_resource=RenderTarget logical_samples=%u native_samples=4 groups=%u\n", logical_samples, logical_samples / 4u);
        return VK_SUCCESS;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[msaa-split] production resource failed: %s\n", error.what());
        return VK_ERROR_INITIALIZATION_FAILED;
    }
}
extern "C" void aps5_destroy_color_target(void *owner) {
    delete static_cast<AgcDriver::Graphics::RenderTarget*>(owner);
}
