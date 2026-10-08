// Original deterministic Vulkan adapter for production stencil sample transfer ownership/barriers.
// GPU bytes are independently checked by color_sample_transfer_probe.cpp.
#include "prx/libSceAgcDriver/Graphics/include/StencilSampleTransfer.hpp"
#include <map>
#include <cassert>
#include <cstdio>
#include <set>
#include <string_view>
#include <type_traits>
#include <bit>
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
unsigned programCreations=0;
bool partialPipeline=false;
unsigned transferStep=0, transferFail=0, recorded=0, draws=0, dispatches=0;
unsigned currentGroup=0, currentSample=0, currentMask=0, logicalCount=8, currentBit=0, stencilWriteMask=0;
VkFormat expectedFormat=VK_FORMAT_D32_SFLOAT_S8_UINT;
VkDeviceSize packedBytes(){return (17u*19u*logicalCount+3u)&~VkDeviceSize(3u);}
std::array<VkSampleLocationEXT,8> positions{{{2.f/16,2.f/16},{6.f/16,2.f/16},{10.f/16,2.f/16},{14.f/16,2.f/16},{2.f/16,10.f/16},{6.f/16,10.f/16},{10.f/16,10.f/16},{14.f/16,10.f/16}}};
void checkLocations(const VkSampleLocationsInfoEXT& pattern,unsigned group){
    assert(pattern.sType==VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT && pattern.sampleLocationGridSize.width==1 && pattern.sampleLocationGridSize.height==1 && pattern.sampleLocationsPerPixel==expectedSamples && pattern.sampleLocationsCount==static_cast<unsigned>(expectedSamples));
    for(unsigned i=0;i<pattern.sampleLocationsCount;++i)assert(pattern.pSampleLocations[i].x==positions[group*expectedSamples+i].x && pattern.pSampleLocations[i].y==positions[group*expectedSamples+i].y);
}
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
    assert(info->setLayoutCount==1 && info->pushConstantRangeCount==1 && info->pPushConstantRanges->size==20);
    return makeHandle("pipelineLayout",out);
}
VKAPI_ATTR void VKAPI_CALL destroyPipelineLayout(VkDevice,VkPipelineLayout id,const VkAllocationCallbacks*){destroy("pipelineLayout",id);}
VKAPI_ATTR VkResult VKAPI_CALL createPass(VkDevice,const VkRenderPassCreateInfo* info,const VkAllocationCallbacks*,VkRenderPass* out){
    assert(info->attachmentCount==1 && info->pAttachments->format==expectedFormat && info->pAttachments->samples==expectedSamples);
    assert(info->pAttachments->loadOp==VK_ATTACHMENT_LOAD_OP_LOAD && info->pAttachments->storeOp==VK_ATTACHMENT_STORE_OP_STORE);
    assert(info->subpassCount==1 && !info->pSubpasses->pResolveAttachments && info->pSubpasses->colorAttachmentCount==0 && info->pSubpasses->pDepthStencilAttachment);
    assert(info->pAttachments->stencilLoadOp==VK_ATTACHMENT_LOAD_OP_LOAD && info->pAttachments->stencilStoreOp==VK_ATTACHMENT_STORE_OP_STORE && info->pAttachments->initialLayout==VK_IMAGE_LAYOUT_GENERAL && info->pAttachments->finalLayout==VK_IMAGE_LAYOUT_GENERAL);
    return makeHandle("pass",out);
}
VKAPI_ATTR void VKAPI_CALL destroyPass(VkDevice,VkRenderPass id,const VkAllocationCallbacks*){destroy("pass",id);}
VKAPI_ATTR VkResult VKAPI_CALL createFramebuffer(VkDevice,const VkFramebufferCreateInfo* info,const VkAllocationCallbacks*,VkFramebuffer* out){
    assert(info->attachmentCount==1 && info->layers==1 && info->width==17 && info->height==19);
    const auto result=makeHandle("framebuffer",out);
    if(result==VK_SUCCESS)framebufferGroups[number(*out)]=static_cast<unsigned>(framebufferGroups.size()%expectedLayers);
    return result;
}
VKAPI_ATTR void VKAPI_CALL destroyFramebufferMock(VkDevice,VkFramebuffer id,const VkAllocationCallbacks*){destroy("framebuffer",id);assert(framebufferGroups.erase(number(id))==1);}
VKAPI_ATTR VkResult VKAPI_CALL createModule(VkDevice,const VkShaderModuleCreateInfo* info,const VkAllocationCallbacks*,VkShaderModule* out){
    assert(info->codeSize>=20 && info->codeSize%4==0 && info->pCode[0]==0x07230203);return makeHandle("module",out);
}
VKAPI_ATTR void VKAPI_CALL destroyModule(VkDevice,VkShaderModule id,const VkAllocationCallbacks*){destroy("module",id);}
VKAPI_ATTR VkResult VKAPI_CALL createGraphics(VkDevice,VkPipelineCache,std::uint32_t count,const VkGraphicsPipelineCreateInfo* info,const VkAllocationCallbacks*,VkPipeline* out){
    assert(count==1 && info->stageCount==2 && info->pMultisampleState->rasterizationSamples==expectedSamples);
    assert(!info->pMultisampleState->sampleShadingEnable && info->pColorBlendState->attachmentCount==0);
    assert(info->pDepthStencilState && !info->pDepthStencilState->depthTestEnable && !info->pDepthStencilState->depthWriteEnable && info->pDepthStencilState->stencilTestEnable);
    for(auto state:{info->pDepthStencilState->front,info->pDepthStencilState->back})assert(state.failOp==VK_STENCIL_OP_KEEP && state.depthFailOp==VK_STENCIL_OP_KEEP && state.passOp==VK_STENCIL_OP_REPLACE && state.compareOp==VK_COMPARE_OP_ALWAYS && state.reference==255);
    const auto* locations=static_cast<const VkPipelineSampleLocationsStateCreateInfoEXT*>(info->pMultisampleState->pNext);
    assert(locations && locations->sType==VK_STRUCTURE_TYPE_PIPELINE_SAMPLE_LOCATIONS_STATE_CREATE_INFO_EXT && locations->sampleLocationsEnable);
    const auto group=locations->sampleLocationsInfo.pSampleLocations[0].y==positions[4].y?1u:0u;checkLocations(locations->sampleLocationsInfo,group);
    const auto mask=*info->pMultisampleState->pSampleMask;
    assert(mask && !(mask&(mask-1)) && mask<(1u<<static_cast<unsigned>(expectedSamples)));
    auto result=makeHandle("pipeline",out);
    if(result!=VK_SUCCESS&&partialPipeline){*out=handle<VkPipeline>(nextHandle++);objects[number(*out)]="pipeline";pipelineMasks[number(*out)]=mask;}
    if(result==VK_SUCCESS){pipelineMasks[number(*out)]=mask;++programCreations;}return result;
}
VKAPI_ATTR VkResult VKAPI_CALL createCompute(VkDevice,VkPipelineCache,std::uint32_t count,const VkComputePipelineCreateInfo* info,const VkAllocationCallbacks*,VkPipeline* out){
    assert(count==1 && info->stage.stage==VK_SHADER_STAGE_COMPUTE_BIT);auto result=makeHandle("pipeline",out);
    if(result!=VK_SUCCESS&&partialPipeline){*out=handle<VkPipeline>(nextHandle++);objects[number(*out)]="pipeline";}
    if(result==VK_SUCCESS)++programCreations;return result;
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
    assert(writes[0].descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && writes[0].pBufferInfo->range==packedBytes());
    assert(writes[0].pBufferInfo->offset==256);
    if(count==2)assert(writes[1].descriptorType==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE && writes[1].pImageInfo->sampler==VK_NULL_HANDLE && writes[1].pImageInfo->imageLayout==VK_IMAGE_LAYOUT_GENERAL);
}
VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer,VkPipelineStageFlags source,VkPipelineStageFlags dest,VkDependencyFlags,unsigned memoryCount,const VkMemoryBarrier*,unsigned bufferCount,const VkBufferMemoryBarrier* buffers,unsigned imageCount,const VkImageMemoryBarrier* image){
    ++recorded;assert(source && dest && memoryCount==0 && bufferCount+imageCount==1);
    if(imageCount){
        assert(image->subresourceRange.layerCount==1 && image->subresourceRange.baseArrayLayer<expectedLayers && image->srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED && image->subresourceRange.aspectMask==(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT));
        assert(image->pNext);checkLocations(*static_cast<const VkSampleLocationsInfoEXT*>(image->pNext),image->subresourceRange.baseArrayLayer);
    }
    if(bufferCount)assert(buffers->offset==256 && buffers->size==packedBytes() && buffers->srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED);
}
VKAPI_ATTR void VKAPI_CALL begin(VkCommandBuffer,const VkRenderPassBeginInfo* info,VkSubpassContents){++recorded;currentGroup=framebufferGroups.at(number(info->framebuffer));assert(info->renderArea.extent.width==17 && info->renderArea.extent.height==19);
    const auto* locations=static_cast<const VkRenderPassSampleLocationsBeginInfoEXT*>(info->pNext);assert(locations && locations->sType==VK_STRUCTURE_TYPE_RENDER_PASS_SAMPLE_LOCATIONS_BEGIN_INFO_EXT && locations->attachmentInitialSampleLocationsCount==1 && locations->postSubpassSampleLocationsCount==1);
    assert(locations->pAttachmentInitialSampleLocations->attachmentIndex==0 && locations->pPostSubpassSampleLocations->subpassIndex==0);
    checkLocations(locations->pAttachmentInitialSampleLocations->sampleLocationsInfo,currentGroup);checkLocations(locations->pPostSubpassSampleLocations->sampleLocationsInfo,currentGroup);}
