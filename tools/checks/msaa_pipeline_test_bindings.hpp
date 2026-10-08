// Explicit descriptor-resource boundary for the pipeline structure test. Real
// Vulkan objects, shader modules, execution and descriptor binding are not
// exercised; Pipeline.cpp and the state decoder themselves are production code.
#pragma once
#include "prx/libSceAgcDriver/Graphics/include/State.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Shaders.hpp"
namespace AgcDriver::Graphics {
class StorageTexture {};
class ShaderResources {
public:
    VkDescriptorSetLayout Layout() const { return VK_NULL_HANDLE; }
    std::span<const std::uint32_t> LayoutKey() const { return {}; }
};
}
