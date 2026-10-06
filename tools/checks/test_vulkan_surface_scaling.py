#!/usr/bin/env python3
"""Compile real iOS scaling adapters against controlled host/Wine dispatch.

This is a dispatch/negotiation regression test, not a GPU or presentation proof.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'upstreams/Madeira/build/win32u-unix/vulkan_ios.c').read_text()


def function(start):
    first = source.index(start)
    pos = source.index('{', first) + 1
    depth = 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[first:pos]


harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#define VKAPI_CALL
#define VK_SUCCESS 0
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT 1
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 2
#define VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_SCALING_CREATE_INFO_EXT 3
#define VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_EXT 4
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR 5
#define VK_STRUCTURE_TYPE_SURFACE_PRESENT_SCALING_CAPABILITIES_EXT 6
#define VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR 7
#define VK_PRESENT_SCALING_STRETCH_BIT_EXT 1
/* Controlled ABI shapes for fields accessed by the actual adapters. */
typedef int VkResult;
typedef void *VkDevice, *VkPhysicalDevice, *VkInstance;
typedef uint64_t VkSwapchainKHR, VkSurfaceKHR;
typedef void (*Proc)(void);
typedef struct { int unused; } VkAllocationCallbacks;
typedef struct VkBaseInStructure { int sType; const struct VkBaseInStructure *pNext; } VkBaseInStructure;
typedef struct { int sType; void *pNext; int swapchainMaintenance1; } VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT;
typedef struct { int sType; void *pNext; } VkPhysicalDeviceFeatures2;
typedef struct { const void *pNext; int sentinel; } VkDeviceCreateInfo;
typedef struct { unsigned width, height; } VkExtent2D;
typedef struct { int sType; const void *pNext; unsigned scalingBehavior; } VkSwapchainPresentScalingCreateInfoEXT;
typedef struct { int sType; void *pNext; unsigned presentMode; } VkSurfacePresentModeEXT;
typedef struct { int sType; const void *pNext; VkSurfaceKHR surface; } VkPhysicalDeviceSurfaceInfo2KHR;
typedef struct { int sType; void *pNext; unsigned supportedPresentScaling; VkExtent2D minScaledImageExtent, maxScaledImageExtent; } VkSurfacePresentScalingCapabilitiesEXT;
typedef struct { int sType; void *pNext; } VkSurfaceCapabilities2KHR;
typedef struct { const void *pNext; VkSurfaceKHR surface; unsigned presentMode; VkExtent2D imageExtent; } VkSwapchainCreateInfoKHR;
typedef VkResult (*PFN_vkCreateSwapchainKHR)(VkDevice,const VkSwapchainCreateInfoKHR*,const VkAllocationCallbacks*,VkSwapchainKHR*);
typedef void (*PFN_vkGetPhysicalDeviceFeatures2)(VkPhysicalDevice,VkPhysicalDeviceFeatures2*);
struct vulkan_instance { struct { int has_VK_EXT_surface_maintenance1; } extensions;
    struct { VkInstance instance; } host;
    VkResult (*p_vkGetPhysicalDeviceSurfaceCapabilities2KHR)(VkPhysicalDevice,const VkPhysicalDeviceSurfaceInfo2KHR*,VkSurfaceCapabilities2KHR*); };
struct vulkan_physical_device { struct vulkan_instance *instance; struct { int has_VK_EXT_swapchain_maintenance1; } extensions;
    struct { VkPhysicalDevice physical_device; } host; };
struct vulkan_device { struct vulkan_physical_device *physical_device; struct { int has_VK_EXT_swapchain_maintenance1; } extensions;
    PFN_vkCreateSwapchainKHR p_vkCreateSwapchainKHR; };
struct surface { struct { struct { VkSurfaceKHR surface; } host; } obj; };
static struct vulkan_instance instance;
static struct vulkan_physical_device physical = {.instance=&instance};
static struct vulkan_device device = {.physical_device=&physical};
static struct surface surface = {.obj.host.surface=71};
static int feature_supported=1, create_error, query_error, scaling_supported=1;
static int enabled_feature, device_calls, swapchain_calls, saw_scaling, explicit_feature;
static const void *expected_tail;
static struct vulkan_physical_device *vulkan_physical_device_from_handle(VkPhysicalDevice p) { assert(p==&physical); return p; }
static struct vulkan_device *vulkan_device_from_handle(VkDevice p) { assert(p==&device); return p; }
static struct surface *surface_from_handle(VkSurfaceKHR s) { return s==11 ? &surface : NULL; }
static void query_features(VkPhysicalDevice p,VkPhysicalDeviceFeatures2 *f) {
    assert(p==(void*)19); ((VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT*)f->pNext)->swapchainMaintenance1=feature_supported;
}
static Proc p_vkGetInstanceProcAddr(VkInstance p,const char *name) {
    assert(p==(void*)17); return !strcmp(name,"vkGetPhysicalDeviceFeatures2") ? (Proc)query_features : NULL;
}
static VkResult native_swapchain(VkDevice p,const VkSwapchainCreateInfoKHR *info,const VkAllocationCallbacks *a,VkSwapchainKHR *ret) {
    (void)p;(void)info;(void)a;*ret=79;return 0;
}
static Proc p_vkGetDeviceProcAddr(VkDevice p,const char *name) {
    (void)p;assert(!strcmp(name,"vkCreateSwapchainKHR"));return (Proc)native_swapchain;
}
static VkResult win32u_vkCreateDevice(VkPhysicalDevice p,const VkDeviceCreateInfo *info,const VkAllocationCallbacks *a,VkDevice *ret) {
    (void)a;assert(p==&physical && info->sentinel==23);++device_calls;enabled_feature=0;
    const VkBaseInStructure *next=info->pNext;
    if (next && next->sType==1) {
        enabled_feature=((const VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT*)next)->swapchainMaintenance1;
        if (!explicit_feature) assert(next->pNext==expected_tail);
    } else assert(next==expected_tail);
    *ret=&device;device.extensions.has_VK_EXT_swapchain_maintenance1=1;device.p_vkCreateSwapchainKHR=native_swapchain;
    return create_error;
}
static VkResult query_scaling(VkPhysicalDevice p,const VkPhysicalDeviceSurfaceInfo2KHR *info,VkSurfaceCapabilities2KHR *caps) {
    assert(p==(void*)19 && info->surface==71);
    assert(((const VkSurfacePresentModeEXT*)info->pNext)->presentMode==2);
    VkSurfacePresentScalingCapabilitiesEXT *support=caps->pNext;
    support->supportedPresentScaling=scaling_supported;support->minScaledImageExtent=(VkExtent2D){1,1};support->maxScaledImageExtent=(VkExtent2D){4096,4096};return query_error;
}
static VkResult win32u_vkCreateSwapchainKHR(VkDevice p,const VkSwapchainCreateInfoKHR *info,const VkAllocationCallbacks *a,VkSwapchainKHR *ret) {
    (void)a;assert(p==&device);++swapchain_calls;saw_scaling=0;
    const VkBaseInStructure *next=info->pNext;
    if (next && next->sType==3) {saw_scaling=1;assert(next->pNext==expected_tail);}
    else assert(next==expected_tail);
    *ret=73;return 0;
}
'''
for start in ('static VkResult VKAPI_CALL madeira_scaled_host_swapchain(',
              'static VkResult madeira_client_create_device(',
              'static VkResult madeira_client_create_swapchain('):
    harness += function(start)
harness += r'''
int main(void) {
    instance.extensions.has_VK_EXT_surface_maintenance1=1;instance.host.instance=(void*)17;
    physical.extensions.has_VK_EXT_swapchain_maintenance1=1;physical.host.physical_device=(void*)19;
    instance.p_vkGetPhysicalDeviceSurfaceCapabilities2KHR=query_scaling;
    VkBaseInStructure tail={99,NULL};expected_tail=&tail;
    VkDeviceCreateInfo create={&tail,23};VkDevice out=NULL;
    assert(madeira_client_create_device(&physical,&create,NULL,&out)==0 && enabled_feature);
    assert(create.pNext==&tail && device.p_vkCreateSwapchainKHR==madeira_scaled_host_swapchain);
    VkSwapchainCreateInfoKHR swap={&tail,11,2,{768,432}};VkSwapchainKHR chain=0;
    assert(madeira_client_create_swapchain(&device,&swap,NULL,&chain)==0 && saw_scaling && swap.pNext==&tail);
    VkSwapchainPresentScalingCreateInfoEXT application_scaling={3,&tail,1};
    swap.pNext=&application_scaling;query_error=-9;
    assert(madeira_client_create_swapchain(&device,&swap,NULL,&chain)==0 && saw_scaling);
    assert(swap.pNext==&application_scaling && application_scaling.pNext==&tail);
    swap.pNext=&tail;query_error=0;
    scaling_supported=0;
    assert(madeira_client_create_swapchain(&device,&swap,NULL,&chain)==0 && !saw_scaling);
    scaling_supported=1;query_error=-9;int prior=swapchain_calls;
    assert(madeira_client_create_swapchain(&device,&swap,NULL,&chain)==-9 && swapchain_calls==prior);
    query_error=0;swap.imageExtent.width=8192;
    assert(madeira_client_create_swapchain(&device,&swap,NULL,&chain)==0 && !saw_scaling);
    feature_supported=0;
    assert(madeira_client_create_device(&physical,&create,NULL,&out)==0 && !enabled_feature);
    assert(device.p_vkCreateSwapchainKHR==native_swapchain);
    feature_supported=1;create_error=-7;
    assert(madeira_client_create_device(&physical,&create,NULL,&out)==-7 && device.p_vkCreateSwapchainKHR==native_swapchain);
    create_error=0;explicit_feature=1;
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT disabled={1,NULL,0};create.pNext=&disabled;
    assert(madeira_client_create_device(&physical,&create,NULL,&out)==0 && !enabled_feature);
    assert(device.p_vkCreateSwapchainKHR==native_swapchain && !disabled.swapchainMaintenance1);
    disabled.swapchainMaintenance1=1;
    assert(madeira_client_create_device(&physical,&create,NULL,&out)==0 && enabled_feature);
    assert(device.p_vkCreateSwapchainKHR==madeira_scaled_host_swapchain && disabled.pNext==NULL);
    explicit_feature=0;create.pNext=&tail;instance.extensions.has_VK_EXT_surface_maintenance1=0;
    assert(madeira_client_create_device(&physical,&create,NULL,&out)==0 && !enabled_feature);
    assert(device.p_vkCreateSwapchainKHR==native_swapchain);
    puts("PASS: actual scaling adapters negotiate features, preserve chains, reject unsupported scaling and propagate host errors");
}
'''
with tempfile.TemporaryDirectory(prefix='anyps5-vulkan-scaling-') as directory:
    c = Path(directory) / 'scaling.c'
    exe = Path(directory) / 'scaling'
    c.write_text(harness)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