VKAPI_ATTR void VKAPI_CALL end(VkCommandBuffer){++recorded;}
VKAPI_ATTR void VKAPI_CALL bindPipeline(VkCommandBuffer,VkPipelineBindPoint point,VkPipeline pipeline){++recorded;if(point==VK_PIPELINE_BIND_POINT_GRAPHICS)currentMask=pipelineMasks.at(number(pipeline));}
VKAPI_ATTR void VKAPI_CALL bindSet(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,unsigned,unsigned count,const VkDescriptorSet* sets,unsigned,const unsigned*){++recorded;assert(count==1 && writtenDescriptors.count(number(*sets)));}
VKAPI_ATTR void VKAPI_CALL push(VkCommandBuffer,VkPipelineLayout,VkShaderStageFlags stages,unsigned offset,unsigned bytes,const void* data){
    ++recorded;assert(offset==0 && bytes==20);const auto* parameters=static_cast<const unsigned*>(data);
    assert(parameters[0]==17 && parameters[1]==19 && parameters[2]==logicalCount);
    if(stages==VK_SHADER_STAGE_FRAGMENT_BIT){currentSample=parameters[3];currentBit=parameters[4];assert(currentBit<8);}else assert(stages==VK_SHADER_STAGE_COMPUTE_BIT && parameters[3]==static_cast<unsigned>(expectedSamples) && parameters[4]==17u*19u*logicalCount);
}
VKAPI_ATTR void VKAPI_CALL viewport(VkCommandBuffer,unsigned first,unsigned count,const VkViewport* value){++recorded;assert(first==0 && count==1 && value->width==17 && value->height==19);}
VKAPI_ATTR void VKAPI_CALL scissor(VkCommandBuffer,unsigned first,unsigned count,const VkRect2D* value){++recorded;assert(first==0 && count==1 && value->extent.width==17 && value->extent.height==19);}
VKAPI_ATTR void VKAPI_CALL draw(VkCommandBuffer,unsigned vertices,unsigned instances,unsigned firstVertex,unsigned firstInstance){
    ++recorded;++draws;assert(vertices==3 && instances==1 && firstVertex==0 && firstInstance==0);
    assert(currentSample/static_cast<unsigned>(expectedSamples)==currentGroup && currentMask==(1u<<(currentSample%expectedSamples)));
    assert(stencilWriteMask==(1u<<currentBit));logicalWrites.push_back(currentSample*8+currentBit);
}
VKAPI_ATTR void VKAPI_CALL dispatch(VkCommandBuffer,unsigned x,unsigned y,unsigned z){++recorded;++dispatches;assert(x==(packedBytes()/4+63)/64 && y==1 && z==1);}
VKAPI_ATTR void VKAPI_CALL stencilMask(VkCommandBuffer,VkStencilFaceFlags faces,unsigned mask){++recorded;assert(faces==VK_STENCIL_FACE_FRONT_AND_BACK && mask && !(mask&(mask-1)) && mask<256);stencilWriteMask=mask;}
VKAPI_ATTR void VKAPI_CALL stencilClear(VkCommandBuffer,VkImage,VkImageLayout layout,const VkClearDepthStencilValue* value,unsigned count,const VkImageSubresourceRange* ranges){++recorded;assert(layout==VK_IMAGE_LAYOUT_GENERAL && value->stencil==0 && count==1 && ranges->aspectMask==VK_IMAGE_ASPECT_STENCIL_BIT && ranges->layerCount==expectedLayers);}
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
    ENTRY("vkCmdSetStencilWriteMask",stencilMask);ENTRY("vkCmdClearDepthStencilImage",stencilClear);
    ENTRY("vkCmdSetViewport",viewport);ENTRY("vkCmdSetScissor",scissor);ENTRY("vkCmdDraw",draw);ENTRY("vkCmdDispatch",dispatch);
