// Original production-helper GPU test. Golden bytes/offsets come from the
// separately linked AMD AddrLib, never production ColorTargetLayout.
#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include <addrinterface.h>
#include "prx/libSceAgcDriver/Graphics/include/ColorSampleSwizzleEquations.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GpuColorSampleTiler.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ColorSampleTransfer.hpp"
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
static unsigned pipelineCreations=0, poolsCreated=0, poolsDestroyed=0;
static VKAPI_ATTR VkResult VKAPI_CALL trackedPipelines(VkDevice device,VkPipelineCache cache,unsigned count,const VkComputePipelineCreateInfo* info,const VkAllocationCallbacks* allocator,VkPipeline* output){
    auto result=vkCreateComputePipelines(device,cache,count,info,allocator,output);if(result==VK_SUCCESS)pipelineCreations+=count;return result;
}
static VKAPI_ATTR VkResult VKAPI_CALL trackedPool(VkDevice device,const VkDescriptorPoolCreateInfo* info,const VkAllocationCallbacks* allocator,VkDescriptorPool* output){
    auto result=vkCreateDescriptorPool(device,info,allocator,output);if(result==VK_SUCCESS)++poolsCreated;return result;
}
static VKAPI_ATTR void VKAPI_CALL trackedDestroyPool(VkDevice device,VkDescriptorPool pool,const VkAllocationCallbacks* allocator){++poolsDestroyed;vkDestroyDescriptorPool(device,pool,allocator);}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL trackedResolve(VkDevice device,const char* name){
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
    AgcDriver::Graphics::Context context{};
    std::unique_ptr<AgcDriver::Graphics::GpuColorSampleTiler> tiler;
    std::vector<std::unique_ptr<AgcDriver::Graphics::GpuColorSampleTiler::Transfer>> retained;
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
            bool portability=false; for(const auto& extension:extensions) portability|=std::strcmp(extension.extensionName,"VK_KHR_portability_subset")==0;
            VkPhysicalDevicePortabilitySubsetFeaturesKHR portableFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR};
            if(portability){VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&portableFeatures;vkGetPhysicalDeviceFeatures2(physical,&features);require(portableFeatures.multisampleArrayImage,"no multisample arrays");portableFeatures={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR};portableFeatures.multisampleArrayImage=VK_TRUE;}
            const float priority=1; VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            queueInfo.queueFamilyIndex=family;queueInfo.queueCount=1;queueInfo.pQueuePriorities=&priority;
            VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; deviceInfo.queueCreateInfoCount=1;deviceInfo.pQueueCreateInfos=&queueInfo;
            const char* portable="VK_KHR_portability_subset";
            if(portability) {deviceInfo.pNext=&portableFeatures;deviceInfo.enabledExtensionCount=1;deviceInfo.ppEnabledExtensionNames=&portable;}
            check(vkCreateDevice(physical,&deviceInfo,nullptr,&device),"vkCreateDevice"); vkGetDeviceQueue(device,family,0,&queue);
            const auto& limits=properties.limits;
            require(limits.maxPushConstantsSize>=sizeof(Push) && limits.maxComputeWorkGroupSize[0]>=8 && limits.maxComputeWorkGroupSize[1]>=8 && limits.maxComputeWorkGroupInvocations>=64 && limits.maxComputeWorkGroupCount[2]>=8,"compute limits");
            VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; poolInfo.queueFamilyIndex=family;poolInfo.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            check(vkCreateCommandPool(device,&poolInfo,nullptr,&pool),"vkCreateCommandPool");
            VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocation.commandPool=pool;allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocation.commandBufferCount=1;
            check(vkAllocateCommandBuffers(device,&allocation,&commands),"vkAllocateCommandBuffers");
            VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(device,&fenceInfo,nullptr,&fence),"vkCreateFence");
            context.device=device;context.physical=physical;context.queue=queue;context.pool=pool;
            context.deviceProc=trackedResolve;context.formatProperties=vkGetPhysicalDeviceFormatProperties;context.imageFormatProperties=vkGetPhysicalDeviceImageFormatProperties;context.multisampleArrayImage=true;context.limits=properties.limits;context.memory=memory;
            tiler=std::make_unique<AgcDriver::Graphics::GpuColorSampleTiler>(context);
            std::printf("[sample-tiling] device=%s api=%u.%u.%u driver=%u max_storage_range=%u production_helper=1\n",properties.deviceName,VK_VERSION_MAJOR(properties.apiVersion),VK_VERSION_MINOR(properties.apiVersion),VK_VERSION_PATCH(properties.apiVersion),properties.driverVersion,limits.maxStorageBufferRange);
        } catch(...) {cleanup();throw;}
    }
    ~Device(){cleanup();}
    void cleanup(){if(device){vkDeviceWaitIdle(device);retained.clear();tiler.reset();if(fence)vkDestroyFence(device,fence,nullptr);if(pool)vkDestroyCommandPool(device,pool,nullptr);vkDestroyDevice(device,nullptr);device={};}if(instance){vkDestroyInstance(instance,nullptr);instance={};}}
    std::uint32_t memoryType(std::uint32_t bits)const {for(std::uint32_t i=0;i<memory.memoryTypeCount;++i)if((bits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))return i;throw std::runtime_error("no host coherent memory");}
    void begin(){retained.clear();check(vkResetCommandBuffer(commands,0),"vkResetCommandBuffer");VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};info.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(vkBeginCommandBuffer(commands,&info),"vkBeginCommandBuffer");}
    void submit(){check(vkEndCommandBuffer(commands),"vkEndCommandBuffer");check(vkResetFences(device,1,&fence),"vkResetFences");VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};info.commandBufferCount=1;info.pCommandBuffers=&commands;check(vkQueueSubmit(queue,1,&info,fence),"vkQueueSubmit");const auto result=vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull);if(result!=VK_SUCCESS)vkDeviceWaitIdle(device);check(result,"vkWaitForFences");}
};
struct Buffer {
    Device& owner;VkBuffer buffer{};VkDeviceMemory memory{};void* mapped{};VkDeviceSize offset{},bytes{},allocationBytes{};
    Buffer(Device& owner,VkDeviceSize bytes):owner(owner),bytes(bytes){
        require(bytes && bytes<=owner.properties.limits.maxStorageBufferRange && bytes<=UINT32_MAX,"buffer payload limits");
        offset=std::max<VkDeviceSize>(256,owner.properties.limits.minStorageBufferOffsetAlignment);allocationBytes=offset+bytes+offset;
        try{VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=allocationBytes;info.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
            check(vkCreateBuffer(owner.device,&info,nullptr,&buffer),"vkCreateBuffer");VkMemoryRequirements requirements{};vkGetBufferMemoryRequirements(owner.device,buffer,&requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=owner.memoryType(requirements.memoryTypeBits);
            check(vkAllocateMemory(owner.device,&allocation,nullptr,&memory),"vkAllocateMemory");check(vkBindBufferMemory(owner.device,buffer,memory,0),"vkBindBufferMemory");check(vkMapMemory(owner.device,memory,0,VK_WHOLE_SIZE,0,&mapped),"vkMapMemory");std::memset(mapped,0xb6,allocationBytes);
        }catch(...){cleanup();throw;}
    }
    ~Buffer(){cleanup();}
    void cleanup(){if(mapped)vkUnmapMemory(owner.device,memory);if(buffer)vkDestroyBuffer(owner.device,buffer,nullptr);if(memory)vkFreeMemory(owner.device,memory,nullptr);mapped={};buffer={};memory={};}
    std::byte* data(){return static_cast<std::byte*>(mapped)+offset;}
    std::uint64_t guardErrors()const{std::uint64_t result=0;const auto* ptr=static_cast<const unsigned char*>(mapped);for(VkDeviceSize i=0;i<offset;++i)result+=ptr[i]!=0xb6;for(VkDeviceSize i=offset+bytes;i<allocationBytes;++i)result+=ptr[i]!=0xb6;return result;}
};
void barrier(Device& device,const Buffer& buffer,VkPipelineStageFlags beforeStage,VkAccessFlags beforeAccess,VkPipelineStageFlags afterStage,VkAccessFlags afterAccess){VkBufferMemoryBarrier value{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};value.srcAccessMask=beforeAccess;value.dstAccessMask=afterAccess;value.srcQueueFamilyIndex=value.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;value.buffer=buffer.buffer;value.offset=buffer.offset;value.size=buffer.bytes;vkCmdPipelineBarrier(device.commands,beforeStage,afterStage,0,0,nullptr,1,&value,0,nullptr);}
void dispatch(Device& device,const Buffer& source,const Buffer& destination,const Push& push,std::uint32_t samples,bool retile,
              VkPipelineStageFlags sourceStage=VK_PIPELINE_STAGE_HOST_BIT,VkAccessFlags sourceAccess=VK_ACCESS_HOST_WRITE_BIT,
              VkPipelineStageFlags destinationStage=VK_PIPELINE_STAGE_HOST_BIT,VkAccessFlags destinationAccess=VK_ACCESS_HOST_WRITE_BIT){
    using namespace AgcDriver::Graphics;
    ColorTarget color{};color.address=0x10000;color.extent={push.width,push.height};color.format=VK_FORMAT_R8G8B8A8_UNORM;
    color.bytes=4ull*push.tiledWords;color.tileMode=ColorTileMode::RenderTarget;color.samples=color.fragments=samples;
    require(GpuColorSampleTiler::Supports(device.context,color),"production helper rejected AMD-qualified layout");
    auto transfer=std::make_unique<GpuColorSampleTiler::Transfer>(*device.tiler,color);
    transfer->Record(device.commands,retile,{device.device,source.buffer,source.offset,source.bytes,sourceStage,sourceAccess},
        {device.device,destination.buffer,destination.offset,destination.bytes,destinationStage,destinationAccess},VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
    device.retained.push_back(std::move(transfer));
}
std::uint32_t pattern(unsigned x,unsigned y,unsigned sample){return((x*29u+y*131u+sample*17u)&255u)|(((x*73u^y*149u^sample*37u)&255u)<<8)|(((x*53u+y*31u+sample*111u)&255u)<<16)|((255u-((x*31u+y*17u+sample*13u)&255u))<<24);}
std::uint64_t compareBytes(const std::byte* actual,const std::vector<std::byte>& expected){std::uint64_t errors=0;for(std::size_t i=0;i<expected.size();++i)errors+=actual[i]!=expected[i];return errors;}

int main(int argc,char** argv){
    static_cast<void>(argc);static_cast<void>(argv);
    try{Oracle oracle;Device device;std::uint64_t words=0,addresses=0,padding=0,errors=0;unsigned cases=0;
        for(const auto samples:{2u,4u,8u})for(const auto extent:{std::array{1u,1u},std::array{17u,19u},std::array{31u,63u},std::array{33u,65u},std::array{257u,129u},std::array{2049u,19u},std::array{4097u,19u},std::array{19u,4097u},std::array{8193u,19u},std::array{19u,8193u},std::array{1920u,1080u}}){
            const auto width=extent[0],height=extent[1];ADDR2_COMPUTE_SURFACE_INFO_INPUT info{};info.size=sizeof(info);info.flags.color=1;info.swizzleMode=ADDR_SW_64KB_R_X;info.resourceType=ADDR_RSRC_TEX_2D;info.bpp=32;info.width=width;info.height=height;info.numSlices=info.numMipLevels=1;info.numSamples=info.numFrags=samples;
            ADDR2_COMPUTE_SURFACE_INFO_OUTPUT geometry{};geometry.size=sizeof(geometry);require(Addr2ComputeSurfaceInfo(oracle.library,&info,&geometry)==ADDR_OK,"AMD surface geometry");
            const auto* equation=AgcDriver::Graphics::FindColorSampleSwizzleEquation(4,samples);require(geometry.blockWidth==equation->blockWidth && geometry.blockHeight==equation->blockHeight && geometry.baseAlign==65536,"AMD block geometry mismatch");
            const auto linearBytes=std::uint64_t(width)*height*samples*4;require(linearBytes<=UINT32_MAX && geometry.surfSize<=UINT32_MAX,"32bit byte-address limits");Push push{width,height,geometry.pitch/geometry.blockWidth,static_cast<std::uint32_t>(geometry.surfSize/4),static_cast<std::uint32_t>(linearBytes/4)};
            std::vector<std::byte> expectedLinear(linearBytes),expectedTiled(geometry.surfSize,std::byte{0xa5}),expectedRetiled(geometry.surfSize,std::byte{0x5a}),expectedChain(geometry.surfSize,std::byte{0xfe});std::vector<bool> visited(geometry.surfSize/4);
            ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_INPUT coord{};coord.size=sizeof(coord);coord.flags=info.flags;coord.swizzleMode=info.swizzleMode;coord.resourceType=info.resourceType;coord.bpp=32;coord.unalignedWidth=width;coord.unalignedHeight=height;coord.numSlices=coord.numMipLevels=1;coord.numSamples=coord.numFrags=samples;
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)for(unsigned sample=0;sample<samples;++sample){coord.x=x;coord.y=y;coord.sample=sample;ADDR2_COMPUTE_SURFACE_ADDRFROMCOORD_OUTPUT output{};output.size=sizeof(output);require(Addr2ComputeSurfaceAddrFromCoord(oracle.library,&coord,&output)==ADDR_OK,"AMD coordinate");require(output.bitPosition==0 && output.addr%4==0 && output.addr+4<=geometry.surfSize && !visited[output.addr/4],"AMD coordinate range/bijection");visited[output.addr/4]=true;++addresses;const auto linearOffset=((std::uint64_t(y)*width+x)*samples+sample)*4;const auto value=pattern(x,y,sample),changed=value^0x58fb2973u;std::memcpy(expectedLinear.data()+linearOffset,&value,4);std::memcpy(expectedTiled.data()+output.addr,&value,4);std::memcpy(expectedRetiled.data()+output.addr,&changed,4);std::memcpy(expectedChain.data()+output.addr,&value,4);}
            Buffer tiled(device,geometry.surfSize),linear(device,linearBytes),retiled(device,geometry.surfSize);std::memcpy(tiled.data(),expectedTiled.data(),expectedTiled.size());std::memset(linear.data(),0xca,linearBytes);std::memset(retiled.data(),0x5a,geometry.surfSize);
            device.begin();dispatch(device,tiled,linear,push,samples,false);device.submit();
            auto caseErrors=compareBytes(linear.data(),expectedLinear)+compareBytes(tiled.data(),expectedTiled)+tiled.guardErrors()+linear.guardErrors();
            for(std::size_t i=0;i<expectedLinear.size();i+=4){std::uint32_t word;std::memcpy(&word,expectedLinear.data()+i,4);word^=0x58fb2973u;std::memcpy(linear.data()+i,&word,4);}
            device.begin();dispatch(device,linear,retiled,push,samples,true);device.submit();
            caseErrors+=compareBytes(retiled.data(),expectedRetiled)+retiled.guardErrors()+linear.guardErrors();
            // A chained GPU transfer with an intentional original-tiled overwrite:
            // neither missing Detile nor missing Retile can pass by seeing input.
            std::memset(linear.data(),0xca,linearBytes);device.begin();dispatch(device,tiled,linear,push,samples,false);
            barrier(device,tiled,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);vkCmdFillBuffer(device.commands,tiled.buffer,tiled.offset,tiled.bytes,0xfefefefeu);dispatch(device,linear,tiled,push,samples,true,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);barrier(device,linear,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);device.submit();
            caseErrors+=compareBytes(tiled.data(),expectedChain)+compareBytes(linear.data(),expectedLinear)+tiled.guardErrors()+linear.guardErrors();
            if(width==17&&height==19){
                using namespace AgcDriver::Graphics;
                ColorTarget color{};color.address=0x10000;color.extent={width,height};color.format=VK_FORMAT_R8G8B8A8_UNORM;
                color.bytes=geometry.surfSize;color.tileMode=ColorTileMode::RenderTarget;color.samples=color.fragments=samples;
                RenderTarget image(device.context,color,false);ColorSampleTransfer imageTransfer(device.context,image,color);
                GpuColorSampleTiler::Transfer tiling(*device.tiler,color);
                std::memcpy(tiled.data(),expectedTiled.data(),expectedTiled.size());std::memset(linear.data(),0xca,linearBytes);
                auto expectedImage=expectedRetiled;
                for(std::size_t i=0;i<visited.size();++i)if(visited[i])std::memcpy(expectedImage.data()+4*i,expectedTiled.data()+4*i,4);
                device.begin();
                tiling.Record(device.commands,false,
                    {device.device,tiled.buffer,tiled.offset,tiled.bytes,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT},
                    {device.device,linear.buffer,linear.offset,linear.bytes,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_WRITE_BIT},
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
                const ColorSampleImageAccess attachment{VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
                imageTransfer.RecordUpload(device.commands,{linear.buffer,linear.offset,linear.bytes,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT},
                    {VK_IMAGE_LAYOUT_UNDEFINED,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,0},attachment);
                // Poison the copied packed source and seed destination padding after
                // upload. Missing image readback or retile cannot accidentally pass.
                barrier(device,linear,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
                vkCmdFillBuffer(device.commands,linear.buffer,linear.offset,linear.bytes,0xfefefefeu);
                barrier(device,tiled,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
                vkCmdFillBuffer(device.commands,tiled.buffer,tiled.offset,tiled.bytes,0x5a5a5a5au);
                imageTransfer.RecordReadback(device.commands,{linear.buffer,linear.offset,linear.bytes,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT},attachment,
                    {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT},VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_HOST_READ_BIT);
                tiling.Record(device.commands,true,{device.device,linear.buffer,linear.offset,linear.bytes,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT},
                    {device.device,tiled.buffer,tiled.offset,tiled.bytes,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT},VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_HOST_READ_BIT);
                device.submit();
                auto imageErrors=compareBytes(tiled.data(),expectedImage)+compareBytes(linear.data(),expectedLinear)+tiled.guardErrors()+linear.guardErrors();caseErrors+=imageErrors;
                std::printf("[sample-tiling-image] production_detile_upload_readback_retile samples=%u logical_words=%llu poisoned_intermediate=1 padding_seed=5a errors=%llu\n",samples,static_cast<unsigned long long>(linearBytes/4),static_cast<unsigned long long>(imageErrors));
            }
            errors+=caseErrors;words+=linearBytes/4*3;padding+=(geometry.surfSize-linearBytes)*2;cases+=3;
            std::printf("[sample-tiling] extent=%ux%u samples=%u tiled_bytes=%llu logical_words=%llu detile_retile_chain_cases=3 padding_bytes=%llu errors=%llu\n",width,height,samples,static_cast<unsigned long long>(geometry.surfSize),static_cast<unsigned long long>(linearBytes/4),static_cast<unsigned long long>(geometry.surfSize-linearBytes),static_cast<unsigned long long>(caseErrors));
        }
        require(pipelineCreations==9,"six tiling programs were not cached across surfaces (plus three image readback programs)");
        device.retained.clear();require(poolsCreated==poolsDestroyed,"descriptor pools outlive their completed transfer owners");
        std::printf("[sample-tiling] cached_tiling_programs=6 additional_image_programs=3 descriptor_pools_created=%u descriptor_pools_destroyed=%u image_chain_cases=3\n",poolsCreated,poolsDestroyed);
        std::printf("[sample-tiling] cases=%u AMD_coordinates=%llu logical_words_checked=%llu padding_bytes_checked=%llu errors=%llu status=%s\n",cases,static_cast<unsigned long long>(addresses),static_cast<unsigned long long>(words),static_cast<unsigned long long>(padding),static_cast<unsigned long long>(errors),errors?"FAIL":"PASS");return errors?1:0;
    }catch(const std::exception& error){std::fprintf(stderr,"[sample-tiling] FAIL %s\n",error.what());return 1;}
}
