// Original native proof of production StencilZeroTransfer against the legacy zero upload.
// Synthetic image/raster ownership only: not DepthSurface/Draw/GuestMemory/Wine/iPad/game proof.
#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include "prx/libSceAgcDriver/Graphics/include/StencilZeroInvariant.hpp"
#include "prx/libSceAgcDriver/Graphics/include/StencilSampleTransfer.hpp"
#include "StencilZeroProbe_spv.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace AgcDriver::Graphics;
namespace {
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
struct AllocationCounters {
    unsigned graphics{}, compute{}, descriptorPools{}, descriptorLayouts{}, pipelineLayouts{}, renderPasses{}, shaderModules{}, imageViews{}, framebuffers{}, descriptorSets{};
    unsigned Total() const { return graphics + compute + descriptorPools + descriptorLayouts + pipelineLayouts + renderPasses + shaderModules + imageViews + framebuffers + descriptorSets; }
} allocations;
template <typename Function> PFN_vkVoidFunction procedure(Function value) { return reinterpret_cast<PFN_vkVoidFunction>(value); }
VKAPI_ATTR VkResult VKAPI_CALL trackGraphics(VkDevice d, VkPipelineCache c, uint32_t n, const VkGraphicsPipelineCreateInfo* i, const VkAllocationCallbacks* a, VkPipeline* o) { auto r=vkCreateGraphicsPipelines(d,c,n,i,a,o); if(r==VK_SUCCESS) allocations.graphics+=n; return r; }
VKAPI_ATTR VkResult VKAPI_CALL trackCompute(VkDevice d, VkPipelineCache c, uint32_t n, const VkComputePipelineCreateInfo* i, const VkAllocationCallbacks* a, VkPipeline* o) { auto r=vkCreateComputePipelines(d,c,n,i,a,o); if(r==VK_SUCCESS) allocations.compute+=n; return r; }
#define TRACK_CREATE(Name, Type, Field) \
VKAPI_ATTR VkResult VKAPI_CALL track##Name(VkDevice d, const Vk##Name##CreateInfo* i, const VkAllocationCallbacks* a, Vk##Type* o) { auto r=vkCreate##Name(d,i,a,o); if(r==VK_SUCCESS) ++allocations.Field; return r; }
TRACK_CREATE(DescriptorPool, DescriptorPool, descriptorPools)
TRACK_CREATE(DescriptorSetLayout, DescriptorSetLayout, descriptorLayouts)
TRACK_CREATE(PipelineLayout, PipelineLayout, pipelineLayouts)
TRACK_CREATE(RenderPass, RenderPass, renderPasses)
TRACK_CREATE(ShaderModule, ShaderModule, shaderModules)
TRACK_CREATE(ImageView, ImageView, imageViews)
TRACK_CREATE(Framebuffer, Framebuffer, framebuffers)
#undef TRACK_CREATE
VKAPI_ATTR VkResult VKAPI_CALL trackDescriptorSets(VkDevice d, const VkDescriptorSetAllocateInfo* i, VkDescriptorSet* o) { auto r=vkAllocateDescriptorSets(d,i,o); if(r==VK_SUCCESS) allocations.descriptorSets+=i->descriptorSetCount; return r; }
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device, const char* name) {
    if(std::strcmp(name,"vkCreateGraphicsPipelines")==0) return procedure(trackGraphics);
    if(std::strcmp(name,"vkCreateComputePipelines")==0) return procedure(trackCompute);
    if(std::strcmp(name,"vkCreateDescriptorPool")==0) return procedure(trackDescriptorPool);
    if(std::strcmp(name,"vkCreateDescriptorSetLayout")==0) return procedure(trackDescriptorSetLayout);
    if(std::strcmp(name,"vkCreatePipelineLayout")==0) return procedure(trackPipelineLayout);
    if(std::strcmp(name,"vkCreateRenderPass")==0) return procedure(trackRenderPass);
    if(std::strcmp(name,"vkCreateShaderModule")==0) return procedure(trackShaderModule);
    if(std::strcmp(name,"vkCreateImageView")==0) return procedure(trackImageView);
    if(std::strcmp(name,"vkCreateFramebuffer")==0) return procedure(trackFramebuffer);
    if(std::strcmp(name,"vkAllocateDescriptorSets")==0) return procedure(trackDescriptorSets);
    return vkGetDeviceProcAddr(device,name);
}
struct Device {
    VkInstance instance{}; VkPhysicalDevice physical{}; VkDevice device{}; VkQueue queue{};
    VkCommandPool pool{}; VkCommandBuffer commands{}; VkFence fence{};
    VkPhysicalDeviceProperties properties{}; VkPhysicalDeviceMemoryProperties memory{};
    Device() {
        try {
            VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion=VK_API_VERSION_1_1; app.pApplicationName="Original stencil zero invariant proof";
            VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo=&app;
            Check(vkCreateInstance(&info,nullptr,&instance),"vkCreateInstance");
            uint32_t count=0; Check(vkEnumeratePhysicalDevices(instance,&count,nullptr),"vkEnumeratePhysicalDevices"); require(count!=0,"no native Vulkan device");
            std::vector<VkPhysicalDevice> devices(count); Check(vkEnumeratePhysicalDevices(instance,&count,devices.data()),"physical devices"); physical=devices.front();
            vkGetPhysicalDeviceProperties(physical,&properties); vkGetPhysicalDeviceMemoryProperties(physical,&memory);
            require(properties.deviceType!=VK_PHYSICAL_DEVICE_TYPE_CPU,"CPU Vulkan is not native GPU proof");
            vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr); std::vector<VkQueueFamilyProperties> families(count); vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
            uint32_t family=UINT32_MAX;
            for(uint32_t i=0;i<count;++i) if(families[i].queueCount && (families[i].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)){family=i;break;}
            require(family!=UINT32_MAX,"no graphics/compute queue");
            Check(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,nullptr),"device extensions"); std::vector<VkExtensionProperties> extensions(count); Check(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,extensions.data()),"extension list");
            bool portability=false,locations=false;
            for(const auto& e:extensions){portability|=std::strcmp(e.extensionName,"VK_KHR_portability_subset")==0;locations|=std::strcmp(e.extensionName,"VK_EXT_sample_locations")==0;}
            require(locations,"VK_EXT_sample_locations required");
            VkPhysicalDevicePortabilitySubsetFeaturesKHR portable{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR};
            if(portability){VkPhysicalDeviceFeatures2 f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};f.pNext=&portable;vkGetPhysicalDeviceFeatures2(physical,&f);require(portable.multisampleArrayImage,"multisampleArrayImage required");portable={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR};portable.multisampleArrayImage=VK_TRUE;}
            const float priority=1; VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
            VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;
            const std::array<const char*,2> enabled{"VK_EXT_sample_locations","VK_KHR_portability_subset"};di.enabledExtensionCount=portability?2:1;di.ppEnabledExtensionNames=enabled.data();if(portability)di.pNext=&portable;
            Check(vkCreateDevice(physical,&di,nullptr,&device),"vkCreateDevice");vkGetDeviceQueue(device,family,0,&queue);
            VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=family;pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;Check(vkCreateCommandPool(device,&pi,nullptr,&pool),"command pool");
            VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;Check(vkAllocateCommandBuffers(device,&ai,&commands),"command buffer");
            VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};Check(vkCreateFence(device,&fi,nullptr,&fence),"fence");
            std::printf("[zero-device] name=%s api=%u.%u.%u driver=%u\n",properties.deviceName,VK_VERSION_MAJOR(properties.apiVersion),VK_VERSION_MINOR(properties.apiVersion),VK_VERSION_PATCH(properties.apiVersion),properties.driverVersion);
        } catch(...) { release(); throw; }
    }
    ~Device(){release();}
    void release(){if(device){vkDeviceWaitIdle(device);if(fence)vkDestroyFence(device,fence,nullptr);if(pool)vkDestroyCommandPool(device,pool,nullptr);vkDestroyDevice(device,nullptr);device={};}if(instance){vkDestroyInstance(instance,nullptr);instance={};}}
    Context context() const {Context c{};c.device=device;c.physical=physical;c.queue=queue;c.pool=pool;c.deviceProc=resolve;c.formatProperties=vkGetPhysicalDeviceFormatProperties;c.imageFormatProperties=vkGetPhysicalDeviceImageFormatProperties;c.memory=memory;c.limits=properties.limits;c.sampleLocations=true;c.multisampleArrayImage=true;VkPhysicalDeviceProperties2 p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};p.pNext=&c.sampleLocationProperties;vkGetPhysicalDeviceProperties2(physical,&p);return c;}
    void begin(){Check(vkResetCommandBuffer(commands,0),"reset command buffer");VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};b.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;Check(vkBeginCommandBuffer(commands,&b),"begin command buffer");}
    void submit(){Check(vkEndCommandBuffer(commands),"end command buffer");Check(vkResetFences(device,1,&fence),"reset fence");VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};s.commandBufferCount=1;s.pCommandBuffers=&commands;Check(vkQueueSubmit(queue,1,&s,fence),"queue submit");const auto r=vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull);if(r!=VK_SUCCESS)vkDeviceWaitIdle(device);Check(r,"fence wait");}
};
struct HostBuffer {
    VkDevice device{};VkBuffer buffer{};VkDeviceMemory memory{};void* mapped{};VkDeviceSize bytes{};
    HostBuffer(const Context& c,VkDeviceSize bytes):device(c.device),bytes(bytes){try{VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};b.size=bytes;b.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;b.sharingMode=VK_SHARING_MODE_EXCLUSIVE;Check(vkCreateBuffer(device,&b,nullptr,&buffer),"readback buffer");VkMemoryRequirements r{};vkGetBufferMemoryRequirements(device,buffer,&r);VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=r.size;a.memoryTypeIndex=c.MemoryType(r.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);Check(vkAllocateMemory(device,&a,nullptr,&memory),"readback memory");Check(vkBindBufferMemory(device,buffer,memory,0),"bind readback");Check(vkMapMemory(device,memory,0,VK_WHOLE_SIZE,0,&mapped),"map readback");}catch(...){release();throw;}}
    ~HostBuffer(){release();}void release(){if(mapped)vkUnmapMemory(device,memory);if(buffer)vkDestroyBuffer(device,buffer,nullptr);if(memory)vkFreeMemory(device,memory,nullptr);}
};
struct Image {
    VkDevice device{};VkImage image{};VkDeviceMemory memory{};VkImageView sampled{};std::array<VkImageView,2> attachments{};VkFormat format;bool depth;
    Image(const Context& c,VkExtent2D extent,VkFormat format,bool depth):device(c.device),format(format),depth(depth){try{VkImageCreateInfo i{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};i.flags=depth?VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT:0;i.imageType=VK_IMAGE_TYPE_2D;i.format=format;i.extent={extent.width,extent.height,1};i.mipLevels=1;i.arrayLayers=2;i.samples=VK_SAMPLE_COUNT_4_BIT;i.tiling=VK_IMAGE_TILING_OPTIMAL;i.usage=(depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;i.sharingMode=VK_SHARING_MODE_EXCLUSIVE;Check(vkCreateImage(device,&i,nullptr,&image),"probe image");VkMemoryRequirements r{};vkGetImageMemoryRequirements(device,image,&r);VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};a.allocationSize=r.size;a.memoryTypeIndex=c.MemoryType(r.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);Check(vkAllocateMemory(device,&a,nullptr,&memory),"probe image memory");Check(vkBindImageMemory(device,image,memory,0),"bind probe image");VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};v.image=image;v.format=format;v.viewType=VK_IMAGE_VIEW_TYPE_2D;v.subresourceRange={static_cast<VkImageAspectFlags>(depth?VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,1,0,1};for(unsigned group=0;group<2;++group){v.subresourceRange.baseArrayLayer=group;Check(vkCreateImageView(device,&v,nullptr,&attachments[group]),"attachment view");}v.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;v.subresourceRange={depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,2};Check(vkCreateImageView(device,&v,nullptr,&sampled),"sampled view");}catch(...){release();throw;}}
    ~Image(){release();}void release(){if(sampled)vkDestroyImageView(device,sampled,nullptr);for(auto v:attachments)if(v)vkDestroyImageView(device,v,nullptr);if(image)vkDestroyImage(device,image,nullptr);if(memory)vkFreeMemory(device,memory,nullptr);}
};
VkShaderModule module(VkDevice device,std::span<const uint32_t> code){VkShaderModuleCreateInfo i{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};i.codeSize=code.size_bytes();i.pCode=code.data();VkShaderModule m{};Check(vkCreateShaderModule(device,&i,nullptr,&m),"probe shader module");return m;}
const std::array<VkSampleLocationEXT,8> positions{{{2.f/16,2.f/16},{6.f/16,2.f/16},{10.f/16,2.f/16},{14.f/16,2.f/16},{3.f/16,11.f/16},{7.f/16,11.f/16},{11.f/16,11.f/16},{15.f/16,11.f/16}}};
VkSampleLocationsInfoEXT pattern(unsigned group){return {VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT,nullptr,VK_SAMPLE_COUNT_4_BIT,{1,1},4,positions.data()+group*4};}
void imageBarrier(Device& d,const Image& image,VkPipelineStageFlags src,VkAccessFlags srcAccess,VkPipelineStageFlags dst,VkAccessFlags dstAccess,bool undefined=false){for(unsigned group=0;group<2;++group){const auto samples=pattern(group);VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};if(image.depth)b.pNext=&samples;b.srcAccessMask=srcAccess;b.dstAccessMask=dstAccess;b.oldLayout=undefined?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_GENERAL;b.newLayout=VK_IMAGE_LAYOUT_GENERAL;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image.image;b.subresourceRange={static_cast<VkImageAspectFlags>(image.depth?VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,1,group,1};vkCmdPipelineBarrier(d.commands,src,dst,0,0,nullptr,0,nullptr,1,&b);}}
void bufferBarrier(Device& d,const HostBuffer& buffer,VkPipelineStageFlags src,VkAccessFlags srcAccess,VkPipelineStageFlags dst,VkAccessFlags dstAccess){VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};b.srcAccessMask=srcAccess;b.dstAccessMask=dstAccess;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.buffer=buffer.buffer;b.size=buffer.bytes;vkCmdPipelineBarrier(d.commands,src,dst,0,0,nullptr,1,&b,0,nullptr);}
constexpr VkPipelineStageFlags DSStages=VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
constexpr VkAccessFlags DSAccess=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
struct Raster {
    Device& owner;VkExtent2D extent;VkRenderPass pass{};VkPipelineLayout layout{};std::array<VkFramebuffer,2> framebuffers{};std::array<std::array<VkPipeline,2>,3> pipelines{};
    Raster(Device& d,const Image& color,const Image& depth,VkExtent2D extent):owner(d),extent(extent){VkShaderModule vertex{},fragment{};try{
        std::array<VkAttachmentDescription,2> attachments{};attachments[0].format=color.format;attachments[0].samples=VK_SAMPLE_COUNT_4_BIT;attachments[0].loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;attachments[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachments[0].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachments[0].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;attachments[0].initialLayout=attachments[0].finalLayout=VK_IMAGE_LAYOUT_GENERAL;attachments[1]=attachments[0];attachments[1].format=depth.format;attachments[1].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_LOAD;attachments[1].stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE;
        const VkAttachmentReference cr{0,VK_IMAGE_LAYOUT_GENERAL},dr{1,VK_IMAGE_LAYOUT_GENERAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&cr;sub.pDepthStencilAttachment=&dr;VkRenderPassCreateInfo pi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};pi.attachmentCount=2;pi.pAttachments=attachments.data();pi.subpassCount=1;pi.pSubpasses=&sub;Check(vkCreateRenderPass(d.device,&pi,nullptr,&pass),"raster renderpass");
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,16};VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};li.pushConstantRangeCount=1;li.pPushConstantRanges=&push;Check(vkCreatePipelineLayout(d.device,&li,nullptr,&layout),"raster layout");
        for(unsigned group=0;group<2;++group){const std::array<VkImageView,2> views{color.attachments[group],depth.attachments[group]};VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fi.renderPass=pass;fi.attachmentCount=2;fi.pAttachments=views.data();fi.width=extent.width;fi.height=extent.height;fi.layers=1;Check(vkCreateFramebuffer(d.device,&fi,nullptr,&framebuffers[group]),"raster framebuffer");}
        vertex=module(d.device,STENCIL_ZERO_VERTEX_SPV);fragment=module(d.device,STENCIL_ZERO_FRAGMENT_SPV);std::array<VkPipelineShaderStageCreateInfo,2> stages{};stages[0]={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex;stages[0].pName="main";stages[1]=stages[0];stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragment;
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};rs.polygonMode=VK_POLYGON_MODE_FILL;rs.cullMode=VK_CULL_MODE_NONE;rs.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;rs.lineWidth=1;
        VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};cb.attachmentCount=1;cb.pAttachments=&blend;const std::array<VkDynamicState,2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamicStates.data();
        const std::array<VkStencilOp,3> ops{VK_STENCIL_OP_KEEP,VK_STENCIL_OP_ZERO,VK_STENCIL_OP_REPLACE};
        for(unsigned op=0;op<3;++op)for(unsigned group=0;group<2;++group){const auto samples=pattern(group);VkPipelineSampleLocationsStateCreateInfoEXT locations{VK_STRUCTURE_TYPE_PIPELINE_SAMPLE_LOCATIONS_STATE_CREATE_INFO_EXT};locations.sampleLocationsEnable=VK_TRUE;locations.sampleLocationsInfo=samples;VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.pNext=&locations;ms.rasterizationSamples=VK_SAMPLE_COUNT_4_BIT;const VkSampleMask mask=15;ms.pSampleMask=&mask;VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};ds.stencilTestEnable=VK_TRUE;ds.front={ops[op],ops[op],ops[op],VK_COMPARE_OP_ALWAYS,255,255,0};const auto back=ops[(op+1)%3];ds.back={back,back,back,VK_COMPARE_OP_ALWAYS,255,255,0};VkGraphicsPipelineCreateInfo p{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};p.stageCount=2;p.pStages=stages.data();p.pVertexInputState=&vi;p.pInputAssemblyState=&ia;p.pViewportState=&vp;p.pRasterizationState=&rs;p.pMultisampleState=&ms;p.pDepthStencilState=&ds;p.pColorBlendState=&cb;p.pDynamicState=&dynamic;p.layout=layout;p.renderPass=pass;Check(vkCreateGraphicsPipelines(d.device,VK_NULL_HANDLE,1,&p,nullptr,&pipelines[op][group]),"partial raster pipeline");}
        vkDestroyShaderModule(d.device,vertex,nullptr);vertex={};vkDestroyShaderModule(d.device,fragment,nullptr);fragment={};
    }catch(...){if(vertex)vkDestroyShaderModule(d.device,vertex,nullptr);if(fragment)vkDestroyShaderModule(d.device,fragment,nullptr);release();throw;}}
    ~Raster(){release();}void release(){for(auto& group:pipelines)for(auto p:group)if(p)vkDestroyPipeline(owner.device,p,nullptr);for(auto f:framebuffers)if(f)vkDestroyFramebuffer(owner.device,f,nullptr);if(layout)vkDestroyPipelineLayout(owner.device,layout,nullptr);if(pass)vkDestroyRenderPass(owner.device,pass,nullptr);}
    void Record(unsigned op,unsigned reverse,unsigned discard){for(unsigned group=0;group<2;++group){const auto samples=pattern(group);const VkAttachmentSampleLocationsEXT initial{1,samples};const VkSubpassSampleLocationsEXT final{0,samples};VkRenderPassSampleLocationsBeginInfoEXT locations{VK_STRUCTURE_TYPE_RENDER_PASS_SAMPLE_LOCATIONS_BEGIN_INFO_EXT};locations.attachmentInitialSampleLocationsCount=1;locations.pAttachmentInitialSampleLocations=&initial;locations.postSubpassSampleLocationsCount=1;locations.pPostSubpassSampleLocations=&final;VkRenderPassBeginInfo b{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};b.pNext=&locations;b.renderPass=pass;b.framebuffer=framebuffers[group];b.renderArea={{0,0},extent};vkCmdBeginRenderPass(owner.commands,&b,VK_SUBPASS_CONTENTS_INLINE);const VkViewport vp{0,0,float(extent.width),float(extent.height),0,1};const VkRect2D scissor{{int32_t(extent.width/4),int32_t(extent.height/5)},{extent.width/2,extent.height*3/5}};vkCmdSetViewport(owner.commands,0,1,&vp);vkCmdSetScissor(owner.commands,0,1,&scissor);vkCmdBindPipeline(owner.commands,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelines[op][group]);const std::array<uint32_t,4> push{group,reverse,discard,0};vkCmdPushConstants(owner.commands,layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(push),push.data());vkCmdDraw(owner.commands,6,1,0,0);vkCmdEndRenderPass(owner.commands);}}
};
struct Reader {
    Device& owner;VkDescriptorSetLayout descriptors{};VkPipelineLayout layout{};VkPipeline pipeline{};VkDescriptorPool pool{};VkDescriptorSet set{};HostBuffer& output;
    Reader(Device& d,const Image& color,const Image& depth,HostBuffer& output):owner(d),output(output){VkShaderModule shader{};try{const std::array<VkDescriptorSetLayoutBinding,3> bindings{{{0,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}}};VkDescriptorSetLayoutCreateInfo di{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};di.bindingCount=3;di.pBindings=bindings.data();Check(vkCreateDescriptorSetLayout(d.device,&di,nullptr,&descriptors),"independent reader descriptors");const VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,16};VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};li.setLayoutCount=1;li.pSetLayouts=&descriptors;li.pushConstantRangeCount=1;li.pPushConstantRanges=&push;Check(vkCreatePipelineLayout(d.device,&li,nullptr,&layout),"independent reader layout");shader=module(d.device,STENCIL_ZERO_READBACK_SPV);VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};ci.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;ci.stage.module=shader;ci.stage.pName="main";ci.layout=layout;Check(vkCreateComputePipelines(d.device,VK_NULL_HANDLE,1,&ci,nullptr,&pipeline),"independent reader pipeline");vkDestroyShaderModule(d.device,shader,nullptr);shader={};const std::array<VkDescriptorPoolSize,2> sizes{{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,2},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}}};VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pi.maxSets=1;pi.poolSizeCount=2;pi.pPoolSizes=sizes.data();Check(vkCreateDescriptorPool(d.device,&pi,nullptr,&pool),"independent reader pool");VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=pool;ai.descriptorSetCount=1;ai.pSetLayouts=&descriptors;Check(vkAllocateDescriptorSets(d.device,&ai,&set),"independent reader set");const std::array<VkDescriptorImageInfo,2> images{{{VK_NULL_HANDLE,color.sampled,VK_IMAGE_LAYOUT_GENERAL},{VK_NULL_HANDLE,depth.sampled,VK_IMAGE_LAYOUT_GENERAL}}};const VkDescriptorBufferInfo buffer{output.buffer,0,output.bytes};std::array<VkWriteDescriptorSet,3> writes{};for(unsigned n=0;n<3;++n){writes[n]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[n].dstSet=set;writes[n].dstBinding=n;writes[n].descriptorCount=1;writes[n].descriptorType=n<2?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;if(n<2)writes[n].pImageInfo=&images[n];else writes[n].pBufferInfo=&buffer;}vkUpdateDescriptorSets(d.device,3,writes.data(),0,nullptr);}catch(...){if(shader)vkDestroyShaderModule(d.device,shader,nullptr);release();throw;}}
    ~Reader(){release();}void release(){if(pool)vkDestroyDescriptorPool(owner.device,pool,nullptr);if(pipeline)vkDestroyPipeline(owner.device,pipeline,nullptr);if(layout)vkDestroyPipelineLayout(owner.device,layout,nullptr);if(descriptors)vkDestroyDescriptorSetLayout(owner.device,descriptors,nullptr);}
    void Record(VkExtent2D extent){bufferBarrier(owner,output,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT);vkCmdBindPipeline(owner.commands,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(owner.commands,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr);const std::array<uint32_t,4> p{extent.width,extent.height,8,4};vkCmdPushConstants(owner.commands,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(p),p.data());vkCmdDispatch(owner.commands,(extent.width*extent.height*8+63)/64,1,1);bufferBarrier(owner,output,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);}
};
struct Result {std::vector<uint32_t> colorDepth;std::vector<std::byte> stencil;};
struct Totals {unsigned pairs{},negativeCases{},formats{},skippedFormats{},repeatPoison{},helperAllocations{};uint64_t colorSamples{},depthSamples{},stencilSamples{},negativeNonzero{},errors{},frontSamples{},backSamples{},discardRemoved{};};
bool formatSupported(Device& d,VkFormat format){VkFormatProperties p{};vkGetPhysicalDeviceFormatProperties(d.physical,format,&p);constexpr auto required=VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;if((p.optimalTilingFeatures&required)!=required)return false;VkImageFormatProperties i{};const auto r=vkGetPhysicalDeviceImageFormatProperties(d.physical,format,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT,&i);return r==VK_SUCCESS&&(i.sampleCounts&VK_SAMPLE_COUNT_4_BIT)&&i.maxArrayLayers>=2;}
void proof(Device& d,VkFormat format,VkExtent2D extent,Totals& totals){const auto context=d.context();Image color(context,extent,VK_FORMAT_R8G8B8A8_UNORM,false),depth(context,extent,format,true);const auto samples=extent.width*extent.height*8;HostBuffer zeros(context,samples),stencilOut(context,samples),colorDepth(context,samples*8);std::memset(zeros.mapped,0,samples);StencilSampleTransfer legacy(context,depth.image,format,depth.attachments,extent,8,positions);const auto beforeConstructor=allocations.Total();StencilZeroTransfer fast(context,depth.image,format,depth.attachments,extent,8,positions);totals.helperAllocations+=allocations.Total()-beforeConstructor;require(allocations.Total()==beforeConstructor,"zero helper allocated immutable/per-call Vulkan objects");Raster raster(d,color,depth,extent);Reader reader(d,color,depth,colorDepth);bool initialized=false;uint64_t iterations=0;
    const auto execute=[&](unsigned mode,unsigned op,unsigned reverse,unsigned discard){std::memset(stencilOut.mapped,0xcd,samples);std::memset(colorDepth.mapped,0xef,samples*8);d.begin();imageBarrier(d,depth,initialized?VK_PIPELINE_STAGE_ALL_COMMANDS_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,initialized?VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT:0,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,!initialized);imageBarrier(d,color,initialized?VK_PIPELINE_STAGE_ALL_COMMANDS_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,initialized?VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT:0,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,!initialized);initialized=true;
        // Re-poison this SAME image for every legacy/optimized/negative comparison.
        // Distinct nonzero depth values per layer also detect accidental layer swaps.
        for(unsigned group=0;group<2;++group){const VkClearDepthStencilValue seed{group?0.375f:0.625f,group?0xb7u:0x3cu};const VkImageSubresourceRange range{VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,0,1,group,1};vkCmdClearDepthStencilImage(d.commands,depth.image,VK_IMAGE_LAYOUT_GENERAL,&seed,1,&range);}
        const VkClearColorValue seedColor{{17.f/255,33.f/255,65.f/255,129.f/255}};const VkImageSubresourceRange colorRange{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,2};vkCmdClearColorImage(d.commands,color.image,VK_IMAGE_LAYOUT_GENERAL,&seedColor,1,&colorRange);imageBarrier(d,color,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        const ColorSampleImageAccess attachment{VK_IMAGE_LAYOUT_GENERAL,DSStages,DSAccess};
        if(mode==0)legacy.RecordUpload(d.commands,{zeros.buffer,0,zeros.bytes,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT},{VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT},attachment);
        else {imageBarrier(d,depth,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,DSStages,DSAccess);if(mode==1){const auto count=allocations.Total();fast.RecordClear(d.commands);totals.helperAllocations+=allocations.Total()-count;require(allocations.Total()==count,"zero clear allocated Vulkan objects");}}
        raster.Record(op,reverse,discard);
        if(mode==1){const auto count=allocations.Total();fast.RecordFinish(d.commands);totals.helperAllocations+=allocations.Total()-count;require(allocations.Total()==count,"zero finish allocated Vulkan objects");}
        imageBarrier(d,color,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
        legacy.RecordReadback(d.commands,{stencilOut.buffer,0,stencilOut.bytes,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT},attachment,{VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT},VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);reader.Record(extent);d.submit();++iterations;++totals.repeatPoison;
        Result result;const auto* values=static_cast<const uint32_t*>(colorDepth.mapped);result.colorDepth.assign(values,values+samples*2);const auto* bytes=static_cast<const std::byte*>(stencilOut.mapped);result.stencil.assign(bytes,bytes+samples);return result;};
    for(unsigned repeat=0;repeat<2;++repeat)for(unsigned op=0;op<3;++op)for(unsigned reverse=0;reverse<2;++reverse){
        uint64_t withoutDiscard=0;
        for(unsigned discard=0;discard<2;++discard){
            const auto baseline=execute(0,op,reverse,discard);
            const auto candidate=execute(1,op,reverse,discard);
            const auto negative=execute(2,op,reverse,discard);
            uint64_t changed=0,untouched=0,nonzeroOutside=0;
            for(unsigned i=0;i<samples;++i){
                const auto colorValue=baseline.colorDepth[i*2];
                const bool outside=colorValue==0x81412111u;
                changed+=!outside;untouched+=outside;
                if(outside){
                    nonzeroOutside+=negative.stencil[i]!=std::byte{0};
                    totals.errors+=negative.stencil[i]!=std::byte((i%8)<4?0x3c:0xb7);
                } else {
                    const unsigned red=colorValue&255u;
                    const unsigned pixel=i/8;
                    const unsigned green=((pixel%extent.width)*13u+(pixel/extent.width)*7u)&255u;
                    const unsigned blue=(i%8)<4?29u:132u;
                    totals.errors+=red!=73u&&red!=173u;
                    totals.errors+=((colorValue>>8u)&255u)!=green;
                    totals.errors+=((colorValue>>16u)&255u)!=blue;
                    totals.errors+=(colorValue>>24u)!=255u;
                    totals.frontSamples+=red==73u;totals.backSamples+=red==173u;
                }
                totals.errors+=baseline.stencil[i]!=std::byte{0};
                totals.errors+=candidate.stencil[i]!=std::byte{0};
                totals.errors+=candidate.colorDepth[i*2]!=colorValue;
                totals.errors+=negative.colorDepth[i*2]!=colorValue;
                const float expected=(i%8)<4?0.625f:0.375f;
                const float z=std::bit_cast<float>(baseline.colorDepth[i*2+1]);
                totals.errors+=!std::isfinite(z)||std::fabs(z-expected)>0.00003f;
                totals.errors+=candidate.colorDepth[i*2+1]!=baseline.colorDepth[i*2+1];
                totals.errors+=negative.colorDepth[i*2+1]!=baseline.colorDepth[i*2+1];
            }
            require(changed&&untouched,"partial raster did not prove both covered and untouched color samples");
            require(nonzeroOutside==untouched,"missing-clear negative did not retain nonzero outside partial raster");
            if(discard){require(changed<withoutDiscard,"fragment discard failed to remove covered samples");totals.discardRemoved+=withoutDiscard-changed;}
            else withoutDiscard=changed;
            ++totals.pairs;++totals.negativeCases;totals.negativeNonzero+=nonzeroOutside;
            totals.colorSamples+=samples*2;totals.depthSamples+=samples*3;totals.stencilSamples+=samples*2;
        }
    }
    std::printf("[zero-image] format=%u extent=%ux%u samples=8 native=4 layers=2 reused_image_poison_iterations=%llu pairs=24 negative_cases=24\n",unsigned(format),extent.width,extent.height,static_cast<unsigned long long>(iterations));
}
}
int main(){try{Device device;Totals totals;for(const auto format:{VK_FORMAT_D32_SFLOAT_S8_UINT,VK_FORMAT_D16_UNORM_S8_UINT}){const bool supported=formatSupported(device,format);if(format==VK_FORMAT_D16_UNORM_S8_UINT){++totals.skippedFormats;std::printf("[zero-format] format=%u native_support=%u production_excluded=1 unqualified\n",unsigned(format),supported);continue;}require(supported,"mandatory D32S8 format unavailable");++totals.formats;for(const auto extent:{VkExtent2D{17,19},VkExtent2D{73,41}})proof(device,format,extent,totals);}require(totals.helperAllocations==0,"zero helper allocation count changed");require(totals.frontSamples&&totals.backSamples&&totals.discardRemoved,"front/back/discard GPU coverage incomplete");std::printf("[zero-details] front_samples=%llu back_samples=%llu discard_removed_samples=%llu\n",static_cast<unsigned long long>(totals.frontSamples),static_cast<unsigned long long>(totals.backSamples),static_cast<unsigned long long>(totals.discardRemoved));std::printf("[zero-summary] pairs=%u negative_cases=%u formats=%u skipped_formats=%u repeat_poison=%u color_samples=%llu depth_samples=%llu stencil_samples=%llu negative_nonzero=%llu helper_allocations=%u errors=%llu status=%s\n",totals.pairs,totals.negativeCases,totals.formats,totals.skippedFormats,totals.repeatPoison,static_cast<unsigned long long>(totals.colorSamples),static_cast<unsigned long long>(totals.depthSamples),static_cast<unsigned long long>(totals.stencilSamples),static_cast<unsigned long long>(totals.negativeNonzero),totals.helperAllocations,static_cast<unsigned long long>(totals.errors),totals.errors?"FAIL":"PASS");return totals.errors?1:0;}catch(const std::exception& e){std::fprintf(stderr,"[zero-fatal] %s\n",e.what());return 1;}}
