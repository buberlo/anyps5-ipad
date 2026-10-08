// Original ownership/recording contract executing the production snapshot owner.
// Mock Buffer/Vulkan/reader only; actual GPU and Windows tracking are separate.
#include "prx/libSceAgcDriver/Graphics/include/ColorSampleSnapshotCopy.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <type_traits>
using namespace AgcDriver::Graphics;
namespace {
template<class T>T handle(std::uintptr_t id){if constexpr(std::is_pointer_v<T>)return reinterpret_cast<T>(id);else return static_cast<T>(id);}
template<class T>std::uintptr_t number(T id){if constexpr(std::is_pointer_v<T>)return reinterpret_cast<std::uintptr_t>(id);else return id;}
struct Allocation{std::byte* bytes;std::size_t size;VkBufferUsageFlags usage;};
std::map<std::uintptr_t,Allocation> allocations;
std::vector<std::string> events;
std::vector<std::byte> guest;
std::vector<VkBufferMemoryBarrier> barriers;
std::vector<std::pair<VkPipelineStageFlags,VkPipelineStageFlags>> stages;
struct Copy{VkBuffer src,dst;VkBufferCopy range;};std::vector<Copy> pending;
std::uintptr_t next=10;unsigned constructions=0,destructions=0,failAt=0,reads=0,lookups=0,rejections=0;
std::string missing;bool failReader=false;std::uint64_t guestAddress=0x10000;std::size_t expectedAlign=65536;
void reader(std::uint64_t address,std::span<std::byte> destination,std::size_t alignment){
 ++reads;events.push_back("flush+check+read");assert(address==guestAddress&&alignment==expectedAlign&&destination.size()==guest.size());
 if(failReader)throw std::runtime_error("synthetic unreadable/unmapped read failure");
 // A pending producer changes guest bytes before the read, never after it.
 guest[13]=std::byte{0x91};std::memcpy(destination.data(),guest.data(),guest.size());
}
VKAPI_ATTR void VKAPI_CALL recordBarrier(VkCommandBuffer,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags d,unsigned mc,const VkMemoryBarrier*,unsigned bc,const VkBufferMemoryBarrier* b,unsigned ic,const VkImageMemoryBarrier*){
 assert(d==0&&mc==0&&bc==2&&ic==0);events.push_back("barrier");stages.push_back({src,dst});for(unsigned i=0;i<bc;++i)barriers.push_back(b[i]);
}
VKAPI_ATTR void VKAPI_CALL recordCopy(VkCommandBuffer,VkBuffer src,VkBuffer dst,unsigned count,const VkBufferCopy* copy){
 assert(count==1);events.push_back("copy-recorded");pending.push_back({src,dst,*copy});
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice,const char* name){++lookups;if(missing==name)return nullptr;
 if(std::strcmp(name,"vkCmdPipelineBarrier")==0)return reinterpret_cast<PFN_vkVoidFunction>(recordBarrier);
 if(std::strcmp(name,"vkCmdCopyBuffer")==0)return reinterpret_cast<PFN_vkVoidFunction>(recordCopy);
 return nullptr;
}
void fence(){events.push_back("fence");for(const auto& p:pending){const auto a=allocations.at(number(p.src)),b=allocations.at(number(p.dst));assert((a.usage&VK_BUFFER_USAGE_TRANSFER_SRC_BIT)&&(b.usage&VK_BUFFER_USAGE_TRANSFER_DST_BIT));assert(p.range.srcOffset==0&&p.range.dstOffset==0&&p.range.size==a.size&&a.size==b.size);std::memcpy(b.bytes,a.bytes,a.size);}pending.clear();}
// Mirrors CommandBatch ownership distinction: recorded/unsubmitted work is
// cancelled on release; submitted work must complete before owners disappear.
struct Batch { bool submitted=false;void Submit(){submitted=true;}~Batch(){if(submitted)fence();else {events.push_back("cancel-unsubmitted");pending.clear();}} };
Context context(){Context c{};c.device=handle<VkDevice>(1);c.deviceProc=resolve;c.limits.maxStorageBufferRange=0x8000000;c.limits.maxPushConstantsSize=128;c.limits.maxBoundDescriptorSets=1;c.limits.maxPerStageDescriptorStorageBuffers=2;c.limits.maxDescriptorSetStorageBuffers=2;c.limits.maxPerStageResources=2;c.limits.maxComputeWorkGroupSize[0]=c.limits.maxComputeWorkGroupSize[1]=8;c.limits.maxComputeWorkGroupSize[2]=1;c.limits.maxComputeWorkGroupInvocations=64;c.limits.maxComputeWorkGroupCount[0]=c.limits.maxComputeWorkGroupCount[1]=65535;c.limits.maxComputeWorkGroupCount[2]=8;return c;}
ColorTarget color(unsigned samples){ColorTarget c{};c.address=guestAddress;c.extent={17,19};c.format=VK_FORMAT_R8G8B8A8_UNORM;c.tileMode=ColorTileMode::RenderTarget;c.samples=c.fragments=samples;const ColorTargetLayout l(c.extent.width,c.extent.height,c.tileMode,4,samples);c.bytes=l.Bytes();return c;}
template<class F>void rejects(F run){auto commands=events.size();bool failed=false;try{run();}catch(const std::runtime_error&){failed=true;}assert(failed&&events.size()==commands);++rejections;}
}
namespace AgcDriver::Graphics{
Buffer::Buffer(const Context& c,std::size_t n,VkBufferUsageFlags u,VkMemoryPropertyFlags p):context(c),size(n),capacity(n),usage(u),properties(p){
 ++constructions;if(failAt&&constructions==failAt)throw std::runtime_error("buffer allocation failure");
 assert(p==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
 buffer=handle<VkBuffer>(next++);mapping=new std::byte[n];std::memset(mapping,0xda,n);allocations.emplace(number(buffer),Allocation{static_cast<std::byte*>(mapping),n,u});
}
Buffer::~Buffer(){assert(pending.empty());++destructions;allocations.erase(number(buffer));delete[] static_cast<std::byte*>(mapping);}
VkBuffer Buffer::Handle()const{return buffer;}
std::span<std::byte>Buffer::Bytes(){return{static_cast<std::byte*>(mapping),size};}
}
int main(int argc,char** argv){try{
 if(argc==3&&std::strcmp(argv[1],"--env")==0){assert(ColorSampleStagingCopyEnabled()==(std::strcmp(argv[2],"1")==0));return 0;}
 const auto c=context();
 for(const auto samples:{2u,4u,8u}){
  auto target=color(samples);guest.assign(target.bytes,std::byte{0x4b});auto expected=guest;expected[13]=std::byte{0x91};
  {ColorSampleSnapshotCopy owner(c,target,reader);assert(owner.Original().size()==target.bytes&&std::equal(owner.Original().begin(),owner.Original().end(),expected.begin()));
   assert(std::all_of(owner.Result().begin(),owner.Result().end(),[](auto b){return b==std::byte{0xda};}));
   rejects([&]{owner.DetileSource();});rejects([&]{owner.RetileDestination();});rejects([&]{owner.RecordSeed(VK_NULL_HANDLE);});
   for(const auto* name:{"vkCmdCopyBuffer","vkCmdPipelineBarrier"}){missing=name;rejects([&]{owner.RecordSeed(handle<VkCommandBuffer>(2));});missing.clear();}
   auto first=barriers.size();{Batch batch;owner.RecordSeed(handle<VkCommandBuffer>(2));assert(pending.size()==1);
    assert(owner.Result()[13]==std::byte{0xda}); // recording does not run a CPU memcpy.
    assert(owner.Original()[13]==std::byte{0x91});guest[13]=std::byte{0x77};guest.back()=std::byte{0x88};
    auto source=owner.DetileSource(),destination=owner.RetileDestination();assert(source.buffer!=destination.buffer&&source.device==c.device&&destination.device==c.device);
    assert(source.stage==(VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)&&source.access==(VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_SHADER_READ_BIT));
    assert(destination.stage==VK_PIPELINE_STAGE_TRANSFER_BIT&&destination.access==VK_ACCESS_TRANSFER_WRITE_BIT);
    rejects([&]{owner.RecordSeed(handle<VkCommandBuffer>(2));});batch.Submit();
   }
   assert(owner.Result().size()==expected.size()&&std::equal(owner.Result().begin(),owner.Result().end(),expected.begin()));
   assert(std::equal(owner.Original().begin(),owner.Original().end(),expected.begin())&&guest[13]==std::byte{0x77}&&guest.back()==std::byte{0x88});
   assert(barriers.size()==first+2&&barriers[first].srcAccessMask==VK_ACCESS_HOST_WRITE_BIT&&barriers[first].dstAccessMask==(VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_SHADER_READ_BIT)&&barriers[first+1].srcAccessMask==0&&barriers[first+1].dstAccessMask==VK_ACCESS_TRANSFER_WRITE_BIT);
   for(unsigned i=0;i<2;++i)assert(barriers[first+i].offset==0&&barriers[first+i].size==target.bytes&&barriers[first+i].srcQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED&&barriers[first+i].dstQueueFamilyIndex==VK_QUEUE_FAMILY_IGNORED);
   assert(stages.back().first==(VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)&&stages.back().second==(VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT));
  }assert(allocations.empty());
 }
 // Reader, metadata and either Buffer construction fail without commands,
 // leaked owners, or writes to the guest. Callback failures can follow a read.
 auto target=color(8);guest.assign(target.bytes,std::byte{0x4b});
 const auto count=constructions;for(unsigned failure:{count+1,count+3}){failAt=failure;bool failed=false;try{ColorSampleSnapshotCopy x(c,target,reader);}catch(const std::runtime_error&){failed=true;}assert(failed&&allocations.empty()&&pending.empty());}failAt=0;
 failReader=true;{bool failed=false;try{ColorSampleSnapshotCopy x(c,target,reader);}catch(const std::runtime_error&){failed=true;}assert(failed&&allocations.empty());}failReader=false;
 for(unsigned variant=0;variant<7;++variant){auto bad=target;if(variant==0)bad.format=VK_FORMAT_R32_UINT;if(variant==1)bad.samples=1;if(variant==2)bad.fragments=4;if(variant==3)bad.dccAddress=0x20000;if(variant==4)bad.bytes--;if(variant==5)bad.address++;if(variant==6)bad.mipTail=true;const auto n=constructions;rejects([&]{ColorSampleSnapshotCopy x(c,bad,reader);});assert(constructions==n&&allocations.empty());}
 rejects([&]{ColorSampleSnapshotCopy x(c,target,nullptr);});
 // Recorded but unsubmitted work is cancelled; submitted work is waited.
 for(bool submitted:{false,true}){ColorSampleSnapshotCopy owner(c,target,reader);try{Batch batch;owner.RecordSeed(handle<VkCommandBuffer>(2));if(submitted)batch.Submit();throw std::runtime_error("after-recording preparation failure");}catch(const std::runtime_error&){}assert(pending.empty()&&owner.Result()[13]==(submitted?std::byte{0x91}:std::byte{0xda}));}
 assert(allocations.empty()&&pending.empty());std::printf("snapshot copy owner contract PASS samples2/4/8 full padding/immutable baseline deferredcopy/fence/unwind %u guarded rejects reads=%u allocations=%u released=%u\n",rejections,reads,constructions,destructions);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"snapshot copy contract FAIL %s\n",e.what());return 1;}}
