// Actual RenderTarget ownership/limits under a deterministic Vulkan adapter.
// This checks API contracts and unwind; GPU pixels are checked by the separate probe.
#include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"
#include <cassert>
#include <cstdio>
#include <set>
#include <string_view>
#include <type_traits>
using namespace AgcDriver::Graphics;
namespace {
std::set<std::uintptr_t> images, memory, views;
std::uintptr_t nextHandle=1;
unsigned step=0, failStep=0, created=0;
VkSampleCountFlagBits expectedSamples=VK_SAMPLE_COUNT_1_BIT;
unsigned expectedLayers=1;
VkSampleCountFlags supportedSamples=VK_SAMPLE_COUNT_1_BIT|VK_SAMPLE_COUNT_2_BIT|VK_SAMPLE_COUNT_4_BIT;
unsigned supportedLayers=2;
VkFormatFeatureFlags supportedFeatures=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
template<class T> T handle(std::uintptr_t value) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(value);
    else return static_cast<T>(value);
}
template<class T> std::uintptr_t number(T value) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<std::uintptr_t>(value);
    else return static_cast<std::uintptr_t>(value);
}
bool fails(){return ++step==failStep;}
VKAPI_ATTR void VKAPI_CALL format(VkPhysicalDevice,VkFormat,VkFormatProperties* out) { *out={0,supportedFeatures,0}; }
VKAPI_ATTR VkResult VKAPI_CALL imageFormat(VkPhysicalDevice,VkFormat,VkImageType,VkImageTiling,VkImageUsageFlags,VkImageCreateFlags,VkImageFormatProperties* out) {
    *out={{4096,4096,1},1,supportedLayers,supportedSamples,1ull<<30}; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL createImage(VkDevice,const VkImageCreateInfo* info,const VkAllocationCallbacks*,VkImage* out) {
    assert(info->samples==expectedSamples && info->arrayLayers==expectedLayers && info->mipLevels==1);
    assert((info->usage&VK_IMAGE_USAGE_SAMPLED_BIT)!=0 || expectedSamples==VK_SAMPLE_COUNT_1_BIT);
    if(fails())return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    auto id=nextHandle++;images.insert(id);*out=handle<VkImage>(id);++created;return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL requirements(VkDevice,VkImage,VkMemoryRequirements* out){*out={65536,4096,1};}
VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice,const VkMemoryAllocateInfo*,const VkAllocationCallbacks*,VkDeviceMemory* out){
    if(fails())return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    auto id=nextHandle++;memory.insert(id);*out=handle<VkDeviceMemory>(id);return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL bind(VkDevice,VkImage,VkDeviceMemory,VkDeviceSize){return fails()?VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL createView(VkDevice,const VkImageViewCreateInfo* info,const VkAllocationCallbacks*,VkImageView* out){
    if(fails())return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if(info->viewType==VK_IMAGE_VIEW_TYPE_2D_ARRAY) assert(info->subresourceRange.baseArrayLayer==0 && info->subresourceRange.layerCount==expectedLayers);
    else assert(info->viewType==VK_IMAGE_VIEW_TYPE_2D && info->subresourceRange.baseArrayLayer<expectedLayers && info->subresourceRange.layerCount==1);
    auto id=nextHandle++;views.insert(id);*out=handle<VkImageView>(id);return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroyView(VkDevice,VkImageView id,const VkAllocationCallbacks*){assert(views.erase(number(id))==1);}
VKAPI_ATTR void VKAPI_CALL destroyImage(VkDevice,VkImage id,const VkAllocationCallbacks*){assert(images.erase(number(id))==1);}
VKAPI_ATTR void VKAPI_CALL freeMemory(VkDevice,VkDeviceMemory id,const VkAllocationCallbacks*){assert(memory.erase(number(id))==1);}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice,const char* raw){
    const std::string_view name=raw;
#define ENTRY(n,f) if(name==n)return reinterpret_cast<PFN_vkVoidFunction>(f)
    ENTRY("vkCreateImage",createImage);ENTRY("vkGetImageMemoryRequirements",requirements);
    ENTRY("vkAllocateMemory",allocate);ENTRY("vkBindImageMemory",bind);ENTRY("vkCreateImageView",createView);
    ENTRY("vkDestroyImageView",destroyView);ENTRY("vkDestroyImage",destroyImage);ENTRY("vkFreeMemory",freeMemory);
#undef ENTRY
    return nullptr;
}
template<class F> void rejects(F f){bool failed=false;try{f();}catch(const std::runtime_error&){failed=true;}assert(failed);assert(images.empty()&&views.empty()&&memory.empty());}
}
int main(){
    Context context{}; context.device=handle<VkDevice>(1); context.physical=handle<VkPhysicalDevice>(1);
    context.deviceProc=resolve;context.formatProperties=format;context.imageFormatProperties=imageFormat;
    context.memory.memoryTypeCount=1;context.memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    context.limits.maxFramebufferWidth=context.limits.maxFramebufferHeight=4096;
    ColorTarget target{};target.extent={16,16};target.format=VK_FORMAT_R8G8B8A8_UNORM;target.bytes=16*16*4;
    unsigned successes=0, failures=0;
    for(auto count:{1u,2u,4u,8u}){
        target.samples=target.fragments=count;expectedSamples=static_cast<VkSampleCountFlagBits>(count>4?4:count);expectedLayers=count/expectedSamples;
        step=failStep=0;
        {
            RenderTarget actual(context,target,true);
            assert(actual.NativeSamples()==expectedSamples && actual.Groups()==expectedLayers);
            assert(actual.Image()!=VK_NULL_HANDLE && actual.View()!=VK_NULL_HANDLE);
            assert((actual.SampledView()!=VK_NULL_HANDLE)==(count>1));
            if(count==8)assert(actual.View()!=actual.View(1));
            bool rangeRejected=false;try{actual.View(expectedLayers);}catch(const std::runtime_error&){rangeRejected=true;}assert(rangeRejected);
        }
        assert(images.empty()&&views.empty()&&memory.empty());++successes;
        const auto steps=3u+expectedLayers+(count>1?1u:0u);
        for(failStep=1;failStep<=steps;++failStep){step=0;rejects([&]{RenderTarget actual(context,target,true);});++failures;}
    }
    failStep=0;const auto previous=created;
    for(auto count:{0u,3u,16u}){target.samples=target.fragments=count;rejects([&]{RenderTarget actual(context,target,false);});++failures;}
    target.samples=8;target.fragments=4;rejects([&]{RenderTarget actual(context,target,false);});++failures;
    target.fragments=8;context.multisampleArrayImage=false;rejects([&]{RenderTarget actual(context,target,false);});++failures;
    context.multisampleArrayImage=true;supportedLayers=1;rejects([&]{RenderTarget actual(context,target,false);});++failures;
    supportedLayers=2;supportedSamples=VK_SAMPLE_COUNT_1_BIT;rejects([&]{RenderTarget actual(context,target,false);});++failures;
    supportedSamples=VK_SAMPLE_COUNT_1_BIT|VK_SAMPLE_COUNT_4_BIT;supportedFeatures&=~VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;rejects([&]{RenderTarget actual(context,target,false);});++failures;
    supportedFeatures|=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;target.extent.width=0;rejects([&]{RenderTarget actual(context,target,false);});++failures;
    target.extent.width=4097;rejects([&]{RenderTarget actual(context,target,false);});++failures;
    target.extent.width=16;context.formatProperties=nullptr;rejects([&]{RenderTarget actual(context,target,false);});++failures;
    assert(created==previous);
    std::printf("RenderTarget contract success_layouts=%u rejected_and_unwound=%u leaked_handles=0 errors=0\n",successes,failures);
}
