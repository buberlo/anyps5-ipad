// Original offscreen descriptor/layout proof. Uses native Vulkan through the
// Windows Wine->MoltenVK bridge; does not exercise AnyPS5 Draw/Recorder itself.
#include <vulkan/vulkan.h>
#include "descriptor_copy_shaders.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr unsigned Width = 17, Height = 19;
constexpr VkDeviceSize Guard = 128, ImageBytes = Width * Height * 4;
constexpr std::array<std::uint32_t, 4> OriginalAdd{7,13,19,23}, ReplacementAdd{71,53,37,29};
void Check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + " VkResult=" + std::to_string(result));
}
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Runtime {
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    VkCommandPool pool{};
    VkCommandBuffer commands{};
    VkFence fence{};
    VkPhysicalDeviceMemoryProperties memory{};
    Runtime() {
        try {
            VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
            app.pApplicationName="AnyPS5 original descriptor copy probe";
            app.apiVersion=VK_API_VERSION_1_1;
            VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo=&app;
            Check(vkCreateInstance(&info,nullptr,&instance),"vkCreateInstance");
            std::uint32_t count=0;
            Check(vkEnumeratePhysicalDevices(instance,&count,nullptr),"vkEnumeratePhysicalDevices count");
            Require(count!=0,"no Vulkan devices");
            std::vector<VkPhysicalDevice> devices(count);
            Check(vkEnumeratePhysicalDevices(instance,&count,devices.data()),"vkEnumeratePhysicalDevices");
            physical=devices.front();
            VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(physical,&properties);
            std::printf("[descriptor-probe] GPU=%s api=%u driver=%u dimensions=%ux%u\n",properties.deviceName,properties.apiVersion,properties.driverVersion,Width,Height);
            vkGetPhysicalDeviceMemoryProperties(physical,&memory);
            vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
            unsigned family=0; while(family<count && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)) ++family;
            Require(family<count,"no graphics queue");
            const float priority=1;
            VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
            VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;
            Check(vkCreateDevice(physical,&di,nullptr,&device),"vkCreateDevice");
            vkGetDeviceQueue(device,family,0,&queue);
            VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pi.queueFamilyIndex=family;pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            Check(vkCreateCommandPool(device,&pi,nullptr,&pool),"vkCreateCommandPool");
            VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
            Check(vkAllocateCommandBuffers(device,&ai,&commands),"vkAllocateCommandBuffers");
            VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            Check(vkCreateFence(device,&fi,nullptr,&fence),"vkCreateFence");
        } catch (...) { Release();throw; }
    }
    Runtime(const Runtime&)=delete;
    ~Runtime(){ Release(); }
    void Release(){
        if(device) vkDeviceWaitIdle(device);
        if(fence) vkDestroyFence(device,fence,nullptr);
        if(pool) vkDestroyCommandPool(device,pool,nullptr);
        if(device) vkDestroyDevice(device,nullptr);
        if(instance) vkDestroyInstance(instance,nullptr);
    }
    unsigned MemoryType(unsigned bits,VkMemoryPropertyFlags flags)const{
        for(unsigned i=0;i<memory.memoryTypeCount;++i)
            if((bits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&flags)==flags)return i;
        throw std::runtime_error("required Vulkan memory type absent");
    }
    void Begin(){
        Check(vkResetCommandBuffer(commands,0),"vkResetCommandBuffer");
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Check(vkBeginCommandBuffer(commands,&bi),"vkBeginCommandBuffer");
    }
    void SubmitWait(){
        Check(vkEndCommandBuffer(commands),"vkEndCommandBuffer");
        Check(vkResetFences(device,1,&fence),"vkResetFences");
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&commands;
        Check(vkQueueSubmit(queue,1,&si,fence),"vkQueueSubmit");
        const auto result=vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ULL);
        // A timeout/error must not destroy buffers/images still referenced by a submission.
        if(result!=VK_SUCCESS) vkDeviceWaitIdle(device);
        Check(result,"vkWaitForFences");
    }
};
struct Buffer {
    VkDevice device;VkBuffer handle{};VkDeviceMemory memory{};void* mapped{};
    Buffer(const Runtime& rt,VkDeviceSize bytes):device(rt.device){
        try{
            VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=bytes;
            ci.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
            Check(vkCreateBuffer(device,&ci,nullptr,&handle),"vkCreateBuffer");
            VkMemoryRequirements mr{};vkGetBufferMemoryRequirements(device,handle,&mr);
            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=mr.size;
            ai.memoryTypeIndex=rt.MemoryType(mr.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            Check(vkAllocateMemory(device,&ai,nullptr,&memory),"vkAllocateMemory buffer");
            Check(vkBindBufferMemory(device,handle,memory,0),"vkBindBufferMemory");
            Check(vkMapMemory(device,memory,0,VK_WHOLE_SIZE,0,&mapped),"vkMapMemory");
        }catch(...){Release();throw;}
    }
    Buffer(const Buffer&)=delete;
    ~Buffer(){Release();}
    void Release(){if(mapped)vkUnmapMemory(device,memory);if(handle)vkDestroyBuffer(device,handle,nullptr);if(memory)vkFreeMemory(device,memory,nullptr);}
};
struct Image {
    VkDevice device;VkImage handle{};VkDeviceMemory memory{};VkImageView view{};
    Image(const Runtime& rt,VkImageUsageFlags usage):device(rt.device){
        try{
            VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.flags=VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
            ci.imageType=VK_IMAGE_TYPE_2D;ci.format=VK_FORMAT_R8G8B8A8_UNORM;ci.extent={Width,Height,1};
            ci.mipLevels=1;ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;
            ci.usage=usage;ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;ci.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
            Check(vkCreateImage(device,&ci,nullptr,&handle),"vkCreateImage");
            VkMemoryRequirements mr{};vkGetImageMemoryRequirements(device,handle,&mr);
            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=mr.size;
            ai.memoryTypeIndex=rt.MemoryType(mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            Check(vkAllocateMemory(device,&ai,nullptr,&memory),"vkAllocateMemory image");
            Check(vkBindImageMemory(device,handle,memory,0),"vkBindImageMemory");
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=handle;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;
            vi.format=ci.format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            Check(vkCreateImageView(device,&vi,nullptr,&view),"vkCreateImageView");
        }catch(...){Release();throw;}
    }
    Image(const Image&)=delete;
    ~Image(){Release();}
    void Release(){if(view)vkDestroyImageView(device,view,nullptr);if(handle)vkDestroyImage(device,handle,nullptr);if(memory)vkFreeMemory(device,memory,nullptr);}
};
void ImageBarrier(VkCommandBuffer commands,VkImage image,VkImageLayout old,VkImageLayout next,
                  VkPipelineStageFlags src,VkPipelineStageFlags dst,VkAccessFlags from,VkAccessFlags to){
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.oldLayout=old;b.newLayout=next;
    b.srcAccessMask=from;b.dstAccessMask=to;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.image=image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdPipelineBarrier(commands,src,dst,0,0,nullptr,0,nullptr,1,&b);
}
void BufferBarrier(VkCommandBuffer commands,VkBuffer buffer,VkPipelineStageFlags src,
                   VkPipelineStageFlags dst,VkAccessFlags from,VkAccessFlags to){
    VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};b.srcAccessMask=from;b.dstAccessMask=to;
    b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.buffer=buffer;b.size=VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(commands,src,dst,0,0,nullptr,1,&b,0,nullptr);
}
void Upload(VkCommandBuffer commands,const Image& image,const Buffer& input,VkImageLayout next,VkPipelineStageFlags stages,VkAccessFlags access){
    BufferBarrier(commands,input.handle,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    ImageBarrier(commands,image.handle,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,VK_ACCESS_TRANSFER_WRITE_BIT);
    VkBufferImageCopy r{};r.bufferOffset=Guard;r.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};r.imageExtent={Width,Height,1};
    vkCmdCopyBufferToImage(commands,input.handle,image.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&r);
    ImageBarrier(commands,image.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,next,VK_PIPELINE_STAGE_TRANSFER_BIT,stages,VK_ACCESS_TRANSFER_WRITE_BIT,access);
}
struct Descriptors {
    VkDevice device;VkDescriptorSetLayout layout{};VkDescriptorPool pool{};VkSampler sampler{};
    std::array<VkDescriptorSet,4> sets{};
    Descriptors(const Runtime& rt,const Image& source,const Buffer& original,const Buffer& replacement):device(rt.device){
        try{
            const std::array<VkDescriptorSetLayoutBinding,3> bindings{{
                {0,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
                {6,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
                {9,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}}};
            VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};li.bindingCount=bindings.size();li.pBindings=bindings.data();
            Check(vkCreateDescriptorSetLayout(device,&li,nullptr,&layout),"vkCreateDescriptorSetLayout");
            const std::array<VkDescriptorPoolSize,3> sizes{{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,4},{VK_DESCRIPTOR_TYPE_SAMPLER,4},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4}}};
            VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pi.maxSets=sets.size();pi.poolSizeCount=sizes.size();pi.pPoolSizes=sizes.data();
            Check(vkCreateDescriptorPool(device,&pi,nullptr,&pool),"vkCreateDescriptorPool");
            const std::array<VkDescriptorSetLayout,4> layouts{layout,layout,layout,layout};
            VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=pool;ai.descriptorSetCount=sets.size();ai.pSetLayouts=layouts.data();
            Check(vkAllocateDescriptorSets(device,&ai,sets.data()),"vkAllocateDescriptorSets");
            VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};si.magFilter=si.minFilter=VK_FILTER_NEAREST;
            si.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            si.maxLod=0;si.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
            Check(vkCreateSampler(device,&si,nullptr,&sampler),"vkCreateSampler");
            const VkDescriptorImageInfo image{VK_NULL_HANDLE,source.view,VK_IMAGE_LAYOUT_GENERAL};
            const VkDescriptorImageInfo sample{sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};
            const VkDescriptorBufferInfo first{original.handle,0,16},second{replacement.handle,0,16};
            const auto Writes=[&](VkDescriptorSet set,const VkDescriptorBufferInfo& buffer){
                std::array<VkWriteDescriptorSet,3> writes{};
                for(unsigned i=0;i<writes.size();++i){auto& w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=bindings[i].binding;w.descriptorType=bindings[i].descriptorType;w.descriptorCount=1;}
                writes[0].pImageInfo=&image;writes[1].pImageInfo=&sample;writes[2].pBufferInfo=&buffer;
                return writes;
            };
            const auto writes=Writes(sets[0],first);vkUpdateDescriptorSets(device,writes.size(),writes.data(),0,nullptr);
            for(unsigned mode=1;mode<=2;++mode){
                std::array<VkCopyDescriptorSet,3> copies{};
                for(unsigned i=0;i<copies.size();++i){auto& c=copies[i];c.sType=VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;c.srcSet=sets[0];c.dstSet=sets[mode];c.srcBinding=c.dstBinding=bindings[i].binding;c.descriptorCount=1;}
                vkUpdateDescriptorSets(device,0,nullptr,copies.size(),copies.data());
            }
            auto overrideWrite=Writes(sets[2],second)[2];vkUpdateDescriptorSets(device,1,&overrideWrite,0,nullptr);
            const auto rewrite=Writes(sets[3],second);vkUpdateDescriptorSets(device,rewrite.size(),rewrite.data(),0,nullptr);
        }catch(...){Release();throw;}
    }
    Descriptors(const Descriptors&)=delete;
    ~Descriptors(){Release();}
    void Release(){if(pool)vkDestroyDescriptorPool(device,pool,nullptr);if(sampler)vkDestroySampler(device,sampler,nullptr);if(layout)vkDestroyDescriptorSetLayout(device,layout,nullptr);}
};
struct Pipeline {
    VkDevice device;VkRenderPass pass{};VkFramebuffer framebuffer{};VkPipelineLayout layout{};VkPipeline pipeline{};
    std::array<VkShaderModule,2> modules{};
    Pipeline(const Runtime& rt,const Image& destination,VkDescriptorSetLayout descriptors,VkImageLayout attachmentLayout):device(rt.device){
        try{
            VkAttachmentDescription attachment{};attachment.format=VK_FORMAT_R8G8B8A8_UNORM;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout=attachment.finalLayout=attachmentLayout;
            const VkAttachmentReference reference{0,attachmentLayout};
            VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&reference;
            VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};ri.attachmentCount=1;ri.pAttachments=&attachment;ri.subpassCount=1;ri.pSubpasses=&subpass;
            Check(vkCreateRenderPass(device,&ri,nullptr,&pass),"vkCreateRenderPass");
            VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fi.renderPass=pass;fi.attachmentCount=1;fi.pAttachments=&destination.view;fi.width=Width;fi.height=Height;fi.layers=1;
            Check(vkCreateFramebuffer(device,&fi,nullptr,&framebuffer),"vkCreateFramebuffer");
            VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};li.setLayoutCount=1;li.pSetLayouts=&descriptors;
            Check(vkCreatePipelineLayout(device,&li,nullptr,&layout),"vkCreatePipelineLayout");
            const std::array<const std::uint32_t*,2> code{DESCRIPTOR_COPY_VERTEX_SPV,DESCRIPTOR_COPY_FRAGMENT_SPV};
            const std::array<std::size_t,2> sizes{sizeof(DESCRIPTOR_COPY_VERTEX_SPV),sizeof(DESCRIPTOR_COPY_FRAGMENT_SPV)};
            std::array<VkPipelineShaderStageCreateInfo,2> stages{};
            for(unsigned i=0;i<2;++i){VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};mi.codeSize=sizes[i];mi.pCode=code[i];Check(vkCreateShaderModule(device,&mi,nullptr,&modules[i]),"vkCreateShaderModule");stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[i].stage=i==0?VK_SHADER_STAGE_VERTEX_BIT:VK_SHADER_STAGE_FRAGMENT_BIT;stages[i].module=modules[i];stages[i].pName="main";}
            VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            const VkViewport viewport{0,0,float(Width),float(Height),0,1};const VkRect2D scissor{{0,0},{Width,Height}};
            VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
            VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};rs.polygonMode=VK_POLYGON_MODE_FILL;rs.cullMode=VK_CULL_MODE_NONE;rs.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;rs.lineWidth=1;
            VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
            const VkPipelineColorBlendAttachmentState color{VK_FALSE,VK_BLEND_FACTOR_ONE,VK_BLEND_FACTOR_ZERO,VK_BLEND_OP_ADD,VK_BLEND_FACTOR_ONE,VK_BLEND_FACTOR_ZERO,VK_BLEND_OP_ADD,15};
            VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};bs.attachmentCount=1;bs.pAttachments=&color;
            VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pi.stageCount=stages.size();pi.pStages=stages.data();pi.pVertexInputState=&vi;pi.pInputAssemblyState=&ia;pi.pViewportState=&vp;pi.pRasterizationState=&rs;pi.pMultisampleState=&ms;pi.pColorBlendState=&bs;pi.layout=layout;pi.renderPass=pass;
            Check(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline),"vkCreateGraphicsPipelines");
        }catch(...){Release();throw;}
    }
    Pipeline(const Pipeline&)=delete;
    ~Pipeline(){Release();}
    void Release(){if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);for(auto module:modules)if(module)vkDestroyShaderModule(device,module,nullptr);if(layout)vkDestroyPipelineLayout(device,layout,nullptr);if(framebuffer)vkDestroyFramebuffer(device,framebuffer,nullptr);if(pass)vkDestroyRenderPass(device,pass,nullptr);}
};
std::array<unsigned char,4> Pattern(unsigned x,unsigned y){
    return {static_cast<unsigned char>((x*29+y*131)&255),static_cast<unsigned char>((x*73^y*149)&255),static_cast<unsigned char>((x+y)*111&255),static_cast<unsigned char>(255-((x*31+y*17)&255))};
}
}
int main(){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    try{
        Runtime rt;
        Buffer seed(rt,Guard+ImageBytes+Guard),original(rt,256),replacement(rt,256);
        std::memset(seed.mapped,0xa6,Guard+ImageBytes+Guard);
        for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x){const auto pixel=Pattern(x,y);std::memcpy(static_cast<unsigned char*>(seed.mapped)+Guard+(y*Width+x)*4,pixel.data(),4);}
        std::memcpy(original.mapped,OriginalAdd.data(),16);std::memcpy(replacement.mapped,ReplacementAdd.data(),16);
        Image source(rt,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
        rt.Begin();Upload(rt.commands,source,seed,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);rt.SubmitWait();
        Descriptors descriptors(rt,source,original,replacement);
        unsigned cases=0,checked=0;
        constexpr std::array<const char*,4> names{"original","all-binding-copy","copy+ssbo-override","explicit-rewrite+ssbo-override"};
        for(const auto imageLayout:{VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL})for(unsigned mode=0;mode<4;++mode){
            Buffer readback(rt,Guard+ImageBytes+Guard);std::memset(readback.mapped,0xc7,Guard+ImageBytes+Guard);
            Image destination(rt,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
            Pipeline pipeline(rt,destination,descriptors.layout,imageLayout);
            rt.Begin();
            Upload(rt.commands,destination,seed,imageLayout,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
            for(const Buffer* params:{&original,&replacement})BufferBarrier(rt.commands,params->handle,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
            // Mirrors lean draw's input dependency, independent of layout transitions.
            VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER};before.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;before.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(rt.commands,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,1,&before,0,nullptr,0,nullptr);
            VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};bi.renderPass=pipeline.pass;bi.framebuffer=pipeline.framebuffer;bi.renderArea={{0,0},{Width,Height}};
            vkCmdBeginRenderPass(rt.commands,&bi,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(rt.commands,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline.pipeline);
            vkCmdBindDescriptorSets(rt.commands,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline.layout,0,1,&descriptors.sets[mode],0,nullptr);
            vkCmdDraw(rt.commands,3,1,0,0);vkCmdEndRenderPass(rt.commands);
            ImageBarrier(rt.commands,destination.handle,imageLayout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
            VkBufferImageCopy copy{};copy.bufferOffset=Guard;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={Width,Height,1};
            vkCmdCopyImageToBuffer(rt.commands,destination.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.handle,1,&copy);
            BufferBarrier(rt.commands,readback.handle,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT);
            rt.SubmitWait();
            const auto* actual=static_cast<const unsigned char*>(readback.mapped);
            const auto& add=mode<2?OriginalAdd:ReplacementAdd;
            unsigned errors=0;std::set<std::uint32_t> colors;
            for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x){auto expected=Pattern(x,y);const auto* pixel=actual+Guard+(y*Width+x)*4;std::uint32_t packed=0;std::memcpy(&packed,pixel,4);colors.insert(packed);for(unsigned c=0;c<4;++c){expected[c]=static_cast<unsigned char>((expected[c]+add[c])&255);if(pixel[c]!=expected[c]){if(errors<4)std::printf("[descriptor-probe] mismatch xy=%u,%u channel=%u actual=%u expected=%u\n",x,y,c,pixel[c],expected[c]);++errors;}++checked;}}
            for(VkDeviceSize i=0;i<Guard;++i){errors+=actual[i]!=0xc7;errors+=actual[Guard+ImageBytes+i]!=0xc7;}
            std::printf("[descriptor-probe] case=%u layout=%s mode=%s channels=%llu colors=%zu guard_bytes=%llu errors=%u\n",++cases,imageLayout==VK_IMAGE_LAYOUT_GENERAL?"GENERAL":"COLOR_ATTACHMENT_OPTIMAL",names[mode],static_cast<unsigned long long>(ImageBytes),colors.size(),static_cast<unsigned long long>(Guard*2),errors);
            Require(errors==0,"descriptor/layout GPU scalar mismatch or guard corruption");Require(colors.size()>200,"insufficient output diversity");
        }
        Require(cases==8&&checked==10336,"incomplete descriptor factorial coverage");
        std::printf("[descriptor-probe] PASS cases=%u checked_channels=%u strict_error_unorm_units=0 scope=ordinary-texture2D-separate-sampler-SSBO-native-Vulkan-not-AnyPS5-Draw\n",cases,checked);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"[descriptor-probe] FAIL %s\n",e.what());return 1;}
}
