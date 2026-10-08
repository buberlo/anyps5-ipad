#ifndef APS5_RENDER_TARGET_FIXTURE_H
#define APS5_RENDER_TARGET_FIXTURE_H
#include <vulkan/vulkan.h>
#ifdef __cplusplus
extern "C" {
#endif
// Own the actual AnyPS5 renderer resource. The surrounding synthetic draw still
// does not execute guest shaders, guest depth/stencil, or guest memory transfers.
VkResult aps5_create_color_target(VkPhysicalDevice physical, VkDevice device,
    uint32_t width, uint32_t height, uint32_t logical_samples, VkBool32 array_enabled,
    void **owner, VkImage *image, VkImageView *first, VkImageView *second, VkImageView *sampled);
void aps5_destroy_color_target(void *owner);
#ifdef __cplusplus
}
#endif
#endif