#undef ENTRY
    return resolve(device,raw);
}
template<class F> void transferRejects(F fn){bool failed=false;try{fn();}catch(const std::runtime_error&){failed=true;}assert(failed);}
void clean(){assert(objects.empty()&&images.empty()&&memory.empty()&&views.empty()&&descriptorParents.empty()&&writtenDescriptors.empty()&&framebufferGroups.empty());}
}
Context makeContext(){
    Context context{};context.device=handle<VkDevice>(1);context.physical=handle<VkPhysicalDevice>(1);
    context.deviceProc=transferResolve;context.formatProperties=format;context.imageFormatProperties=imageFormat;
    supportedFeatures|=VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    context.memory.memoryTypeCount=1;context.memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    context.limits.maxFramebufferWidth=context.limits.maxFramebufferHeight=4096;
    context.limits.maxViewportDimensions[0]=context.limits.maxViewportDimensions[1]=4096;
    context.limits.maxPushConstantsSize=128;context.limits.maxStorageBufferRange=1u<<26;
    context.limits.minStorageBufferOffsetAlignment=256;
    context.limits.maxPerStageDescriptorStorageBuffers=context.limits.maxDescriptorSetStorageBuffers=8;
    context.limits.maxPerStageDescriptorSampledImages=context.limits.maxDescriptorSetSampledImages=8;
    context.limits.maxComputeWorkGroupSize[0]=context.limits.maxComputeWorkGroupSize[1]=1024;
    context.limits.maxComputeWorkGroupInvocations=1024;context.limits.maxComputeWorkGroupCount[0]=context.limits.maxComputeWorkGroupCount[1]=65535;
    context.sampleLocations=true;context.multisampleArrayImage=true;
    context.sampleLocationProperties.sampleLocationSampleCounts=VK_SAMPLE_COUNT_2_BIT|VK_SAMPLE_COUNT_4_BIT;
    context.sampleLocationProperties.sampleLocationSubPixelBits=4;
    context.sampleLocationProperties.maxSampleLocationGridSize={1,1};
    context.sampleLocationProperties.sampleLocationCoordinateRange[0]=0;
    context.sampleLocationProperties.sampleLocationCoordinateRange[1]=0.9375f;
    return context;
}
int main(){
    auto context=makeContext();const auto basePositions=positions;
    for (const auto capacity : {0u, 9u}) {
        transferRejects([&] { StencilSampleProgramCache rejected(context, capacity); });
        assert(programCreations == 0); clean();
    }
    const VkImage image=handle<VkImage>(1000);const std::array<VkImageView,2> attachments{handle<VkImageView>(1001),handle<VkImageView>(1002)};
    expectedSamples=VK_SAMPLE_COUNT_4_BIT;expectedLayers=2;logicalCount=8;expectedFormat=VK_FORMAT_D32_SFLOAT_S8_UINT;
    const ColorSampleImageAccess attachment{VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    auto construct=[&](const Context& c,VkFormat f=VK_FORMAT_D32_SFLOAT_S8_UINT){return std::make_unique<StencilSampleTransfer>(c,image,f,attachments,VkExtent2D{17,19},8,positions);};
    unsigned warmRejects=0;
    {
        StencilSampleProgramCache cache(context,2);context.stencilSamplePrograms=&cache;
        auto first=construct(context);assert(programCreations==9);const auto firstHandles=pipelineMasks;
        auto hit=construct(context);assert(programCreations==9&&firstHandles==pipelineMasks);
        for(unsigned variation=0;variation<8;++variation){auto bad=context;auto before=programCreations;
            switch(variation){case 0:bad.sampleLocations=false;break;case 1:bad.multisampleArrayImage=false;break;case 2:bad.sampleLocationProperties.sampleLocationSampleCounts=0;break;case 3:bad.limits.maxStorageBufferRange=4;break;case 4:bad.limits.maxFramebufferWidth=16;break;case 5:bad.limits.maxDescriptorSetStorageBuffers=0;break;case 6:bad.device=handle<VkDevice>(99);break;case 7:bad.physical=handle<VkPhysicalDevice>(99);break;}
            transferRejects([&]{auto rejected=construct(bad);});assert(programCreations==before);++warmRejects;
        }
        std::swap(positions[0],positions[1]);auto changed=construct(context);assert(programCreations==18);
        positions=basePositions;hit.reset();hit=construct(context);assert(programCreations==18);
        positions[0].x=1.f/16;auto eviction=construct(context);assert(programCreations==27);
        // The evicted ordered-position bundle is still held by its transfer.
        assert(pipelineMasks.size()==24);changed.reset();assert(pipelineMasks.size()==16);
        positions=basePositions;cache.Clear();assert(pipelineMasks.size()==16);
        first.reset();assert(pipelineMasks.size()==16); // same bundle remains in hit.
        draws=recorded=0;logicalWrites.clear();
        hit->RecordUpload(handle<VkCommandBuffer>(1),{handle<VkBuffer>(1024),256,packedBytes(),VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT},attachment,attachment);
        assert(draws==64&&logicalWrites.size()==64);
        hit.reset();assert(pipelineMasks.size()==8);eviction.reset();assert(pipelineMasks.empty());
        cache.Clear();
    }
    context.stencilSamplePrograms=nullptr;clean();
    // Bit-identical keys distinguish +0 and -0, ordered positions, formats,
    // logical/native counts and independent per-device cache domains.
    {
        StencilSampleProgramCache cache(context,8);context.stencilSamplePrograms=&cache;auto before=programCreations;
        positions=basePositions;positions[0].x=0.f;{auto p=construct(context);}assert(programCreations==before+9);
        positions[0].x=std::bit_cast<float>(0x80000000u);{auto p=construct(context);}assert(programCreations==before+18);
        expectedFormat=VK_FORMAT_D16_UNORM_S8_UINT;{auto p=construct(context,expectedFormat);}assert(programCreations==before+27);
        expectedFormat=VK_FORMAT_D32_SFLOAT_S8_UINT;positions=basePositions;
        expectedLayers=1;logicalCount=2;expectedSamples=VK_SAMPLE_COUNT_2_BIT;
        {StencilSampleTransfer p(context,image,expectedFormat,std::span(attachments).first(1),{17,19},2,std::span(positions).first(2));}assert(programCreations==before+30);
        logicalCount=4;expectedSamples=VK_SAMPLE_COUNT_4_BIT;
        {StencilSampleTransfer p(context,image,expectedFormat,std::span(attachments).first(1),{17,19},4,std::span(positions).first(4));}assert(programCreations==before+35);
        expectedLayers=2;logicalCount=8;
        auto other=context;other.device=handle<VkDevice>(2);other.physical=handle<VkPhysicalDevice>(2);StencilSampleProgramCache second(other,2);other.stencilSamplePrograms=&second;{auto p=construct(other);}assert(programCreations==before+44);
        second.Clear();cache.Clear();
    }
    context.stencilSamplePrograms=nullptr;clean();
    // Every constructor failure unwinds local views/framebuffers and partial
    // program handles. A complete cached bundle may survive a framebuffer
    // failure, but Clear must release it and a subsequent construction retries.
    unsigned failures=0;
    for(unsigned fail=1;fail<=19;++fail){StencilSampleProgramCache cache(context,2);context.stencilSamplePrograms=&cache;transferStep=0;transferFail=fail;partialPipeline=true;
        transferRejects([&]{auto p=construct(context);});transferFail=0;partialPipeline=false;cache.Clear();clean();
        {auto retry=construct(context);}cache.Clear();clean();++failures;
    }
    context.stencilSamplePrograms=nullptr;positions=basePositions;
    std::printf("Stencil immutable program cache contract PASS: exact ordered bit keys, formats/counts/device domains, invalid_capacity=2 warm_invalid=%u retained_after_eviction_clear=PASS raster_draws=64 failure_unwind_retry=%u leaked_handles=0\n",warmRejects,failures);
}
