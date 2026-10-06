#!/usr/bin/env python3
"""Test production iOS admission/dispatch against a controlled native driver."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "upstreams/Madeira/build/win32u-unix/vulkan_ios.c").read_text()
start = source.index('#include "vulkan_lifecycle.h"')
end = source.index('static PFN_vkVoidFunction VKAPI_CALL madeira_vkGetInstanceProcAddr', start)
production = source[start:end]
prefix = r'''
#include <vulkan/vulkan_core.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
static pthread_mutex_t test_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t test_changed = PTHREAD_COND_INITIALIZER;
static int hold_submit, submit_entered, release_submit;
static atomic_int calls, idle_calls, destroyed, worker_started, worker_done, pause_done;
static VkResult create_result;
static VkResult madeira_vkCreateDevice_base(VkPhysicalDevice p, const VkDeviceCreateInfo *i,
 const VkAllocationCallbacks *a, VkDevice *d) {
 (void)p;(void)i;(void)a;*d=(VkDevice)(uintptr_t)0x1234;return create_result;
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue q,uint32_t n,const VkSubmitInfo *s,VkFence f) {
 (void)q;(void)n;(void)s;(void)f;atomic_fetch_add(&calls,1);
 pthread_mutex_lock(&test_lock);
 if(hold_submit) {submit_entered=1;pthread_cond_broadcast(&test_changed);
  while(!release_submit)pthread_cond_wait(&test_changed,&test_lock);}
 pthread_mutex_unlock(&test_lock);return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2(VkQueue q,uint32_t n,const VkSubmitInfo2 *s,VkFence f) {
 (void)s;return vkQueueSubmit(q,n,NULL,f);
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2KHR(VkQueue q,uint32_t n,const VkSubmitInfo2 *s,VkFence f) {
 return vkQueueSubmit2(q,n,s,f);
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue q,const VkPresentInfoKHR *i) {
 (void)i;return vkQueueSubmit(q,0,NULL,VK_NULL_HANDLE);
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue q) {
 (void)q;return VK_ERROR_DEVICE_LOST;
}
VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice d) {
 assert(d==(VkDevice)(uintptr_t)0x1234);atomic_fetch_add(&idle_calls,1);return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice d,VkSwapchainKHR s,uint64_t t,
 VkSemaphore m,VkFence f,uint32_t *i) {
 (void)d;(void)s;(void)t;(void)m;(void)f;*i=7;return VK_SUBOPTIMAL_KHR;
}
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImage2KHR(VkDevice d,const VkAcquireNextImageInfoKHR *a,uint32_t *i) {
 (void)a;return vkAcquireNextImageKHR(d,VK_NULL_HANDLE,0,VK_NULL_HANDLE,VK_NULL_HANDLE,i);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice d,const VkAllocationCallbacks *a) {
 (void)a;assert(d==(VkDevice)(uintptr_t)0x1234);atomic_fetch_add(&destroyed,1);
}
static void other_proc(void) {}
static PFN_vkVoidFunction VKAPI_CALL fake_get(VkDevice d,const char *n) {
 (void)d;return !strcmp(n,"absent")?NULL:other_proc;
}
static void *vulkan_handle;
static void *madeira_vk_dlsym(void *h,const char *n) {
 (void)h;assert(!strcmp(n,"vkGetDeviceProcAddr"));return (void *)fake_get;
}
'''
suffix = r'''
static void *submit_worker(void *arg) {
 (void)arg;atomic_store(&worker_started,1);
 assert(aps5_vkQueueSubmit(VK_NULL_HANDLE,0,NULL,VK_NULL_HANDLE)==VK_ERROR_OUT_OF_DEVICE_MEMORY);
 atomic_store(&worker_done,1);return NULL;
}
static void *pause_worker(void *arg) {
 (void)arg;aps5_vulkan_set_active(0);atomic_store(&pause_done,1);return NULL;
}
static void wait_for(atomic_int *flag) {
 for(unsigned i=0;i<2000&&!atomic_load(flag);i++)usleep(1000);
 assert(atomic_load(flag));
}
int main(void) {
 pthread_t worker,pauser;
 /* Inactive startup parks the actual forwarded call. No fake success. */
 assert(!pthread_create(&worker,NULL,submit_worker,NULL));wait_for(&worker_started);
 usleep(20000);assert(!atomic_load(&calls)&&!atomic_load(&worker_done));
 aps5_vulkan_set_active(1);assert(!pthread_join(worker,NULL));assert(atomic_load(&calls)==1);
 VkDevice device;create_result=VK_ERROR_INITIALIZATION_FAILED;
 assert(madeira_vkCreateDevice(VK_NULL_HANDLE,NULL,NULL,&device)==create_result);
 assert(!aps5_gpu_devices);
 create_result=VK_SUCCESS;assert(madeira_vkCreateDevice(VK_NULL_HANDLE,NULL,NULL,&device)==VK_SUCCESS);
 assert(aps5_gpu_devices&&aps5_gpu_devices->host==device);
 /* Pause cannot finish or drain until the running submission returns. */
 hold_submit=1;atomic_store(&worker_started,0);atomic_store(&worker_done,0);
 assert(!pthread_create(&worker,NULL,submit_worker,NULL));
 pthread_mutex_lock(&test_lock);while(!submit_entered)pthread_cond_wait(&test_changed,&test_lock);pthread_mutex_unlock(&test_lock);
 assert(!pthread_create(&pauser,NULL,pause_worker,NULL));usleep(20000);
 assert(!atomic_load(&pause_done)&&!atomic_load(&idle_calls));
 pthread_mutex_lock(&test_lock);release_submit=1;pthread_cond_broadcast(&test_changed);pthread_mutex_unlock(&test_lock);
 assert(!pthread_join(worker,NULL));assert(!pthread_join(pauser,NULL));
 assert(atomic_load(&idle_calls)==1&&!aps5_gpu.active&&!aps5_gpu.running);
 /* A new call remains parked across pause, then returns its real error. */
 atomic_store(&worker_started,0);atomic_store(&worker_done,0);
 assert(!pthread_create(&worker,NULL,submit_worker,NULL));wait_for(&worker_started);
 usleep(20000);assert(atomic_load(&calls)==2&&!atomic_load(&worker_done));
 aps5_vulkan_set_active(1);assert(!pthread_join(worker,NULL));assert(atomic_load(&calls)==3);
 #define CHECK_PROC(n) assert(madeira_vkGetDeviceProcAddr(device,#n)==(PFN_vkVoidFunction)aps5_##n)
 CHECK_PROC(vkQueueSubmit);CHECK_PROC(vkQueueSubmit2);CHECK_PROC(vkQueueSubmit2KHR);
 CHECK_PROC(vkQueuePresentKHR);CHECK_PROC(vkQueueWaitIdle);CHECK_PROC(vkDeviceWaitIdle);
 CHECK_PROC(vkAcquireNextImageKHR);CHECK_PROC(vkAcquireNextImage2KHR);CHECK_PROC(vkDestroyDevice);
 assert(madeira_vkGetDeviceProcAddr(device,"absent")==NULL);
 assert(madeira_vkGetDeviceProcAddr(device,"other")==other_proc);
 assert(aps5_vkQueueWaitIdle(VK_NULL_HANDLE)==VK_ERROR_DEVICE_LOST);
 uint32_t index=0;assert(aps5_vkAcquireNextImage2KHR(device,NULL,&index)==VK_SUBOPTIMAL_KHR&&index==7);
 aps5_vkDestroyDevice(device,NULL);assert(!aps5_gpu_devices&&atomic_load(&destroyed)==1);
 aps5_vulkan_set_active(0);assert(atomic_load(&idle_calls)==1);
 puts("PASS actual lifecycle: parked calls, quiescence, device lifetime, native dispatch and error propagation");
}
'''
with tempfile.TemporaryDirectory(prefix="anyps5-lifecycle-") as temp:
    path = Path(temp)
    (path / "check.c").write_text(prefix + production + suffix)
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
        "-std=c11", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror", "-pthread",
        "-I" + str(root / "upstreams/Madeira/build/win32u-unix"),
        "-I" + str(root / "upstreams/AnyPS5/3rdparty/Vulkan-Headers/include"),
        str(path / "check.c"), "-o", str(path / "check")], check=True)
    subprocess.run([str(path / "check")], check=True, timeout=10)
