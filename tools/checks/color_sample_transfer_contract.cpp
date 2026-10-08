// Original deterministic Vulkan adapter for production sample transfer ownership/barriers.
// GPU bytes are independently checked by color_sample_transfer_probe.cpp.
#include "prx/libSceAgcDriver/Graphics/include/ColorSampleTransfer.hpp"
#include <map>
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
namespace {
std::map<std::uintptr_t,std::string> objects;
std::map<std::uintptr_t,std::uintptr_t> descriptorParents;
std::map<std::uintptr_t,unsigned> framebufferGroups;
std::map<std::uintptr_t,unsigned> pipelineMasks;
std::set<std::uintptr_t> writtenDescriptors;
unsigned transferStep=0, transferFail=0, recorded=0, draws=0, dispatches=0;
unsigned currentGroup=0, currentSample=0, currentMask=0, logicalCount=8;
std::vector<unsigned> logicalWrites;
std::string missing;
VkResult make(std::string type,std::uintptr_t& id){
    if(++transferStep==transferFail)return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    id=nextHandle++;objects.emplace(id,std::move(type));return VK_SUCCESS;
}
template<class T> VkResult makeHandle(const char* type,T* out){std::uintptr_t id=0;auto result=make(type,id);*out=handle<T>(id);return result;}
template<class T> void destroy(const char* type,T id){auto i=objects.find(number(id));assert(i!=objects.end()&&i->second==type);objects.erase(i);}
VKAPI_ATTR VkResult VKAPI_CALL createDescriptorLayout(VkDevice,const VkDescriptorSetLayoutCreateInfo* info,const VkAllocationCallbacks*,VkDescriptorSetLayout* out){
    assert(info->bindingCount==1 || info->bindingCount==2);
    return makeHandle("descriptorLayout",out);
}
VKAPI_ATTR void VKAPI_CALL destroyDescriptorLayout(VkDevice,VkDescriptorSetLayout id,const VkAllocationCallbacks*){destroy("descriptorLayout",id);}
VKAPI_ATTR VkResult VKAPI_CALL createPipelineLayout(VkDevice,const VkPipelineLayoutCreateInfo* info,const VkAllocationCallbacks*,VkPipelineLayout* out){
    assert(info->setLayoutCount==1 && info->pushConstantRangeCount==1 && info->pPushConstantRanges->size==16);
    return makeHandle("pipelineLayout",out);
}
VKAPI_ATTR void VKAPI_CALL destroyPipelineLayout(VkDevice,VkPipelineLayout id,const VkAllocationCallbacks*){destroy("pipelineLayout",id);}
VKAPI_ATTR VkResult VKAPI_CALL createPass(VkDevice,const VkRenderPassCreateInfo* info,const VkAllocationCallbacks*,VkRenderPass* out){
    assert(info->attachmentCount==1 && info->pAttachments->format==VK_FORMAT_R8G8B8A8_UNORM && info->pAttachments->samples==expectedSamples);
    assert(info->pAttachments->loadOp==VK_ATTACHMENT_LOAD_OP_LOAD && info->pAttachments->storeOp==VK_ATTACHMENT_STORE_OP_STORE);
    assert(info->subpassCount==1 && !info->pSubpasses->pResolveAttachments);
    return makeHandle("pass",out);
}
VKAPI_ATTR void VKAPI_CALL destroyPass(VkDevice,VkRenderPass id,const VkAllocationCallbacks*){destroy("pass",id);}
VKAPI_ATTR VkResult VKAPI_CALL createFramebuffer(VkDevice,const VkFramebufferCreateInfo* info,const VkAllocationCallbacks*,VkFramebuffer* out){
    assert(info->attachmentCount==1 && info->layers==1 && info->width==17 && info->height==19);
    const auto result=makeHandle("framebuffer",out);
    if(result==VK_SUCCESS)framebufferGroups[number(*out)]=static_cast<unsigned>(framebufferGroups.size());
    return result;
}
VKAPI_ATTR void VKAPI_CALL destroyFramebufferMock(VkDevice,VkFramebuffer id,const VkAllocationCallbacks*){destroy("framebuffer",id);assert(framebufferGroups.erase(number(id))==1);}
VKAPI_ATTR VkResult VKAPI_CALL createModule(VkDevice,const VkShaderModuleCreateInfo* info,const VkAllocationCallbacks*,VkShaderModule* out){
    assert(info->codeSize>=20 && info->codeSize%4==0 && info->pCode[0]==0x07230203);return makeHandle("module",out);
}
VKAPI_ATTR void VKAPI_CALL destroyModule(VkDevice,VkShaderModule id,const VkAllocationCallbacks*){destroy("module",id);}
VKAPI_ATTR VkResult VKAPI_CALL createGraphics(VkDevice,VkPipelineCache,std::uint32_t count,const VkGraphicsPipelineCreateInfo* info,const VkAllocationCallbacks*,VkPipeline* out){
    assert(count==1 && info->stageCount==2 && info->pMultisampleState->rasterizationSamples==expectedSamples);
    assert(!info->pMultisampleState->sampleShadingEnable && !info->pColorBlendState->pAttachments->blendEnable && info->pColorBlendState->pAttachments->colorWriteMask==15);
    const auto mask=*info->pMultisampleState->pSampleMask;
    assert(mask && !(mask&(mask-1)) && mask<(1u<<static_cast<unsigned>(expectedSamples)));
    const auto result=makeHandle("pipeline",out);if(result==VK_SUCCESS)pipelineMasks[number(*out)]=mask;return result;
}
VKAPI_ATTR VkResult VKAPI_CALL createCompute(VkDevice,VkPipelineCache,std::uint32_t count,const VkComputePipelineCreateInfo* info,const VkAllocationCallbacks*,VkPipeline* out){
    assert(count==1 && info->stage.stage==VK_SHADER_STAGE_COMPUTE_BIT);return makeHandle("pipeline",out);
}
VKAPI_ATTR void VKAPI_CALL destroyPipeline(VkDevice,VkPipeline id,const VkAllocationCallbacks*){destroy("pipeline",id);pipelineMasks.erase(number(id));}
VKAPI_ATTR VkResult VKAPI_CALL createPool(VkDevice,const VkDescriptorPoolCreateInfo* info,const VkAllocationCallbacks*,VkDescriptorPool* out){assert(info->maxSets==1);return makeHandle("pool",out);}
VKAPI_ATTR void VKAPI_CALL destroyPool(VkDevice,VkDescriptorPool id,const VkAllocationCallbacks*){
    destroy("pool",id);
    for(auto i=descriptorParents.begin();i!=descriptorParents.end();){if(i->second==number(id)){writtenDescriptors.erase(i->first);destroy("set",handle<VkDescriptorSet>(i->first));i=descriptorParents.erase(i);}else ++i;}
}
VKAPI_ATTR VkResult VKAPI_CALL allocateSet(VkDevice,const VkDescriptorSetAllocateInfo* info,VkDescriptorSet* out){
    assert(info->descriptorSetCount==1);const auto result=makeHandle("set",out);if(result==VK_SUCCESS)descriptorParents[number(*out)]=number(info->descriptorPool);return result;
}
VKAPI_ATTR void VKAPI_CALL updateSet(VkDevice,unsigned count,const VkWriteDescriptorSet* writes,unsigned,const VkCopyDescriptorSet*){
    assert(count==1||count==2);assert(writtenDescriptors.insert(number(writes[0].dstSet)).second);
    assert(writes[0].descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && writes[0].pBufferInfo->range==17u*19u*logicalCount*4u);
    assert(writes[0].pBufferInfo->offset==256);
    if(count==2)assert(writes[1].descriptorType==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE && writes[1].pImageInfo->sampler==VK_NULL_HANDLE && writes[1].pImageInfo->imageLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer,VkPipelineStageFlags source,VkPipelineStageFlags dest,VkDependencyFlags,unsigned memoryCount,const VkMemoryBarrier*,unsigned bufferCount,const VkBufferMemoryBarrier* buffers,unsigned imageCount,const VkImageMemoryBarrier* image){
    ++recorded;assert(source && dest && memoryCount==0 && bufferCount+imageCount==1);
    if(imageCount)assert(image->subresourceRange.layerCount==expectedLayers && image->subresourceRange.baseArrayLayer==0 && image->srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED);
    if(bufferCount)assert(buffers->offset==256 && buffers->size==17u*19u*logicalCount*4u && buffers->srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED);
}
VKAPI_ATTR void VKAPI_CALL begin(VkCommandBuffer,const VkRenderPassBeginInfo* info,VkSubpassContents){++recorded;currentGroup=framebufferGroups.at(number(info->framebuffer));assert(info->renderArea.extent.width==17 && info->renderArea.extent.height==19);}
VKAPI_ATTR void VKAPI_CALL end(VkCommandBuffer){++recorded;}
VKAPI_ATTR void VKAPI_CALL bindPipeline(VkCommandBuffer,VkPipelineBindPoint point,VkPipeline pipeline){++recorded;if(point==VK_PIPELINE_BIND_POINT_GRAPHICS)currentMask=pipelineMasks.at(number(pipeline));}
VKAPI_ATTR void VKAPI_CALL bindSet(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,unsigned,unsigned count,const VkDescriptorSet* sets,unsigned,const unsigned*){++recorded;assert(count==1 && writtenDescriptors.count(number(*sets)));}
VKAPI_ATTR void VKAPI_CALL push(VkCommandBuffer,VkPipelineLayout,VkShaderStageFlags stages,unsigned offset,unsigned bytes,const void* data){
    ++recorded;assert(offset==0 && bytes==16);const auto* parameters=static_cast<const unsigned*>(data);
    assert(parameters[0]==17 && parameters[1]==19 && parameters[2]==logicalCount);
    if(stages==VK_SHADER_STAGE_FRAGMENT_BIT)currentSample=parameters[3];else assert(stages==VK_SHADER_STAGE_COMPUTE_BIT && parameters[3]==static_cast<unsigned>(expectedSamples));
}
VKAPI_ATTR void VKAPI_CALL viewport(VkCommandBuffer,unsigned first,unsigned count,const VkViewport* value){++recorded;assert(first==0 && count==1 && value->width==17 && value->height==19);}
VKAPI_ATTR void VKAPI_CALL scissor(VkCommandBuffer,unsigned first,unsigned count,const VkRect2D* value){++recorded;assert(first==0 && count==1 && value->extent.width==17 && value->extent.height==19);}
VKAPI_ATTR void VKAPI_CALL draw(VkCommandBuffer,unsigned vertices,unsigned instances,unsigned firstVertex,unsigned firstInstance){
    ++recorded;++draws;assert(vertices==3 && instances==1 && firstVertex==0 && firstInstance==0);
    assert(currentSample/static_cast<unsigned>(expectedSamples)==currentGroup && currentMask==(1u<<(currentSample%expectedSamples)));
    logicalWrites.push_back(currentSample);
}
VKAPI_ATTR void VKAPI_CALL dispatch(VkCommandBuffer,unsigned x,unsigned y,unsigned z){++recorded;++dispatches;assert(x==3 && y==3 && z==1);}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL transferResolve(VkDevice device,const char* raw){
    const std::string_view name=raw;if(name==missing)return nullptr;
#define ENTRY(n,f) if(name==n)return reinterpret_cast<PFN_vkVoidFunction>(f)
    ENTRY("vkCreateDescriptorSetLayout",createDescriptorLayout);ENTRY("vkDestroyDescriptorSetLayout",destroyDescriptorLayout);
    ENTRY("vkCreatePipelineLayout",createPipelineLayout);ENTRY("vkDestroyPipelineLayout",destroyPipelineLayout);
    ENTRY("vkCreateRenderPass",createPass);ENTRY("vkDestroyRenderPass",destroyPass);
    ENTRY("vkCreateFramebuffer",createFramebuffer);ENTRY("vkDestroyFramebuffer",destroyFramebufferMock);
    ENTRY("vkCreateShaderModule",createModule);ENTRY("vkDestroyShaderModule",destroyModule);
    ENTRY("vkCreateGraphicsPipelines",createGraphics);ENTRY("vkCreateComputePipelines",createCompute);ENTRY("vkDestroyPipeline",destroyPipeline);
    ENTRY("vkCreateDescriptorPool",createPool);ENTRY("vkDestroyDescriptorPool",destroyPool);ENTRY("vkAllocateDescriptorSets",allocateSet);ENTRY("vkUpdateDescriptorSets",updateSet);
    ENTRY("vkCmdPipelineBarrier",barrier);ENTRY("vkCmdBeginRenderPass",begin);ENTRY("vkCmdEndRenderPass",end);
    ENTRY("vkCmdBindPipeline",bindPipeline);ENTRY("vkCmdBindDescriptorSets",bindSet);ENTRY("vkCmdPushConstants",push);
    ENTRY("vkCmdSetViewport",viewport);ENTRY("vkCmdSetScissor",scissor);ENTRY("vkCmdDraw",draw);ENTRY("vkCmdDispatch",dispatch);
#undef ENTRY
    return resolve(device,raw);
}
template<class F> void transferRejects(F fn){bool failed=false;try{fn();}catch(const std::runtime_error&){failed=true;}assert(failed);}
void clean(){assert(objects.empty()&&images.empty()&&memory.empty()&&views.empty()&&descriptorParents.empty()&&writtenDescriptors.empty()&&framebufferGroups.empty());}
}
int main(){
    Context context{};context.device=handle<VkDevice>(1);context.physical=handle<VkPhysicalDevice>(1);
    context.deviceProc=transferResolve;context.formatProperties=format;context.imageFormatProperties=imageFormat;
    context.memory.memoryTypeCount=1;context.memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    context.limits.maxFramebufferWidth=context.limits.maxFramebufferHeight=4096;
    context.limits.maxViewportDimensions[0]=context.limits.maxViewportDimensions[1]=4096;
    context.limits.maxPushConstantsSize=128;context.limits.maxStorageBufferRange=1u<<26;
    context.limits.minStorageBufferOffsetAlignment=256;
    context.limits.maxPerStageDescriptorStorageBuffers=context.limits.maxDescriptorSetStorageBuffers=8;
    context.limits.maxPerStageDescriptorSampledImages=context.limits.maxDescriptorSetSampledImages=8;
    context.limits.maxComputeWorkGroupSize[0]=context.limits.maxComputeWorkGroupSize[1]=1024;
    context.limits.maxComputeWorkGroupInvocations=1024;context.limits.maxComputeWorkGroupCount[0]=context.limits.maxComputeWorkGroupCount[1]=65535;
    ColorTarget color{};color.extent={17,19};color.format=VK_FORMAT_R8G8B8A8_UNORM;
    const ColorSampleImageAccess attachment{VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    const ColorSampleImageAccess initial{VK_IMAGE_LAYOUT_UNDEFINED,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,0};
    unsigned successes=0,rejections=0;
    for(auto samples:{2u,4u,8u}){
        logicalCount=color.samples=color.fragments=samples;expectedSamples=static_cast<VkSampleCountFlagBits>(std::min(samples,4u));expectedLayers=samples/expectedSamples;
        transferStep=transferFail=0;
        {
            RenderTarget target(context,color,false);
            ColorSampleTransfer transfer(context,target,color);
            const auto constructionSteps=transferStep;
            assert(transfer.PackedBytes()==17u*19u*samples*4u);
            const ColorSampleBufferAccess input{handle<VkBuffer>(1024),256,transfer.PackedBytes(),VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT};
            logicalWrites.clear();draws=dispatches=recorded=0;
            transfer.RecordUpload(handle<VkCommandBuffer>(1),input,initial,attachment);
            assert(draws==samples && logicalWrites.size()==samples);
            for(unsigned sample=0;sample<samples;++sample)assert(logicalWrites[sample]==sample);
            transfer.RecordReadback(handle<VkCommandBuffer>(1),{input.buffer,256,input.bytes,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT},attachment,attachment,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
            assert(dispatches==1 && descriptorParents.size()==2);
            // A second pair records fresh immutable descriptor sets instead of rewriting pending ones.
            transfer.RecordUpload(handle<VkCommandBuffer>(1),input,attachment,attachment);
            transfer.RecordReadback(handle<VkCommandBuffer>(1),input,attachment,attachment,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
            assert(descriptorParents.size()==4 && dispatches==2);
            ++successes;
            for(auto name:{"vkCmdDraw","vkCmdBindDescriptorSets","vkCreateDescriptorPool","vkAllocateDescriptorSets","vkUpdateDescriptorSets"}){
                missing=name;recorded=0;transferRejects([&]{transfer.RecordUpload(handle<VkCommandBuffer>(1),input,attachment,attachment);});assert(recorded==0);missing.clear();++rejections;
            }
            for(unsigned fail=1;fail<=2;++fail){
                transferFail=transferStep+fail;recorded=0;transferRejects([&]{transfer.RecordUpload(handle<VkCommandBuffer>(1),input,attachment,attachment);});assert(recorded==0 && descriptorParents.size()==4);transferFail=0;++rejections;
            }
            for(auto bad:{ColorSampleBufferAccess{input.buffer,128,input.bytes,input.stage,input.access},ColorSampleBufferAccess{input.buffer,256,input.bytes-1,input.stage,input.access},ColorSampleBufferAccess{input.buffer,256,input.bytes,0,0},ColorSampleBufferAccess{}}){
                recorded=0;transferRejects([&]{transfer.RecordUpload(handle<VkCommandBuffer>(1),bad,initial,attachment);});assert(recorded==0);++rejections;
            }
            recorded=0;transferRejects([&]{transfer.RecordReadback(handle<VkCommandBuffer>(1),input,initial,attachment,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);});assert(recorded==0);++rejections;
            // Constructor failure injection covers all successfully allocated resources.
            for(unsigned fail=1;fail<=constructionSteps;++fail){
                transferStep=0;transferFail=fail;
                const auto before=objects.size();transferRejects([&]{ColorSampleTransfer failing(context,target,color);});assert(objects.size()==before);++rejections;
            }
            transferFail=0;
            for(auto bad: {ColorTarget(color),ColorTarget(color),ColorTarget(color)}){
                const auto index=rejections%3;
                if(index==0)bad.extent.width=18;else if(index==1)bad.format=VK_FORMAT_B8G8R8A8_UNORM;else bad.fragments=1;
                const auto before=objects.size();transferRejects([&]{ColorSampleTransfer failing(context,target,bad);});assert(objects.size()==before);++rejections;
            }
            auto wrong=context;wrong.device=handle<VkDevice>(2);transferRejects([&]{ColorSampleTransfer failing(wrong,target,color);});++rejections;
        }
        clean();
    }
    std::printf("ColorSampleTransfer contract layouts=%u rejected_and_unwound=%u pending_descriptor_isolation=PASS barrier_sample_mapping=PASS leaked_handles=0 errors=0\n",successes,rejections);
}
