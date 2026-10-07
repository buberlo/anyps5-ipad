#!/usr/bin/env python3
"""Check existing HLE image/semaphore indexing with two and three swapchain images.

Image-count policy is checked separately from the existing synchronization path. It executes
production AcquireImage, the blit submit setup and QueuePresent against a fake
Vulkan boundary; it cannot measure Metal drawable availability or device FPS.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
implementation = (root / 'upstreams/AnyPS5/core/libs/prx/libSceAgcDriver/Execution/src/VulkanDevice.cpp').read_text()
acquire = implementation[implementation.index('bool VulkanDevice::AcquireImage() {'):implementation.index('bool VulkanDevice::present(')]
present = implementation[implementation.index('void VulkanDevice::QueuePresent() {'):implementation.index('ShaderRecompiler::SpirvTarget VulkanDevice::ComputeTarget')]
submit = implementation[implementation.index('    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};', implementation.index('bool VulkanDevice::present(')):]
submit = submit[:submit.index('    const auto submittedAt =')]
source = r'''
#include "prx/libSceAgcDriver/Execution/include/PerformanceTimer.hpp"
#include "prx/libSceAgcDriver/Execution/include/SwapchainImages.hpp"
#include <vulkan/vulkan.h>
#include <cassert>
#include <limits>
#include <map>
#include <vector>
namespace AgcDriver {
template<typename T> static T handle(unsigned value) { return reinterpret_cast<T>(static_cast<uintptr_t>(value)); }
static unsigned nextIndex=0,creates=0,acquires=0,waits=0,submits=0,presents=0;
static bool waited=false;
static VkResult acquireResult=VK_SUCCESS;
static std::map<VkSemaphore,bool> signals;
static VkSemaphore expectedSemaphore=VK_NULL_HANDLE;
static VkFence expectedSlot=VK_NULL_HANDLE;
static VkResult VKAPI_CALL acquireImage(VkDevice,VkSwapchainKHR,uint64_t timeout,VkSemaphore semaphore,VkFence fence,uint32_t* index) {
 assert(timeout==5000000000ULL && semaphore==VK_NULL_HANDLE && fence==handle<VkFence>(5));
 ++acquires;waited=false;*index=nextIndex;return acquireResult;
}
static VkResult VKAPI_CALL waitFences(VkDevice,uint32_t count,const VkFence* fences,VkBool32 all,uint64_t timeout) {
 assert(count==1 && fences[0]==handle<VkFence>(5) && all==VK_TRUE && timeout==UINT64_MAX);
 ++waits;waited=true;return VK_SUCCESS;
}
static VkResult VKAPI_CALL resetFences(VkDevice,uint32_t count,const VkFence* fences) {
 assert(count==1 && (fences[0]==handle<VkFence>(5) || fences[0]==expectedSlot));return VK_SUCCESS;
}
static VkResult VKAPI_CALL createSemaphore(VkDevice,const VkSemaphoreCreateInfo* info,const VkAllocationCallbacks*,VkSemaphore* output) {
 assert(waited && info->sType==VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO && !info->pNext);
 *output=handle<VkSemaphore>(100+(++creates));signals[*output]=false;return VK_SUCCESS;
}
static VkResult VKAPI_CALL queueSubmit(VkQueue,uint32_t count,const VkSubmitInfo* info,VkFence fence) {
 assert(waited && fence==expectedSlot && count==1 && info->waitSemaphoreCount==0);
 assert(info->commandBufferCount==1 && info->signalSemaphoreCount==1 && info->pSignalSemaphores[0]==expectedSemaphore);
 assert(!signals[expectedSemaphore]);signals[expectedSemaphore]=true;++submits;return VK_SUCCESS;
}
static VkResult VKAPI_CALL queuePresent(VkQueue,const VkPresentInfoKHR* info) {
 assert(info->swapchainCount==1 && info->pImageIndices[0]==nextIndex && info->waitSemaphoreCount==1);
 assert(!info->pNext && info->pWaitSemaphores[0]==expectedSemaphore && signals[expectedSemaphore]);
 signals[expectedSemaphore]=false;++presents;return VK_SUCCESS;
}
static void require(bool value,const char* text) {if(!value)throw std::runtime_error(text);}
static void check(VkResult result,const char* text) {require(result==VK_SUCCESS,text);}
class VulkanDevice {
public:
 struct State {
  VkDevice device=handle<VkDevice>(1);VkQueue queue=handle<VkQueue>(2);VkSwapchainKHR swapchain=handle<VkSwapchainKHR>(3);
  VkExtent2D extent{768,432};VkFence acquireFence=handle<VkFence>(5);
  std::vector<VkImage> images;std::vector<VkSemaphore> rendered;
  bool imageAcquired=false,queuePending=false,presentIdEnabled=false;uint32_t acquiredIndex=0,queueIndex=0;
  void DestroyRetiredSwapchains() { assert(waited); }
  template<typename T,typename F> static T pointer(F f) {
   T out;static_assert(sizeof(out)==sizeof(f));std::memcpy(&out,&f,sizeof(out));return out;
  }
  template<typename T> T DeviceFunction(const char* name) {
   if(!std::strcmp(name,"vkAcquireNextImageKHR"))return pointer<T>(&acquireImage);
   if(!std::strcmp(name,"vkWaitForFences"))return pointer<T>(&waitFences);
   if(!std::strcmp(name,"vkResetFences"))return pointer<T>(&resetFences);
   if(!std::strcmp(name,"vkCreateSemaphore"))return pointer<T>(&createSemaphore);
   if(!std::strcmp(name,"vkQueueSubmit"))return pointer<T>(&queueSubmit);
   if(!std::strcmp(name,"vkQueuePresentKHR"))return pointer<T>(&queuePresent);
   abort();
  }
 } storage;
 State* state=&storage;
 explicit VulkanDevice(unsigned count) {storage.images.resize(count);storage.rendered.resize(count,VK_NULL_HANDLE);}
 bool AcquireImage();void QueuePresent();
 void Submit(unsigned slotIndex) {
  PerformanceTimer timing("test");auto commands=handle<VkCommandBuffer>(20+slotIndex);
  struct Slot {VkFence fence;} slot{handle<VkFence>(10+slotIndex)};expectedSlot=slot.fence;
  auto& rendered=state->rendered[state->acquiredIndex];expectedSemaphore=rendered;
''' + submit + r'''
  state->imageAcquired=false;state->queuePending=true;state->queueIndex=state->acquiredIndex;
 }
};
''' + acquire + present + r'''
}
int main() {
 using namespace AgcDriver;
 assert(SelectSwapchainImages(2,0,nullptr)==3);
 assert(SelectSwapchainImages(2,0,"2")==2);
 assert(SelectSwapchainImages(2,0,"3")==3);
 assert(SelectSwapchainImages(1,2,nullptr)==2);
 assert(SelectSwapchainImages(1,1,nullptr)==1);
 assert(SelectSwapchainImages(4,0,"2")==4);
 assert(SelectSwapchainImages(3,3,"2")==3);
 for(const char* bad:{"", "1", "4", "03", "3 ", "bad"}) {
  bool rejected=false;try {SelectSwapchainImages(2,0,bad);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
 }
 for(auto limits:{std::pair{0u,0u},std::pair{4u,3u}}) {
  bool rejected=false;try {SelectSwapchainImages(limits.first,limits.second,nullptr);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
 }
 for(unsigned count:{2u,3u}) {
  VulkanDevice device(count);const auto oldCreates=creates;
  for(unsigned frame=0;frame<count*5;++frame) {
   nextIndex=frame%count;assert(device.AcquireImage());
   assert(waited && device.storage.acquiredIndex==nextIndex);
   device.Submit(frame%2); // command/fence slots stay independent of image count
   device.QueuePresent();assert(!device.storage.queuePending);
  }
  assert(creates-oldCreates==count); // one reusable binary semaphore per image
  for(const auto& entry:signals)assert(!entry.second);
  nextIndex=count;bool rejected=false;
  try {device.AcquireImage();}catch(const std::runtime_error&){rejected=true;}assert(rejected);
  auto previousWaits=waits;acquireResult=VK_ERROR_OUT_OF_DATE_KHR;
  assert(!device.AcquireImage() && !device.storage.extent.width && waits==previousWaits);
  acquireResult=VK_SUCCESS;
 }
 assert(submits==25 && presents==25 && acquires==29 && waits==27);
 puts("PASS surface limits, explicit image selection, two/three image indexing, per-image semaphore reuse, two independent blit slots, acquire-fence ordering and out-of-date handling");
}
'''
with tempfile.TemporaryDirectory(prefix='anyps5-swapchain-images-') as temporary:
    path=Path(temporary);(path/'check.cpp').write_text(source)
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+[
        '-std=c++20','-DAPS5_ENABLE_TIMING_LOG=0','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-pthread',
        '-I'+str(root/'upstreams/AnyPS5/core/libs'),
        '-I'+str(root/'upstreams/AnyPS5/3rdparty/Vulkan-Headers/include'),
        str(path/'check.cpp'),'-o',str(path/'check')],check=True)
    env=dict(os.environ)
    env.pop('APS5_FRAME_TIMING',None);env.pop('APS5_PRESENT_ASSOCIATION_SECONDS',None)
    subprocess.run([str(path/'check')],env=env,check=True,timeout=10)
