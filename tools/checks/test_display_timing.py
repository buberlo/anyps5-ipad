#!/usr/bin/env python3
"""Compile production telemetry against native Vulkan fakes; never needs a GPU."""
from pathlib import Path
import os
import json
import shlex
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = r'''
#include <vulkan/vulkan_core.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static VkResult madeira_vkCreateDevice_base(VkPhysicalDevice, const VkDeviceCreateInfo *, const VkAllocationCallbacks *, VkDevice *);
static uint64_t fake_ns = 1000000000;
static int test_clock_gettime(clockid_t kind, struct timespec *ts) { (void)kind;ts->tv_sec=fake_ns/1000000000;ts->tv_nsec=fake_ns%1000000000;return 0; }
#define clock_gettime test_clock_gettime
#include "aps5_display_timing.h"
#undef clock_gettime
static int extension_supported = 1, saw_extension, injected, create_calls, destroy_calls;
static VkResult present_result = VK_SUCCESS, past_result = VK_SUCCESS, create_result = VK_SUCCESS;
static struct aps5_past_timing pending[64];
static uint32_t pending_count;
static VkResult madeira_vkCreateDevice_base(VkPhysicalDevice p,const VkDeviceCreateInfo *i,const VkAllocationCallbacks *a,VkDevice *d) {
 (void)p;(void)a; ++create_calls; saw_extension=0;
 for(uint32_t n=0;n<i->enabledExtensionCount;++n) if(!strcmp(i->ppEnabledExtensionNames[n],"VK_GOOGLE_display_timing")) ++saw_extension;
 *d=(VkDevice)(uintptr_t)1;return create_result;
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice p,const char *n,uint32_t *c,VkExtensionProperties *e) {
 (void)p;(void)n;*c=extension_supported?1:0;
 if(e&&extension_supported) strcpy(e[0].extensionName,"VK_GOOGLE_display_timing");return VK_SUCCESS;
}
static VkResult VKAPI_CALL fake_past(VkDevice d,VkSwapchainKHR s,uint32_t *n,struct aps5_past_timing *t) {
 (void)d;(void)s;if(past_result!=VK_SUCCESS&&past_result!=VK_INCOMPLETE)return past_result;
 assert(*n>=pending_count);*n=pending_count;memcpy(t,pending,pending_count*sizeof(*t));pending_count=0;return past_result;
}
static VkResult VKAPI_CALL fake_refresh(VkDevice d,VkSwapchainKHR s,struct aps5_refresh_cycle *r) { (void)d;(void)s;r->refreshDuration=16666667;return VK_SUCCESS; }
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice d,const char *n) {
 (void)d;if(!strcmp(n,"vkGetPastPresentationTimingGOOGLE"))return (PFN_vkVoidFunction)fake_past;
 if(!strcmp(n,"vkGetRefreshCycleDurationGOOGLE"))return (PFN_vkVoidFunction)fake_refresh;return NULL;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice d,const VkSwapchainCreateInfoKHR *i,const VkAllocationCallbacks *a,VkSwapchainKHR *s) {
 (void)d;(void)i;(void)a;static uintptr_t identity=42;*s=(VkSwapchainKHR)identity++;return create_result;
}
VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice d,VkSwapchainKHR s,const VkAllocationCallbacks *a) { (void)d;(void)s;(void)a;++destroy_calls; }
static const void *expected_next;
VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue q,const VkPresentInfoKHR *i) {
 (void)q;const struct aps5_present_times *t=i->pNext;
 if(t&&t->sType==APS5_GOOGLE_PRESENT_TIMES) {
  ++injected;assert(t->swapchainCount==i->swapchainCount);
  assert(t->pTimes[0].presentID!=0);assert(t->pNext==expected_next);
 } else assert(i->pNext==expected_next);
 return present_result;
}
static void add_pending(uint32_t id,uint64_t ns) { pending[pending_count++]=(struct aps5_past_timing){id,0,ns,ns,0}; }
static void present(VkPresentInfoKHR *info) { fake_ns+=300000000; assert(aps5_timing_present(VK_NULL_HANDLE,info)==present_result); }
int main(void) {
 struct aps5_display_stats stats={0};stats.refresh_ns=16666667;
 assert(aps5_display_add(&stats,100000000));assert(!aps5_display_add(&stats,100000000));assert(!aps5_display_add(&stats,99999999));
 assert(aps5_display_add(&stats,116666667));assert(aps5_display_add(&stats,150000001));
 assert(stats.count==3&&stats.missed_vblanks==1);
 uint64_t mean,p95,p99;aps5_display_summary(&stats,&mean,&p95,&p99);assert(mean==25000000&&p95==33333334&&p99==33333334);
 for(unsigned i=0;i<600;++i)assert(aps5_display_add(&stats,stats.last_ns+16666667));assert(stats.used==256);
 aps5_display_reset_window(&stats);assert(!stats.used&&!stats.last_ns&&stats.count==603);
 VkDeviceCreateInfo device_info={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};VkDevice device;
 assert(aps5_timing_create_device(VK_NULL_HANDLE,&device_info,NULL,&device)==VK_SUCCESS);assert(saw_extension==1&&create_calls==1);
 assert(device_info.enabledExtensionCount==0&&device_info.ppEnabledExtensionNames==NULL);
 aps5_timing_set_active(1);
 VkSwapchainCreateInfoKHR swap_info={.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,.imageExtent={1280,720}};VkSwapchainKHR swapchain;
 assert(aps5_timing_create_swapchain(device,&swap_info,NULL,&swapchain)==VK_SUCCESS);assert(aps5_vulkan_display_timing_available());
 uint32_t index=0;VkPresentInfoKHR info={.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,.swapchainCount=1,.pSwapchains=&swapchain,.pImageIndices=&index};
 add_pending(2,116666667);add_pending(1,100000000);present(&info);assert(injected==1&&aps5_vulkan_displayed_count()==2);assert(aps5_vulkan_display_fps()>59.99&&aps5_vulkan_display_fps()<60.01);
 add_pending(2,116666667);add_pending(3,133333334);past_result=VK_INCOMPLETE;present(&info);assert(aps5_vulkan_displayed_count()==3);past_result=VK_SUCCESS;
 /* Native errors are never changed into successful submissions; old completed records remain valid. */
 present_result=VK_ERROR_OUT_OF_DATE_KHR;present(&info);assert(aps5_vulkan_displayed_count()==3);present_result=VK_SUCCESS;
 aps5_timing_set_active(0);assert(!aps5_vulkan_display_timing_available());assert(aps5_vulkan_display_fps()==-1);add_pending(4,166666668);present(&info);assert(aps5_vulkan_displayed_count()==3);
 aps5_timing_set_active(1);add_pending(5,200000002);present(&info);assert(aps5_vulkan_displayed_count()==3);
 add_pending(6,216666669);present(&info);assert(aps5_vulkan_displayed_count()==4);
 /* Preserve callers' original timing requests, IDs, desired times and following chain. */
 struct aps5_present_time caller_time={999,987654321};struct aps5_present_times caller={APS5_GOOGLE_PRESENT_TIMES,NULL,1,&caller_time};
 info.pNext=&caller;expected_next=NULL;int before=injected;present(&info);assert(injected==before+1&&caller_time.presentID==999&&caller_time.desiredPresentTime==987654321);
 info.pNext=NULL;
 /* Preserve any unrelated pNext chain when injecting. */
 VkBaseInStructure marker={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO};info.pNext=&marker;expected_next=&marker;present(&info);assert(info.pNext==&marker);
 info.pNext=NULL;expected_next=NULL;
 /* Replacement never mixes old-surface completions with the current one. */
 uint64_t epoch_before=aps5_display_epoch,count_before=aps5_vulkan_displayed_count();VkSwapchainKHR replacement;
 assert(aps5_timing_create_swapchain(device,&swap_info,NULL,&replacement)==VK_SUCCESS);
 assert(aps5_display_epoch>epoch_before&&aps5_vulkan_display_fps()==0);
 add_pending(7,233333336);present(&info);assert(aps5_vulkan_displayed_count()==count_before);pending_count=0;
 info.pSwapchains=&replacement;add_pending(8,250000003);present(&info);assert(aps5_vulkan_displayed_count()==count_before+1);
 fake_ns+=2000000000;assert(aps5_vulkan_display_fps()==0);
 // Capacity exhaustion must mark telemetry unavailable, not reuse old FPS.
 VkSwapchainKHR extra[APS5_MAX_TIMED_SWAPCHAINS];
 for(unsigned i=0;i<APS5_MAX_TIMED_SWAPCHAINS;++i)
  assert(aps5_timing_create_swapchain(device,&swap_info,NULL,&extra[i])==VK_SUCCESS);
 assert(!aps5_vulkan_display_timing_available()&&aps5_vulkan_display_fps()==-1);
 for(unsigned i=0;i<APS5_MAX_TIMED_SWAPCHAINS;++i)aps5_timing_destroy_swapchain(device,extra[i],NULL);
 assert(!aps5_vulkan_display_timing_available());
 // A fresh tracked surface restores measurement after freeing diagnostic slots.
 VkSwapchainKHR tracked;assert(aps5_timing_create_swapchain(device,&swap_info,NULL,&tracked)==VK_SUCCESS);
 assert(aps5_vulkan_display_timing_available());aps5_timing_destroy_swapchain(device,tracked,NULL);
 aps5_timing_destroy_swapchain(device,replacement,NULL);info.pSwapchains=&swapchain;
 aps5_phase_end("vkQueuePresentKHR",aps5_phase_begin());
 aps5_timing_destroy_swapchain(device,swapchain,NULL);assert(destroy_calls==3+APS5_MAX_TIMED_SWAPCHAINS&&!aps5_vulkan_display_timing_available());
 aps5_timing_destroy_device(device);
 extension_supported=0;assert(aps5_timing_create_device(VK_NULL_HANDLE,&device_info,NULL,&device)==VK_SUCCESS);assert(!saw_extension);
 assert(aps5_timing_create_swapchain(device,&swap_info,NULL,&swapchain)==VK_SUCCESS);assert(!aps5_vulkan_display_timing_available());before=injected;present(&info);assert(injected==before);
 create_result=VK_ERROR_OUT_OF_HOST_MEMORY;assert(aps5_timing_create_device(VK_NULL_HANDLE,&device_info,NULL,&device)==create_result);
 puts("PASS display timing: native completion, extension negotiation, bounded statistics, timestamps, errors, pNext, pause/resume and unsupported fallback");
}
'''
with tempfile.TemporaryDirectory(prefix="anyps5-display-") as temp:
    path = Path(temp)
    (path / "check.c").write_text(source)
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) + ["-std=c11", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror", "-pthread",
        "-I" + str(root / "upstreams/Madeira/build/win32u-unix"),
        "-I" + str(root / "upstreams/AnyPS5/3rdparty/Vulkan-Headers/include"),
        str(path / "check.c"), "-o", str(path / "check")], check=True)
    env = dict(os.environ); env["APS5_PERF_REPORT"] = "1"
    result = subprocess.run([str(path / "check")], check=True, timeout=10, env=env, capture_output=True, text=True)
    reports = [json.loads(line.split("] ", 1)[1]) for line in result.stderr.splitlines() if line.startswith("[anyps5-display] ")]
    assert reports, "Production native telemetry did not emit a report"
    for report in reports:
        assert report["source"] == "VK_GOOGLE_display_timing" and report["schema"] == 1
        assert len(report["interval_histogram"]) == 257 and report["histogram_step_ns"] == 100000
        assert report["epoch"] > 0 and report["swapchain_id"] > 0
    print(result.stdout.strip())
    print("PASS production JSON telemetry schema and bounded histogram output")
