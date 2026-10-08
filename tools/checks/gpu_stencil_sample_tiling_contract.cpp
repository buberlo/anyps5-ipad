// Original deterministic ownership/limit/barrier test of the production helper.
// Byte correctness is tested separately on a real GPU against AMD AddrLib.
#include "prx/libSceAgcDriver/Graphics/include/GpuStencilSampleTiler.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <map>
#include <set>
#include <string_view>
#include <type_traits>
using namespace AgcDriver::Graphics;
namespace {
template<class T> T handle(std::uintptr_t id) { if constexpr(std::is_pointer_v<T>) return reinterpret_cast<T>(id); else return static_cast<T>(id); }
template<class T> std::uintptr_t number(T id) { if constexpr(std::is_pointer_v<T>) return reinterpret_cast<std::uintptr_t>(id); else return id; }
std::uintptr_t next=10;
std::map<std::uintptr_t,std::string> objects;
std::map<std::uintptr_t,std::uintptr_t> sets;
std::set<std::uintptr_t> updated;
std::vector<VkBufferMemoryBarrier> barriers;
std::vector<std::pair<VkPipelineStageFlags,VkPipelineStageFlags>> stages;
unsigned step=0, fail=0, pipelineCreations=0, commands=0, dispatches=0;
std::string missing;
std::array<std::uint32_t,5> lastPush{};
std::set<unsigned> specs;
template<class T> VkResult create(const char* type,T* out) {
    if(++step==fail) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *out=handle<T>(next++);objects[number(*out)]=type;return VK_SUCCESS;
}
template<class T> void destroy(const char* type,T id) {assert(objects.at(number(id))==type);objects.erase(number(id));}
VKAPI_ATTR VkResult VKAPI_CALL descriptorLayout(VkDevice,const VkDescriptorSetLayoutCreateInfo* info,const VkAllocationCallbacks*,VkDescriptorSetLayout* out) {
    assert(info->bindingCount==2);for(unsigned i=0;i<2;++i)assert(info->pBindings[i].binding==i&&info->pBindings[i].descriptorCount==1&&info->pBindings[i].descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER&&info->pBindings[i].stageFlags==VK_SHADER_STAGE_COMPUTE_BIT);
    return create("descriptors",out);
}
VKAPI_ATTR void VKAPI_CALL destroyDescriptors(VkDevice,VkDescriptorSetLayout id,const VkAllocationCallbacks*){destroy("descriptors",id);}
VKAPI_ATTR VkResult VKAPI_CALL layout(VkDevice,const VkPipelineLayoutCreateInfo* info,const VkAllocationCallbacks*,VkPipelineLayout* out){assert(info->setLayoutCount==1&&info->pushConstantRangeCount==1&&info->pPushConstantRanges->size==20);return create("layout",out);}
VKAPI_ATTR void VKAPI_CALL destroyLayout(VkDevice,VkPipelineLayout id,const VkAllocationCallbacks*){destroy("layout",id);}
VKAPI_ATTR VkResult VKAPI_CALL module(VkDevice,const VkShaderModuleCreateInfo* info,const VkAllocationCallbacks*,VkShaderModule* out){assert(info->pCode[0]==0x07230203&&info->codeSize>20);return create("module",out);}
VKAPI_ATTR void VKAPI_CALL destroyModule(VkDevice,VkShaderModule id,const VkAllocationCallbacks*){destroy("module",id);}
VKAPI_ATTR VkResult VKAPI_CALL pipeline(VkDevice,VkPipelineCache,unsigned count,const VkComputePipelineCreateInfo* info,const VkAllocationCallbacks*,VkPipeline* out){
    assert(count==1&&info->stage.stage==VK_SHADER_STAGE_COMPUTE_BIT&&info->stage.pSpecializationInfo);
    auto& spec=*info->stage.pSpecializationInfo;assert(spec.mapEntryCount==1&&spec.dataSize==4);
    const auto* data=static_cast<const unsigned*>(spec.pData);assert(data[0]<2);
    assert(spec.pMapEntries[0].constantID==0&&spec.pMapEntries[0].offset==0&&spec.pMapEntries[0].size==4);
    auto result=create("pipeline",out);
    if(result!=VK_SUCCESS) { // Vulkan partial handle on failure must be released.
        *out=handle<VkPipeline>(next++);objects[number(*out)]="pipeline";
    } else {++pipelineCreations;specs.insert(data[0]);}
    return result;
}
VKAPI_ATTR void VKAPI_CALL destroyPipeline(VkDevice,VkPipeline id,const VkAllocationCallbacks*){destroy("pipeline",id);}
VKAPI_ATTR VkResult VKAPI_CALL pool(VkDevice,const VkDescriptorPoolCreateInfo* info,const VkAllocationCallbacks*,VkDescriptorPool* out){assert(info->maxSets==1&&info->poolSizeCount==1&&info->pPoolSizes->type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER&&info->pPoolSizes->descriptorCount==2);return create("pool",out);}
VKAPI_ATTR void VKAPI_CALL destroyPool(VkDevice,VkDescriptorPool id,const VkAllocationCallbacks*){
    for(auto it=sets.begin();it!=sets.end();)if(it->second==number(id)){updated.erase(it->first);it=sets.erase(it);}else ++it;
    destroy("pool",id);
}
VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice,const VkDescriptorSetAllocateInfo* info,VkDescriptorSet* out){assert(info->descriptorSetCount==1&&objects.at(number(info->descriptorPool))=="pool");if(++step==fail)return VK_ERROR_OUT_OF_DEVICE_MEMORY;*out=handle<VkDescriptorSet>(next++);sets[number(*out)]=number(info->descriptorPool);return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL update(VkDevice,unsigned count,const VkWriteDescriptorSet* writes,unsigned,const VkCopyDescriptorSet*){assert(count==2&&writes[0].dstSet==writes[1].dstSet&&updated.insert(number(writes[0].dstSet)).second);for(unsigned i=0;i<2;++i)assert(writes[i].dstBinding==i&&writes[i].descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER&&writes[i].pBufferInfo->offset==256&&writes[i].pBufferInfo->range>0);}
VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer,VkPipelineStageFlags source,VkPipelineStageFlags dest,VkDependencyFlags,unsigned,const VkMemoryBarrier*,unsigned count,const VkBufferMemoryBarrier* value,unsigned,const VkImageMemoryBarrier*){assert(count==1&&value->offset==256&&value->size!=VK_WHOLE_SIZE&&value->srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED&&value->dstQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED);barriers.push_back(*value);stages.push_back({source,dest});++commands;}
VKAPI_ATTR void VKAPI_CALL bind(VkCommandBuffer,VkPipelineBindPoint point,VkPipeline id){assert(point==VK_PIPELINE_BIND_POINT_COMPUTE&&objects.at(number(id))=="pipeline");++commands;}
VKAPI_ATTR void VKAPI_CALL bindSet(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,unsigned first,unsigned count,const VkDescriptorSet* ids,unsigned dynamic,const unsigned*){assert(first==0&&count==1&&dynamic==0&&updated.contains(number(*ids)));++commands;}
VKAPI_ATTR void VKAPI_CALL push(VkCommandBuffer,VkPipelineLayout,VkShaderStageFlags stage,unsigned offset,unsigned bytes,const void* data){assert(stage==VK_SHADER_STAGE_COMPUTE_BIT&&offset==0&&bytes==20);std::memcpy(lastPush.data(),data,20);++commands;}
VKAPI_ATTR void VKAPI_CALL dispatch(VkCommandBuffer,unsigned x,unsigned y,unsigned z){assert(x==(lastPush[0]+7)/8&&y==(lastPush[1]+7)/8&&z==2);++commands;++dispatches;}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice,const char* raw){const std::string_view name=raw;if(name==missing)return nullptr;
#define ENTRY(n,f) if(name==n)return reinterpret_cast<PFN_vkVoidFunction>(f)
    ENTRY("vkCreateDescriptorSetLayout",descriptorLayout);ENTRY("vkDestroyDescriptorSetLayout",destroyDescriptors);
    ENTRY("vkCreatePipelineLayout",layout);ENTRY("vkDestroyPipelineLayout",destroyLayout);
    ENTRY("vkCreateShaderModule",module);ENTRY("vkDestroyShaderModule",destroyModule);
    ENTRY("vkCreateComputePipelines",pipeline);ENTRY("vkDestroyPipeline",destroyPipeline);
    ENTRY("vkCreateDescriptorPool",pool);ENTRY("vkDestroyDescriptorPool",destroyPool);
    ENTRY("vkAllocateDescriptorSets",allocate);ENTRY("vkUpdateDescriptorSets",update);
    ENTRY("vkCmdPipelineBarrier",barrier);ENTRY("vkCmdBindPipeline",bind);ENTRY("vkCmdBindDescriptorSets",bindSet);
    ENTRY("vkCmdPushConstants",push);ENTRY("vkCmdDispatch",dispatch);
#undef ENTRY
    return nullptr;
}
Context context(){Context c{};c.device=handle<VkDevice>(1);c.deviceProc=resolve;
    c.sampleLocations=c.multisampleArrayImage=true;
    c.limits.maxStorageBufferRange=UINT32_MAX;c.limits.minStorageBufferOffsetAlignment=256;
    c.limits.maxPushConstantsSize=128;c.limits.maxBoundDescriptorSets=4;c.limits.maxPerStageDescriptorStorageBuffers=8;c.limits.maxDescriptorSetStorageBuffers=8;c.limits.maxPerStageResources=8;
    c.limits.maxComputeWorkGroupSize[0]=c.limits.maxComputeWorkGroupSize[1]=1024;c.limits.maxComputeWorkGroupSize[2]=64;c.limits.maxComputeWorkGroupInvocations=1024;
    for(auto& n:c.limits.maxComputeWorkGroupCount)n=65535;return c;}
