// Original admission/recording adapter; no GPU, Draw, or memory-tracking claim.
#include "prx/libSceAgcDriver/Graphics/include/StencilZeroInvariant.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Shaders.hpp"
#include <spirv/unified1/spirv.hpp>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>
using namespace AgcDriver::Graphics;
using namespace ShaderRecompiler;
namespace {
template<class T> T handle(std::uintptr_t v) { if constexpr(std::is_pointer_v<T>) return reinterpret_cast<T>(v); else return static_cast<T>(v); }
unsigned rejects=0, commands=0, barriers=0, clears=0, lookups=0;
VkFormatFeatureFlags features=VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
VkSampleCountFlags sampleCounts=VK_SAMPLE_COUNT_4_BIT;
unsigned layers=2;
VkResult queryResult=VK_SUCCESS;
std::string_view missing;
const std::array<VkSampleLocationEXT,8> positions{{{0,0},{.125f,.125f},{.25f,.25f},{.375f,.375f},{.5f,.5f},{.625f,.625f},{.75f,.75f},{.875f,.875f}}};
VKAPI_ATTR void VKAPI_CALL format(VkPhysicalDevice,VkFormat,VkFormatProperties* out) { *out={0,features,0}; }
VKAPI_ATTR VkResult VKAPI_CALL imageFormat(VkPhysicalDevice,VkFormat,VkImageType type,VkImageTiling tiling,VkImageUsageFlags usage,VkImageCreateFlags flags,VkImageFormatProperties* out) {
    assert(type==VK_IMAGE_TYPE_2D && tiling==VK_IMAGE_TILING_OPTIMAL);
    assert(usage==(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT));
    assert(flags==VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT);
    *out={{4096,4096,1},1,layers,sampleCounts,1ull<<30};return queryResult;
}
VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer,VkPipelineStageFlags source,VkPipelineStageFlags destination,VkDependencyFlags dep,unsigned mc,const VkMemoryBarrier*,unsigned bc,const VkBufferMemoryBarrier*,unsigned ic,const VkImageMemoryBarrier* value) {
    assert(dep==0 && mc==0 && bc==0 && ic==1);
    assert(value->image==handle<VkImage>(123));
    assert(value->oldLayout==VK_IMAGE_LAYOUT_GENERAL && value->newLayout==VK_IMAGE_LAYOUT_GENERAL);
    assert(value->srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED && value->dstQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED);
    assert(value->subresourceRange.aspectMask==(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT));
    assert(value->subresourceRange.baseMipLevel==0 && value->subresourceRange.levelCount==1 && value->subresourceRange.layerCount==1);
    const auto group=value->subresourceRange.baseArrayLayer;
    assert(group==barriers%2);
    const auto* pattern=static_cast<const VkSampleLocationsInfoEXT*>(value->pNext);
    assert(pattern && pattern->sType==VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT && pattern->sampleLocationsPerPixel==VK_SAMPLE_COUNT_4_BIT);
    assert(pattern->sampleLocationGridSize.width==1 && pattern->sampleLocationGridSize.height==1 && pattern->sampleLocationsCount==4);
    for(unsigned i=0;i<4;++i) assert(pattern->pSampleLocations[i].x==positions[group*4+i].x && pattern->pSampleLocations[i].y==positions[group*4+i].y);
    constexpr auto tests=VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    constexpr auto access=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    if(barriers<2) {assert(source==VK_PIPELINE_STAGE_ALL_COMMANDS_BIT && destination==VK_PIPELINE_STAGE_TRANSFER_BIT);assert(value->srcAccessMask==(VK_ACCESS_TRANSFER_WRITE_BIT|access|VK_ACCESS_SHADER_READ_BIT) && value->dstAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT);}
    else if(barriers<4) {assert(source==VK_PIPELINE_STAGE_TRANSFER_BIT && destination==tests);assert(value->srcAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT && value->dstAccessMask==access);}
    else {assert(source==tests && destination==VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);assert(value->srcAccessMask==access && value->dstAccessMask==(access|VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT));}
    ++commands;++barriers;
}
VKAPI_ATTR void VKAPI_CALL clear(VkCommandBuffer,VkImage image,VkImageLayout layout,const VkClearDepthStencilValue* value,unsigned count,const VkImageSubresourceRange* range) {
    assert(barriers==2 && image==handle<VkImage>(123) && layout==VK_IMAGE_LAYOUT_GENERAL && value->stencil==0 && count==1);
    assert(range->aspectMask==VK_IMAGE_ASPECT_STENCIL_BIT && range->baseArrayLayer==0 && range->layerCount==2 && range->baseMipLevel==0 && range->levelCount==1);
    ++commands;++clears;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice,const char* name) {
    ++lookups; if(name==missing)return nullptr;
    if(std::string_view(name)=="vkCmdPipelineBarrier")return reinterpret_cast<PFN_vkVoidFunction>(barrier);
    if(std::string_view(name)=="vkCmdClearDepthStencilImage")return reinterpret_cast<PFN_vkVoidFunction>(clear);
    assert(false && "zero transfer tried to allocate a Vulkan object or program");return nullptr;
}
template<class F> void rejected(F f) {bool failed=false;try {f();}catch(const std::runtime_error&) {failed=true;}assert(failed);++rejects;}
Context context() {
    Context c{};c.device=handle<VkDevice>(1);c.physical=handle<VkPhysicalDevice>(1);c.deviceProc=resolve;c.formatProperties=format;c.imageFormatProperties=imageFormat;
    c.sampleLocations=true;c.multisampleArrayImage=true;
    c.sampleLocationProperties.sampleLocationSampleCounts=VK_SAMPLE_COUNT_4_BIT;c.sampleLocationProperties.sampleLocationSubPixelBits=4;
    c.sampleLocationProperties.maxSampleLocationGridSize={1,1};c.sampleLocationProperties.sampleLocationCoordinateRange[0]=0;c.sampleLocationProperties.sampleLocationCoordinateRange[1]=.9375f;
    c.limits.maxFramebufferWidth=c.limits.maxFramebufferHeight=4096;c.limits.maxViewportDimensions[0]=c.limits.maxViewportDimensions[1]=4096;
    c.limits.maxPushConstantsSize=128;c.limits.maxStorageBufferRange=1u<<26;
    c.limits.maxPerStageDescriptorStorageBuffers=c.limits.maxDescriptorSetStorageBuffers=8;
    c.limits.maxPerStageDescriptorSampledImages=c.limits.maxDescriptorSetSampledImages=8;
    c.limits.maxComputeWorkGroupSize[0]=1024;c.limits.maxComputeWorkGroupInvocations=1024;c.limits.maxComputeWorkGroupCount[0]=65535;return c;
}
State state() {
    State s{};s.depth=DepthTarget{1,2,{17,19},VK_FORMAT_D32_SFLOAT_S8_UINT,.625f,0,8};s.samples.count=8;s.samples.nativeCount=4;s.samples.groups=2;s.samples.mask=255;s.stencilTest=true;
    s.stencilFront=s.stencilBack={VK_STENCIL_OP_KEEP,VK_STENCIL_OP_ZERO,VK_STENCIL_OP_REPLACE,VK_COMPARE_OP_ALWAYS,255,255,0};return s;
}
std::vector<std::uint32_t> fragmentWords() {return {0x07230203,0x10000,0,20,0,(5u<<16)|spv::OpEntryPoint,spv::ExecutionModelFragment,1,0x6e69616d,0};}
void append(std::vector<std::uint32_t>& words,spv::Op op,std::initializer_list<std::uint32_t> operands) {words.push_back(((operands.size()+1)<<16)|op);words.insert(words.end(),operands);}
}
int main(int argc,char** argv) {
    if(argc==3 && std::string_view(argv[1])=="--env") {assert(ZeroStencilInvariantEnabled()==(std::string_view(argv[2])=="1"));return 0;}
    std::uint64_t scans=0;
    for(unsigned offset=0;offset<16;++offset) for(unsigned length=0;length<=257;++length) {
        std::vector<std::byte> storage(length+offset+16,std::byte{0xa7});std::fill(storage.begin()+offset,storage.begin()+offset+length,std::byte{0});
        auto span=std::span<const std::byte>(storage).subspan(offset,length);assert(AllStencilBytesZero(span));++scans;
        for(unsigned at=0;at<length;++at) {storage[offset+at]=std::byte{1};assert(!AllStencilBytesZero(span));storage[offset+at]=std::byte{0};++scans;}
        for(unsigned at=0;at<offset;++at)assert(storage[at]==std::byte{0xa7});for(unsigned at=offset+length;at<storage.size();++at)assert(storage[at]==std::byte{0xa7});
    }
    auto s=state();RecompileResult program{};program.spirv=fragmentWords();std::array<CompiledShader,1> shaders{{{ShaderStage::Fragment,&program,0}}};std::array<std::byte,257> bytes{};
    assert(PreservesZeroStencil(s,shaders,bytes,bytes.size()));
    unsigned safeFaces=0;
    for(auto a:{VK_STENCIL_OP_KEEP,VK_STENCIL_OP_ZERO,VK_STENCIL_OP_REPLACE})for(auto b:{VK_STENCIL_OP_KEEP,VK_STENCIL_OP_ZERO,VK_STENCIL_OP_REPLACE})for(auto c:{VK_STENCIL_OP_KEEP,VK_STENCIL_OP_ZERO,VK_STENCIL_OP_REPLACE}) {
        auto t=s;t.stencilFront.failOp=a;t.stencilFront.passOp=b;t.stencilFront.depthFailOp=c;t.stencilBack=t.stencilFront;assert(PreservesZeroStencil(t,shaders,bytes,bytes.size()));++safeFaces;
    }
    unsigned unsafeStates=0;
    for(unsigned variation=0;variation<28;++variation) {auto t=s;
        switch(variation) {case 0:t.depth.reset();break;case 1:t.stencilTest=false;break;case 2:t.depthTest=true;break;case 3:t.depthWrite=true;break;case 4:t.depthBoundsTest=true;break;case 5:t.depthBias=true;break;case 6:t.depthClamp=true;break;case 7:t.depth->clearStencil=1;break;case 8:t.depth->stencilAddress=0;break;case 9:t.samples.count=4;break;case 10:t.samples.nativeCount=8;break;case 11:t.samples.groups=1;break;case 12:t.samples.mask=127;break;case 13:t.depth->samples=4;break;case 14:t.depth->format=VK_FORMAT_D32_SFLOAT;break;case 15:t.stencilFront.reference=1;break;case 16:t.stencilBack.reference=1;break;case 17:t.stencilFront.writeMask=127;break;case 18:t.stencilBack.writeMask=127;break;case 19:t.stencilFront.compareOp=VK_COMPARE_OP_NEVER;break;case 20:t.stencilBack.compareOp=VK_COMPARE_OP_EQUAL;break;case 21:t.stencilFront.passOp=VK_STENCIL_OP_INCREMENT_AND_WRAP;break;case 22:t.stencilBack.depthFailOp=VK_STENCIL_OP_INVERT;break;case 23:t.stencilFront.failOp=VK_STENCIL_OP_DECREMENT_AND_CLAMP;break;case 24:t.stencilBack.failOp=VK_STENCIL_OP_INCREMENT_AND_CLAMP;break;}
        if(variation==25)t.depth->format=VK_FORMAT_D16_UNORM_S8_UINT;
        if(variation==26)t.stencilFront.compareMask=127;
        if(variation==27)t.stencilBack.compareMask=127;
        assert(!PreservesZeroStencil(t,shaders,bytes,bytes.size()));++unsafeStates;
    }
    assert(!PreservesZeroStencil(s,shaders,bytes,bytes.size()-1));assert(!PreservesZeroStencil(s,shaders,{},0));
    for(unsigned at=0;at<bytes.size();++at) {bytes[at]=std::byte{1};assert(!PreservesZeroStencil(s,shaders,bytes,bytes.size()));bytes[at]=std::byte{0};}
    unsigned shaderRejects=0;
    for(unsigned variation=0;variation<17;++variation) {auto words=fragmentWords();
        switch(variation) {case 0:append(words,spv::OpDecorate,{2,spv::DecorationBuiltIn,spv::BuiltInFragDepth});break;case 1:append(words,spv::OpDecorate,{2,spv::DecorationBuiltIn,spv::BuiltInFragStencilRefEXT});break;case 2:append(words,spv::OpMemberDecorate,{2,0,spv::DecorationBuiltIn,spv::BuiltInFragDepth});break;case 3:append(words,spv::OpMemberDecorate,{2,0,spv::DecorationBuiltIn,spv::BuiltInFragStencilRefEXT});break;case 4:append(words,spv::OpExecutionMode,{1,spv::ExecutionModeDepthReplacing});break;case 5:append(words,spv::OpExecutionMode,{1,spv::ExecutionModeStencilRefReplacingEXT});break;case 6:append(words,spv::OpDecorateId,{1,spv::DecorationBuiltIn,2});break;case 7:append(words,spv::OpExecutionModeId,{1,spv::ExecutionModeLocalSizeId,2,3,4});break;case 8:words[0]=0;break;case 9:words.resize(4);break;case 10:words.push_back(0);break;case 11:words.push_back((4u<<16)|spv::OpNop);break;case 12:words[6]=spv::ExecutionModelVertex;break;case 13:append(words,spv::OpEntryPoint,{spv::ExecutionModelFragment,1,0,0});break;case 14:append(words,spv::OpDecorate,{2,spv::DecorationBuiltIn});break;case 15:append(words,spv::OpMemberDecorate,{2,0,spv::DecorationBuiltIn});break;case 16:words.resize(5);break;}
        program.spirv=std::move(words);assert(!PreservesZeroStencil(s,shaders,bytes,bytes.size()));++shaderRejects;
    }
    program.spirv=fragmentWords();shaders[0].program=nullptr;assert(!PreservesZeroStencil(s,shaders,bytes,bytes.size()));shaders[0].program=&program;
    std::array<CompiledShader,2> duplicate{shaders[0],shaders[0]};assert(!PreservesZeroStencil(s,duplicate,bytes,bytes.size()));assert(!PreservesZeroStencil(s,{},bytes,bytes.size()));
    const auto image=handle<VkImage>(123);const std::array<VkImageView,2> views{handle<VkImageView>(1),handle<VkImageView>(2)};
    const auto construct=[&](const Context& c,VkFormat f=VK_FORMAT_D32_SFLOAT_S8_UINT,VkExtent2D extent=VkExtent2D{17,19},unsigned sample=8) {return StencilZeroTransfer(c,image,f,views,extent,sample,positions);};
    auto good=context();auto transfer=construct(good);assert(lookups==0 && commands==0);
    for(unsigned variation=0;variation<17;++variation) {auto bad=good;
        switch(variation) {case 0:bad.sampleLocations=false;break;case 1:bad.multisampleArrayImage=false;break;case 2:bad.sampleLocationProperties.sampleLocationSampleCounts=0;break;case 3:bad.sampleLocationProperties.maxSampleLocationGridSize.width=0;break;case 4:bad.sampleLocationProperties.sampleLocationSubPixelBits=3;break;case 5:bad.sampleLocationProperties.sampleLocationCoordinateRange[1]=.5f;break;case 6:bad.limits.maxFramebufferWidth=16;break;case 7:bad.limits.maxViewportDimensions[1]=18;break;case 8:bad.limits.maxStorageBufferRange=4;break;case 9:bad.limits.maxPushConstantsSize=19;break;case 10:bad.limits.maxPerStageDescriptorStorageBuffers=0;break;case 11:bad.limits.maxDescriptorSetSampledImages=0;break;case 12:bad.limits.maxComputeWorkGroupSize[0]=32;break;case 13:bad.limits.maxComputeWorkGroupInvocations=32;break;case 14:bad.limits.maxComputeWorkGroupCount[0]=0;break;case 15:bad.formatProperties=nullptr;break;case 16:bad.imageFormatProperties=nullptr;break;}
        rejected([&]{construct(bad);});assert(commands==0 && lookups==0);
    }
    rejected([&]{construct(good,VK_FORMAT_D32_SFLOAT);});rejected([&]{construct(good,VK_FORMAT_D16_UNORM_S8_UINT);});rejected([&]{construct(good,VK_FORMAT_D32_SFLOAT_S8_UINT,{0,19});});rejected([&]{construct(good,VK_FORMAT_D32_SFLOAT_S8_UINT,{17,19},4);});
    auto badPositions=positions;badPositions[7].x=std::numeric_limits<float>::quiet_NaN();rejected([&]{StencilZeroTransfer t(good,image,VK_FORMAT_D32_SFLOAT_S8_UINT,views,{17,19},8,badPositions);});
    rejected([&]{StencilZeroTransfer t(good,VK_NULL_HANDLE,VK_FORMAT_D32_SFLOAT_S8_UINT,views,{17,19},8,positions);});
    rejected([&]{StencilZeroTransfer t(good,image,VK_FORMAT_D32_SFLOAT_S8_UINT,std::span<const VkImageView>(views).first(1),{17,19},8,positions);});
    features=0;rejected([&]{construct(good);});features=VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    sampleCounts=VK_SAMPLE_COUNT_1_BIT;rejected([&]{construct(good);});sampleCounts=VK_SAMPLE_COUNT_4_BIT;
    layers=1;rejected([&]{construct(good);});layers=2;queryResult=VK_ERROR_FORMAT_NOT_SUPPORTED;rejected([&]{construct(good);});queryResult=VK_SUCCESS;
    for(auto name:{"vkCmdPipelineBarrier","vkCmdClearDepthStencilImage"}) {missing=name;rejected([&]{transfer.RecordClear(handle<VkCommandBuffer>(1));});assert(commands==0);}
    missing={};rejected([&]{transfer.RecordClear(VK_NULL_HANDLE);});assert(commands==0);
    transfer.RecordClear(handle<VkCommandBuffer>(1));transfer.RecordFinish(handle<VkCommandBuffer>(1));assert(commands==7 && barriers==6 && clears==1);
    std::printf("zero stencil contract: scans=%llu safe_face_tuples=%u unsafe_states=%u shader_rejects=%u metadata_or_recording_rejects=%u per_group_barriers=6 stencil_only_clear=1 created_objects=0 PASS\n",static_cast<unsigned long long>(scans),safeFaces,unsafeStates,shaderRejects,rejects);
}
