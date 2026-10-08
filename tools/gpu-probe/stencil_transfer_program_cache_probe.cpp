// Original native cache-hit/change/clear test of production stencil programs and S8 transfer.
// Independent AMD AddrLib goldens; no DepthSurface/Draw/GuestMemory/iPad qualification.
#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include <addrinterface.h>
#include "prx/libSceAgcDriver/Graphics/include/StencilSampleTransfer.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GpuStencilSampleTiler.hpp"
#include "StencilSampleDepthProbe_spv.h"
#include <cmath>
#include <set>
using namespace AgcDriver::Graphics;
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
void check(VkResult value, const char* reason) { if (value != VK_SUCCESS) throw std::runtime_error(std::string(reason) + " VkResult=" + std::to_string(value)); }
static void* ADDR_API allocate(const ADDR_ALLOCSYSMEM_INPUT* input) { return std::malloc(input->sizeInBytes); }
static ADDR_E_RETURNCODE ADDR_API release(const ADDR_FREESYSMEM_INPUT* input) { std::free(input->pVirtAddr); return ADDR_OK; }
struct Oracle {
    ADDR_HANDLE library{};
    Oracle() {
        ADDR_CREATE_INPUT input{}; input.size=sizeof(input); input.chipEngine=13;
        input.chipFamily=0x8f; input.chipRevision=1; input.regValue.gbAddrConfig=4u|(3u<<6u);
        input.callbacks.allocSysMem=allocate; input.callbacks.freeSysMem=release; input.createFlags.fillSizeFields=1;
        ADDR_CREATE_OUTPUT output{}; output.size=sizeof(output);
        require(AddrCreate(&input,&output)==ADDR_OK,"AddrCreate"); library=output.hLib;
    }
    ~Oracle() { if(library) AddrDestroy(library); }
};
static unsigned graphicsProgramCreations=0;
static unsigned pipelineCreations=0, tilerProgramCreations=0, poolsCreated=0, poolsDestroyed=0;
static VKAPI_ATTR VkResult VKAPI_CALL trackedPipelines(VkDevice device,VkPipelineCache cache,unsigned count,const VkComputePipelineCreateInfo* info,const VkAllocationCallbacks* allocator,VkPipeline* output){
    auto result=vkCreateComputePipelines(device,cache,count,info,allocator,output);if(result==VK_SUCCESS){pipelineCreations+=count;for(unsigned i=0;i<count;++i)if(info[i].stage.pSpecializationInfo&&info[i].stage.pSpecializationInfo->mapEntryCount==1&&info[i].stage.pSpecializationInfo->dataSize==4)++tilerProgramCreations;}return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL trackedGraphics(VkDevice device,VkPipelineCache cache,unsigned count,const VkGraphicsPipelineCreateInfo* info,const VkAllocationCallbacks* allocator,VkPipeline* output){auto result=vkCreateGraphicsPipelines(device,cache,count,info,allocator,output);if(result==VK_SUCCESS)graphicsProgramCreations+=count;return result;}
static VKAPI_ATTR VkResult VKAPI_CALL trackedPool(VkDevice device,const VkDescriptorPoolCreateInfo* info,const VkAllocationCallbacks* allocator,VkDescriptorPool* output){
    auto result=vkCreateDescriptorPool(device,info,allocator,output);if(result==VK_SUCCESS)++poolsCreated;return result;
}
static VKAPI_ATTR void VKAPI_CALL trackedDestroyPool(VkDevice device,VkDescriptorPool pool,const VkAllocationCallbacks* allocator){++poolsDestroyed;vkDestroyDescriptorPool(device,pool,allocator);}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL trackedResolve(VkDevice device,const char* name){
    if(std::strcmp(name,"vkCreateGraphicsPipelines")==0)return reinterpret_cast<PFN_vkVoidFunction>(trackedGraphics);
    if(std::strcmp(name,"vkCreateComputePipelines")==0)return reinterpret_cast<PFN_vkVoidFunction>(trackedPipelines);
    if(std::strcmp(name,"vkCreateDescriptorPool")==0)return reinterpret_cast<PFN_vkVoidFunction>(trackedPool);
    if(std::strcmp(name,"vkDestroyDescriptorPool")==0)return reinterpret_cast<PFN_vkVoidFunction>(trackedDestroyPool);
    return vkGetDeviceProcAddr(device,name);
}
struct Push { std::uint32_t width,height,blocksPerRow,tiledWords,linearWords; };
struct Device {
    VkInstance instance{}; VkPhysicalDevice physical{}; VkDevice device{};
    VkPhysicalDeviceProperties properties{}; VkPhysicalDeviceMemoryProperties memory{};
    VkQueue queue{}; VkCommandPool pool{}; VkCommandBuffer commands{}; VkFence fence{};
    std::unique_ptr<GpuStencilSampleTiler> tiler;
    std::unique_ptr<StencilSampleProgramCache> stencilCache;
    std::vector<std::unique_ptr<GpuStencilSampleTiler::Transfer>> retained;
    Device() {
        try {
            VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion=VK_API_VERSION_1_1; app.pApplicationName="Original sample tiling prototype";
            VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo=&app;
            check(vkCreateInstance(&info,nullptr,&instance),"vkCreateInstance");
            std::uint32_t count=0; check(vkEnumeratePhysicalDevices(instance,&count,nullptr),"vkEnumeratePhysicalDevices");
            require(count!=0,"no GPU"); std::vector<VkPhysicalDevice> devices(count);
            check(vkEnumeratePhysicalDevices(instance,&count,devices.data()),"vkEnumeratePhysicalDevices list"); physical=devices.front();
            vkGetPhysicalDeviceProperties(physical,&properties); vkGetPhysicalDeviceMemoryProperties(physical,&memory);
            require(properties.deviceType!=VK_PHYSICAL_DEVICE_TYPE_CPU,"CPU Vulkan device is not GPU proof");
            vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr); std::vector<VkQueueFamilyProperties> families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data()); std::uint32_t family=UINT32_MAX;
            for(std::uint32_t i=0;i<count;++i) if(families[i].queueCount && (families[i].queueFlags&(VK_QUEUE_COMPUTE_BIT|VK_QUEUE_GRAPHICS_BIT))==(VK_QUEUE_COMPUTE_BIT|VK_QUEUE_GRAPHICS_BIT)) {family=i;break;}
            require(family!=UINT32_MAX,"no compute queue");
            check(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,nullptr),"vkEnumerateDeviceExtensionProperties");
            std::vector<VkExtensionProperties> extensions(count); check(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,extensions.data()),"device extensions");
            bool portability=false,sampleLocations=false;
            for(const auto& extension:extensions) { portability|=std::strcmp(extension.extensionName,"VK_KHR_portability_subset")==0; sampleLocations|=std::strcmp(extension.extensionName,"VK_EXT_sample_locations")==0; }
            require(sampleLocations,"VK_EXT_sample_locations unavailable");
            VkPhysicalDevicePortabilitySubsetFeaturesKHR portableFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR};
            if(portability) { VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&portableFeatures;vkGetPhysicalDeviceFeatures2(physical,&features);require(portableFeatures.multisampleArrayImage,"multisample array image unavailable");portableFeatures={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR};portableFeatures.multisampleArrayImage=VK_TRUE; }
            const float priority=1; VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            queueInfo.queueFamilyIndex=family;queueInfo.queueCount=1;queueInfo.pQueuePriorities=&priority;
            VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; deviceInfo.queueCreateInfoCount=1;deviceInfo.pQueueCreateInfos=&queueInfo;
            const std::array<const char*,2> enabledExtensions{"VK_EXT_sample_locations","VK_KHR_portability_subset"};
            deviceInfo.enabledExtensionCount=portability?2:1;deviceInfo.ppEnabledExtensionNames=enabledExtensions.data();if(portability)deviceInfo.pNext=&portableFeatures;
            check(vkCreateDevice(physical,&deviceInfo,nullptr,&device),"vkCreateDevice"); vkGetDeviceQueue(device,family,0,&queue);
            const auto& limits=properties.limits;
            require(limits.maxPushConstantsSize>=sizeof(Push) && limits.maxComputeWorkGroupSize[0]>=8 && limits.maxComputeWorkGroupSize[1]>=8 && limits.maxComputeWorkGroupInvocations>=64 && limits.maxComputeWorkGroupCount[2]>=8,"compute limits");
            VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; poolInfo.queueFamilyIndex=family;poolInfo.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            check(vkCreateCommandPool(device,&poolInfo,nullptr,&pool),"vkCreateCommandPool");
            VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocation.commandPool=pool;allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocation.commandBufferCount=1;
            check(vkAllocateCommandBuffers(device,&allocation,&commands),"vkAllocateCommandBuffers");
            VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(device,&fenceInfo,nullptr,&fence),"vkCreateFence");
            stencilCache=std::make_unique<StencilSampleProgramCache>(context(),2);
            tiler=std::make_unique<GpuStencilSampleTiler>(context());
            std::printf("[s8-tiling] device=%s api=%u.%u.%u driver=%u max_storage_range=%u production_helper=1\n",properties.deviceName,VK_VERSION_MAJOR(properties.apiVersion),VK_VERSION_MINOR(properties.apiVersion),VK_VERSION_PATCH(properties.apiVersion),properties.driverVersion,limits.maxStorageBufferRange);
        } catch(...) {cleanup();throw;}
    }
    ~Device(){cleanup();}
    void cleanup(){if(device){vkDeviceWaitIdle(device);retained.clear();tiler.reset();if(stencilCache)stencilCache->Clear();stencilCache.reset();if(fence)vkDestroyFence(device,fence,nullptr);if(pool)vkDestroyCommandPool(device,pool,nullptr);vkDestroyDevice(device,nullptr);device={};}if(instance){vkDestroyInstance(instance,nullptr);instance={};}}
    Context context() const { Context value{};value.device=device;value.physical=physical;value.queue=queue;value.pool=pool;value.deviceProc=trackedResolve;value.stencilSamplePrograms=stencilCache.get();value.formatProperties=vkGetPhysicalDeviceFormatProperties;value.imageFormatProperties=vkGetPhysicalDeviceImageFormatProperties;value.memory=memory;value.limits=properties.limits;value.multisampleArrayImage=true;value.sampleLocations=true;VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};properties2.pNext=&value.sampleLocationProperties;vkGetPhysicalDeviceProperties2(physical,&properties2);return value; }
    std::uint32_t memoryType(std::uint32_t bits)const {for(std::uint32_t i=0;i<memory.memoryTypeCount;++i)if((bits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))return i;throw std::runtime_error("no host coherent memory");}
    void begin(){retained.clear();check(vkResetCommandBuffer(commands,0),"vkResetCommandBuffer");VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};info.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(vkBeginCommandBuffer(commands,&info),"vkBeginCommandBuffer");}
    void submit(){check(vkEndCommandBuffer(commands),"vkEndCommandBuffer");check(vkResetFences(device,1,&fence),"vkResetFences");VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};info.commandBufferCount=1;info.pCommandBuffers=&commands;check(vkQueueSubmit(queue,1,&info,fence),"vkQueueSubmit");const auto result=vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull);if(result!=VK_SUCCESS)vkDeviceWaitIdle(device);check(result,"vkWaitForFences");}
};
struct ProbeBuffer {
    Device& owner;VkBuffer buffer{};VkDeviceMemory memory{};void* mapped{};VkDeviceSize offset{},bytes{},allocationBytes{};
    ProbeBuffer(Device& owner,VkDeviceSize bytes,unsigned factor=1):owner(owner),bytes(bytes){
        require(bytes && bytes<=owner.properties.limits.maxStorageBufferRange && bytes<=UINT32_MAX,"buffer payload limits");
        require(factor>=1&&factor<=3,"prototype offset factor");offset=std::max<VkDeviceSize>(256,owner.properties.limits.minStorageBufferOffsetAlignment)*factor;allocationBytes=offset+bytes+offset;
        try{VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=allocationBytes;info.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
            check(vkCreateBuffer(owner.device,&info,nullptr,&buffer),"vkCreateBuffer");VkMemoryRequirements requirements{};vkGetBufferMemoryRequirements(owner.device,buffer,&requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=owner.memoryType(requirements.memoryTypeBits);
            check(vkAllocateMemory(owner.device,&allocation,nullptr,&memory),"vkAllocateMemory");check(vkBindBufferMemory(owner.device,buffer,memory,0),"vkBindBufferMemory");check(vkMapMemory(owner.device,memory,0,VK_WHOLE_SIZE,0,&mapped),"vkMapMemory");std::memset(mapped,0xb6,allocationBytes);
        }catch(...){cleanup();throw;}
    }
    ~ProbeBuffer(){cleanup();}
    void cleanup(){if(mapped)vkUnmapMemory(owner.device,memory);if(buffer)vkDestroyBuffer(owner.device,buffer,nullptr);if(memory)vkFreeMemory(owner.device,memory,nullptr);mapped={};buffer={};memory={};}
    std::byte* data(){return static_cast<std::byte*>(mapped)+offset;}
    std::uint64_t guardErrors()const{std::uint64_t result=0;const auto* ptr=static_cast<const unsigned char*>(mapped);for(VkDeviceSize i=0;i<offset;++i)result+=ptr[i]!=0xb6;for(VkDeviceSize i=offset+bytes;i<allocationBytes;++i)result+=ptr[i]!=0xb6;return result;}
};
void barrier(Device& device,const ProbeBuffer& buffer,VkPipelineStageFlags beforeStage,VkAccessFlags beforeAccess,VkPipelineStageFlags afterStage,VkAccessFlags afterAccess){VkBufferMemoryBarrier value{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};value.srcAccessMask=beforeAccess;value.dstAccessMask=afterAccess;value.srcQueueFamilyIndex=value.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;value.buffer=buffer.buffer;value.offset=buffer.offset;value.size=buffer.bytes;vkCmdPipelineBarrier(device.commands,beforeStage,afterStage,0,0,nullptr,1,&value,0,nullptr);}
void dispatch(Device& device,const ProbeBuffer& source,const ProbeBuffer& destination,const Push& push,bool retile,
              VkPipelineStageFlags sourceStage=VK_PIPELINE_STAGE_HOST_BIT,VkAccessFlags sourceAccess=VK_ACCESS_HOST_WRITE_BIT,
              VkPipelineStageFlags destinationStage=VK_PIPELINE_STAGE_HOST_BIT,VkAccessFlags destinationAccess=VK_ACCESS_HOST_WRITE_BIT,
              VkPipelineStageFlags consumerStage=VK_PIPELINE_STAGE_HOST_BIT,VkAccessFlags consumerAccess=VK_ACCESS_HOST_READ_BIT) {
    DepthTarget depth{};depth.stencilAddress=0x10000;depth.extent={push.width,push.height};depth.format=VK_FORMAT_D32_SFLOAT_S8_UINT;depth.samples=8;
    require(GpuStencilSampleTiler::Supports(device.context(),depth),"production helper rejected AMD-qualified S8 layout");
    auto transfer=std::make_unique<GpuStencilSampleTiler::Transfer>(*device.tiler,depth);
    transfer->Record(device.commands,retile,{device.device,source.buffer,source.offset,source.bytes,sourceStage,sourceAccess},
        {device.device,destination.buffer,destination.offset,destination.bytes,destinationStage,destinationAccess},consumerStage,consumerAccess);
    device.retained.push_back(std::move(transfer));
}
std::uint32_t pattern(unsigned x,unsigned y,unsigned sample){return((x*29u+y*131u+sample*17u)&255u)|(((x*73u^y*149u^sample*37u)&255u)<<8)|(((x*53u+y*31u+sample*111u)&255u)<<16)|((255u-((x*31u+y*17u+sample*13u)&255u))<<24);}
std::uint64_t compareBytes(const std::byte* actual,const std::vector<std::byte>& expected){std::uint64_t errors=0;for(std::size_t i=0;i<expected.size();++i)errors+=actual[i]!=expected[i];return errors;}


// Preserved original synthetic depth/stencil image owner and independent Z reader.
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


struct Golden {
    unsigned width,height;
    ADDR2_COMPUTE_SURFACE_INFO_OUTPUT geometry{};
    Push push{};
    std::vector<std::byte> linear,tiled,changed,chain;
    std::uint64_t coordinates{};
    Golden(Oracle& oracle,unsigned width,unsigned height):width(width),height(height) {
        ADDR2_COMPUTE_SURFACE_INFO_INPUT info{};info.size=sizeof(info);info.flags.stencil=1;
        info.swizzleMode=ADDR_SW_64KB_Z_X;info.resourceType=ADDR_RSRC_TEX_2D;info.bpp=8;
        info.width=width;info.height=height;info.numSlices=info.numMipLevels=1;info.numSamples=info.numFrags=8;
        geometry.size=sizeof(geometry);require(Addr2ComputeSurfaceInfo(oracle.library,&info,&geometry)==ADDR_OK,"AMD S8 surface geometry");
        require(geometry.blockWidth==64&&geometry.blockHeight==128&&geometry.baseAlign==65536&&geometry.pitch==(width+63)/64*64&&geometry.height==(height+127)/128*128,"AMD S8 geometry differs");
        const auto bytes=std::uint64_t(width)*height*8;require(bytes<=UINT32_MAX&&geometry.surfSize<=UINT32_MAX&&geometry.surfSize%4==0,"32-bit indexing limits");
        push={width,height,geometry.pitch/64,static_cast<std::uint32_t>(geometry.surfSize/4),static_cast<std::uint32_t>(bytes/4)};
        linear.resize(bytes);tiled.assign(geometry.surfSize,std::byte{0xa5});changed.assign(geometry.surfSize,std::byte{0x5a});chain.assign(geometry.surfSize,std::byte{0xfe});
        std::vector<bool> visited(geometry.surfSize);
        ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT coord{};coord.size=sizeof(coord);coord.flags=info.flags;coord.swizzleMode=info.swizzleMode;coord.resourceType=info.resourceType;coord.bpp=8;
        coord.unalignedWidth=width;coord.unalignedHeight=height;coord.numSlices=coord.numMipLevels=1;coord.numSamples=coord.numFrags=8;
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            std::uint64_t first=0;
            for(unsigned sample=0;sample<8;++sample) {
                coord.x=x;coord.y=y;coord.sample=sample;ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT address{};address.size=sizeof(address);
                require(Addr2ComputeSurfaceAddrFromCoord(oracle.library,&coord,&address)==ADDR_OK,"AMD S8 coordinate");
                require(address.bitPosition==0&&address.addr<geometry.surfSize&&!visited[address.addr],"AMD S8 coordinate range/bijection");
                if(sample==0)first=address.addr;
                require(first%8==0&&address.addr==first+sample,"S8/8 word ownership is not contiguous/aligned");
                visited[address.addr]=true;++coordinates;
                const auto index=(std::uint64_t(y)*width+x)*8+sample;
                const unsigned value=(((x*31u)^(y*127u)^(sample*73u))+19u)&255u;
                linear[index]=std::byte(value);tiled[address.addr]=linear[index];chain[address.addr]=linear[index];
                changed[address.addr]=std::byte(value^((0x58fb2973u>>(8*(sample%4)))&255u));
            }
        }
    }
};
struct Totals {
    unsigned computeCases{},imageCases{};
    std::uint64_t coordinates{},logicalBytes{},paddingBytes{},depthSamples{},errors{};
    std::set<unsigned> values;
};
void computeCase(Device& device,const Golden& golden,Totals& totals,unsigned factor) {
    const auto bytes=golden.linear.size(),tiledBytes=golden.tiled.size();
    ProbeBuffer tiled(device,tiledBytes,factor),linear(device,bytes,factor%3+1),retiled(device,tiledBytes,(factor+1)%3+1);
    std::memcpy(tiled.data(),golden.tiled.data(),tiledBytes);std::memset(linear.data(),0xca,bytes);std::memset(retiled.data(),0x5a,tiledBytes);
    device.begin();
    barrier(device,tiled,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
    barrier(device,linear,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT);
    dispatch(device,tiled,linear,golden.push,false);
    barrier(device,linear,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);device.submit();
    auto errors=compareBytes(linear.data(),golden.linear)+compareBytes(tiled.data(),golden.tiled)+linear.guardErrors()+tiled.guardErrors();
    for(std::size_t i=0;i<bytes;i+=4) {std::uint32_t word;std::memcpy(&word,golden.linear.data()+i,4);word^=0x58fb2973u;std::memcpy(linear.data()+i,&word,4);}
    device.begin();
    barrier(device,linear,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
    barrier(device,retiled,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT);
    dispatch(device,linear,retiled,golden.push,true);
    barrier(device,retiled,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);device.submit();
    errors+=compareBytes(retiled.data(),golden.changed)+retiled.guardErrors()+linear.guardErrors();
    std::memset(linear.data(),0xca,bytes);device.begin();
    barrier(device,tiled,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
    barrier(device,linear,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT);
    dispatch(device,tiled,linear,golden.push,false);
    barrier(device,tiled,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    vkCmdFillBuffer(device.commands,tiled.buffer,tiled.offset,tiled.bytes,0xfefefefeu);
    barrier(device,tiled,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT);
    barrier(device,linear,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
    dispatch(device,linear,tiled,golden.push,true,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    barrier(device,tiled,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
    barrier(device,linear,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);device.submit();
    errors+=compareBytes(tiled.data(),golden.chain)+compareBytes(linear.data(),golden.linear)+tiled.guardErrors()+linear.guardErrors();
    totals.errors+=errors;totals.computeCases+=3;totals.logicalBytes+=bytes*3;totals.paddingBytes+=(tiledBytes-bytes)*2;
    std::printf("[s8-tiling] extent=%ux%u samples=8 tiled_bytes=%zu logical_bytes=%zu offsets=%llu,%llu,%llu compute_cases=3 errors=%llu\n",golden.width,golden.height,tiledBytes,bytes,
        static_cast<unsigned long long>(tiled.offset),static_cast<unsigned long long>(linear.offset),static_cast<unsigned long long>(retiled.offset),static_cast<unsigned long long>(errors));
}
void imageCase(Device& device,const Golden& golden,Totals& totals,bool alias) {
    const auto context=device.context();
    std::array<VkSampleLocationEXT,8> positions{{{2.f/16,2.f/16},{6.f/16,2.f/16},{10.f/16,2.f/16},{14.f/16,2.f/16},{2.f/16,10.f/16},{6.f/16,10.f/16},{10.f/16,10.f/16},{14.f/16,10.f/16}}};
    static unsigned ordinal=0;
    const auto index=ordinal++;
    const unsigned tuple=index==2?1:index==4?2:0;
    if(tuple==1)std::swap(positions[0],positions[1]);
    if(tuple==2)positions[0].x=3.f/16;
    const auto before=graphicsProgramCreations+pipelineCreations;
    DepthImage target(context,{golden.width,golden.height},VK_FORMAT_D32_SFLOAT_S8_UINT,8);
    StencilSampleTransfer transfer(context,target.image,target.format,std::span(target.attachments).first(target.groups),{golden.width,golden.height},8,positions);
    const unsigned createdPrograms=graphicsProgramCreations+pipelineCreations-before;
    require(createdPrograms==((index==0||index==2||index==4)?9u:0u),"native tuple hit/change program identity mismatch");
    // Drop every cache reference while this transfer still needs the bundle.
    // Its shared ownership must retain compatible renderpass/layouts/pipelines
    // through actual GPU upload/readback and the same fence below.
    if(index==5)device.stencilCache->Clear();
    std::printf("[stencil-cache-native] case=%u tuple=%u new_programs=%u clear_before_submit=%u\n",index,tuple,createdPrograms,index==5);
    require(transfer.PackedBytes()==golden.linear.size(),"S8 packed transfer bytes differ");
    ProbeBuffer tiled(device,golden.tiled.size(),3),linear(device,golden.linear.size(),2);
    std::unique_ptr<ProbeBuffer> separate;
    if(!alias)separate=std::make_unique<ProbeBuffer>(device,golden.linear.size(),1);
    ProbeBuffer& output=alias?linear:*separate;
    HostBuffer depthOutput(context,golden.linear.size()*sizeof(float));
    std::memset(depthOutput.mapped,0xde,golden.linear.size()*sizeof(float));
    DepthReadback depthReader(context,target.depthView,depthOutput.buffer);
    std::memcpy(tiled.data(),golden.tiled.data(),golden.tiled.size());std::memset(linear.data(),0xca,linear.bytes);
    if(!alias)std::memset(output.data(),0xf1,output.bytes);
    device.begin();
    barrier(device,tiled,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
    barrier(device,linear,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT);
    dispatch(device,tiled,linear,golden.push,false);
    VkImageMemoryBarrier initialize{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};initialize.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;initialize.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;initialize.newLayout=VK_IMAGE_LAYOUT_GENERAL;
    initialize.srcQueueFamilyIndex=initialize.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;initialize.image=target.image;initialize.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,0,1,0,target.groups};
    vkCmdPipelineBarrier(device.commands,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&initialize);
    const VkClearDepthStencilValue initial{0.625f,0x3cu};vkCmdClearDepthStencilImage(device.commands,target.image,VK_IMAGE_LAYOUT_GENERAL,&initial,1,&initialize.subresourceRange);
    const ColorSampleImageAccess attachment{VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    transfer.RecordUpload(device.commands,{linear.buffer,linear.offset,linear.bytes,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT},
        {VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT},attachment);
    // Poison both prior sources after their reads. Neither missing image
    // readback nor missing Retile can pass by preserving source contents.
    barrier(device,tiled,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    vkCmdFillBuffer(device.commands,tiled.buffer,tiled.offset,tiled.bytes,0xfefefefeu);
    barrier(device,tiled,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT);
    barrier(device,linear,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    vkCmdFillBuffer(device.commands,linear.buffer,linear.offset,linear.bytes,0xfefefefeu);
    const ColorSampleBufferAccess outputBefore{output.buffer,output.offset,output.bytes,alias?VK_PIPELINE_STAGE_TRANSFER_BIT:VK_PIPELINE_STAGE_HOST_BIT,alias?VK_ACCESS_TRANSFER_WRITE_BIT:VK_ACCESS_HOST_WRITE_BIT};
    transfer.RecordReadback(device.commands,outputBefore,attachment,{VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT},VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
    dispatch(device,output,tiled,golden.push,true,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    barrier(device,tiled,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
    barrier(device,output,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
    depthReader.Record(device.commands,golden.width,8,target.nativeSamples,static_cast<unsigned>(golden.linear.size()));
    device.submit();
    auto errors=compareBytes(tiled.data(),golden.chain)+compareBytes(output.data(),golden.linear)+tiled.guardErrors()+linear.guardErrors()+output.guardErrors();
    const auto* depth=static_cast<const float*>(depthOutput.mapped);std::uint64_t zErrors=0;
    for(std::size_t i=0;i<golden.linear.size();++i)zErrors+=!std::isfinite(depth[i])||std::fabs(depth[i]-0.625f)>0.000001f;
    errors+=zErrors;totals.errors+=errors;++totals.imageCases;totals.logicalBytes+=golden.linear.size();totals.paddingBytes+=golden.tiled.size()-golden.linear.size();totals.depthSamples+=golden.linear.size();
    std::printf("[s8-image-chain] extent=%ux%u samples=8 native=4 groups=2 upload_raster_draws=64 alias=%u stencil_bytes=%zu depth_samples=%zu depth_errors=%llu errors=%llu\n",golden.width,golden.height,alias,golden.linear.size(),golden.linear.size(),static_cast<unsigned long long>(zErrors),static_cast<unsigned long long>(errors));
}
int main(int argc,char** argv) {
    static_cast<void>(argc);static_cast<void>(argv);
    try {
        Oracle oracle;Device device;Totals totals;unsigned ordinal=0;
        for(const auto extent:{std::array{1u,1u},std::array{17u,19u},std::array{31u,63u},std::array{63u,127u},std::array{65u,129u},std::array{257u,129u},std::array{2049u,19u},std::array{4097u,19u},std::array{19u,4097u},std::array{8193u,19u},std::array{19u,8193u},std::array{16383u,1u},std::array{1u,16383u},std::array{1920u,1080u}}) {
            Golden golden(oracle,extent[0],extent[1]);totals.coordinates+=golden.coordinates;
            for(const auto value:golden.linear)totals.values.insert(static_cast<unsigned char>(value));
            computeCase(device,golden,totals,ordinal++%3+1);
            if(golden.width==17||golden.width==65||golden.width==257)for(bool alias:{false,true})imageCase(device,golden,totals,alias);
        }
        device.retained.clear();
        require(tilerProgramCreations==2&&pipelineCreations==5&&graphicsProgramCreations==24&&poolsCreated==poolsDestroyed,"production helper cache/pool lifetime mismatch");
        std::printf("[s8-ownership] tiler_programs=%u total_transfer_compute_programs=%u pools_created=%u pools_destroyed=%u\n",tilerProgramCreations,pipelineCreations,poolsCreated,poolsDestroyed);
        require(totals.values.size()==256,"fixture did not cover all S8 byte values");
        std::printf("[s8-summary] compute_cases=%u image_chain_cases=%u AMD_coordinates=%llu logical_bytes_checked=%llu padding_bytes_checked=%llu unchanged_depth_samples=%llu s8_values=%zu errors=%llu status=%s\n",totals.computeCases,totals.imageCases,
            static_cast<unsigned long long>(totals.coordinates),static_cast<unsigned long long>(totals.logicalBytes),static_cast<unsigned long long>(totals.paddingBytes),static_cast<unsigned long long>(totals.depthSamples),totals.values.size(),static_cast<unsigned long long>(totals.errors),totals.errors?"FAIL":"PASS");
        return totals.errors?1:0;
    }catch(const std::exception& error){std::fprintf(stderr,"[s8-tiling] FAIL %s\n",error.what());return 1;}
}