DepthTarget depth(){DepthTarget d{};d.stencilAddress=0x10000;d.extent={17,19};d.format=VK_FORMAT_D32_SFLOAT_S8_UINT;d.samples=8;return d;}
template<class F> void rejects(F test){auto before=commands;bool rejected=false;try{test();}catch(const std::runtime_error&){rejected=true;}assert(rejected&&commands==before);}
}
int main(){
    auto c=context();unsigned rejected=0;
    assert(GpuStencilSampleTiler::Supports(c,depth()));
    auto d16=depth();d16.format=VK_FORMAT_D16_UNORM_S8_UINT;assert(GpuStencilSampleTiler::Supports(c,d16));
    for(unsigned variation=0;variation<28;++variation){auto d=depth();auto ctx=c;DepthLayoutParameters options{.samples=8,.fragments=8};
        switch(variation){
        case 0:d.format=VK_FORMAT_D32_SFLOAT;break;case 1:d.samples=1;break;case 2:d.samples=2;break;case 3:d.samples=4;break;
        case 4:d.stencilAddress=0;break;case 5:++d.stencilAddress;break;case 6:d.stencilAddress=UINT64_MAX-65535;break;
        case 7:d.extent.width=0;break;case 8:d.extent.height=0;break;case 9:d.extent.width=16385;break;
        case 10:options.samples=4;break;case 11:options.fragments=4;break;case 12:options.swizzleMode=16;break;
        case 13:options.compressionEnabled=true;break;case 14:options.pipeBankXor=1;break;case 15:options.pitchInElements=17;break;
        case 16:ctx.device=VK_NULL_HANDLE;break;case 17:ctx.sampleLocations=false;break;case 18:ctx.multisampleArrayImage=false;break;
        case 19:ctx.limits.maxStorageBufferRange=65535;break;case 20:ctx.limits.maxDescriptorSetStorageBuffers=1;break;
        case 21:ctx.limits.maxPerStageDescriptorStorageBuffers=1;break;case 22:ctx.limits.maxPushConstantsSize=19;break;
        case 23:ctx.limits.maxComputeWorkGroupCount[2]=1;break;case 24:ctx.limits.maxComputeWorkGroupInvocations=63;break;
        case 25:ctx.limits.maxComputeWorkGroupSize[0]=7;break;case 26:ctx.limits.maxComputeWorkGroupCount[0]=2;break;
        case 27:ctx.limits.maxBoundDescriptorSets=0;break;}
        assert(!GpuStencilSampleTiler::Supports(ctx,d,options));++rejected;
    }
    for(unsigned failure=1;failure<=3;++failure){step=0;fail=failure;rejects([&]{GpuStencilSampleTiler owner(c);});assert(objects.empty());}fail=0;
    {
        GpuStencilSampleTiler owner(c);assert(objects.size()==3&&pipelineCreations==0);
        for(unsigned repeat=0;repeat<3;++repeat){
            GpuStencilSampleTiler::Transfer transfer(owner,depth());
            StencilSampleTilingBufferAccess tiled{c.device,handle<VkBuffer>(3),256,65536,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT};
            StencilSampleTilingBufferAccess linear{c.device,handle<VkBuffer>(4),256,17*19*8,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT};
            const auto commandBuffer=handle<VkCommandBuffer>(2);
            rejects([&]{auto bad=tiled;bad.device=handle<VkDevice>(99);transfer.Record(commandBuffer,false,bad,linear,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);});
            rejects([&]{auto bad=tiled;bad.bytes=1;transfer.Record(commandBuffer,false,bad,linear,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);});
            rejects([&]{auto bad=tiled;bad.offset=UINT64_MAX-255;transfer.Record(commandBuffer,false,bad,linear,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);});
            rejects([&]{auto bad=linear;bad.buffer=tiled.buffer;transfer.Record(commandBuffer,false,tiled,bad,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);});
            missing="vkCmdDispatch";rejects([&]{transfer.Record(commandBuffer,false,tiled,linear,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);});missing.clear();
            auto start=barriers.size();transfer.Record(commandBuffer,false,tiled,linear,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
            assert(barriers.size()==start+3&&barriers[start].srcAccessMask==VK_ACCESS_HOST_WRITE_BIT&&barriers[start].dstAccessMask==VK_ACCESS_SHADER_READ_BIT&&barriers[start+1].dstAccessMask==VK_ACCESS_SHADER_WRITE_BIT&&barriers[start+2].srcAccessMask==VK_ACCESS_SHADER_WRITE_BIT&&barriers[start+2].dstAccessMask==VK_ACCESS_SHADER_READ_BIT);
            assert(stages[start+2].first==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT&&stages[start+2].second==VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            assert(sets.size()==1);
            tiled.stage=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_HOST_BIT;tiled.access=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_HOST_WRITE_BIT;
            linear.stage=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;linear.access=VK_ACCESS_SHADER_WRITE_BIT;
            start=barriers.size();transfer.Record(commandBuffer,true,linear,tiled,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
            assert(sets.size()==2&&barriers[start+1].srcAccessMask==(VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_HOST_WRITE_BIT)&&stages[start+1].second==VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT&&barriers[start+2].dstAccessMask==VK_ACCESS_HOST_READ_BIT&&stages[start+2].second==VK_PIPELINE_STAGE_HOST_BIT);
            for(unsigned n=0;n<3;++n)transfer.Record(commandBuffer,true,linear,tiled,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
            assert(sets.size()==5);
        }
        assert(sets.empty()&&pipelineCreations==2&&specs.size()==2&&objects.size()==5);
        GpuStencilSampleTiler::Transfer transfer(owner,depth());
        StencilSampleTilingBufferAccess tiled{c.device,handle<VkBuffer>(3),256,65536,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT};
        StencilSampleTilingBufferAccess linear{c.device,handle<VkBuffer>(4),256,17*19*8,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT};
        for(unsigned f=1;f<=2;++f){step=0;fail=f;rejects([&]{transfer.Record(handle<VkCommandBuffer>(2),false,tiled,linear,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);});assert(objects.size()==5&&sets.empty());}fail=0;
    }
    assert(objects.empty()&&sets.empty());
    {GpuStencilSampleTiler owner(c);GpuStencilSampleTiler::Transfer transfer(owner,depth());step=0;fail=1;
        rejects([&]{transfer.Record(handle<VkCommandBuffer>(2),false,{c.device,handle<VkBuffer>(3),256,65536,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT},{c.device,handle<VkBuffer>(4),256,17*19*8,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT},VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);});assert(objects.size()==3);fail=0;
    }
    assert(objects.empty()&&sets.empty());
    std::printf("GPU S8/8 tiling production contract PASS: two cached programs, distinct retained sets, exact barriers, %u metadata fallbacks, atomic failure cleanup\n",rejected);
}
