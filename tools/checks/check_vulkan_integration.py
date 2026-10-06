#!/usr/bin/env python3
"""Execute the production Metal initializer and Unix binder with host fakes.

No GPU is claimed: these tests cover the static-handle bug, missing Metal
extension, and ARM64EC versus unsupported wow64 table dispatch.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
madeira = root / "upstreams/Madeira"
loader = madeira / "build/win32u-unix/moltenvk_static_loader.c"


def function(source, start):
    first = source.index(start)
    pos = source.index("{", first) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[first:pos]


surface = (madeira / "build/win32u-unix/vulkan_metal_ios.c").read_text()
initializer = function(surface, "UINT winios_pVulkanInit(")
surface_harness = r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "moltenvk_static_loader.h"
typedef unsigned UINT;
typedef int VkResult;
typedef struct { char extensionName[256]; uint32_t specVersion; } VkExtensionProperties;
typedef void (*Proc)(void);
typedef Proc (*PFN_vkGetInstanceProcAddr)(void *, const char *);
typedef VkResult (*PFN_vkEnumerateInstanceExtensionProperties)(const char *, uint32_t *, VkExtensionProperties *);
#define VK_NULL_HANDLE NULL
#define VK_SUCCESS 0
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER 0xc000000dU
#define STATUS_NOT_IMPLEMENTED 0xc0000002U
#define STATUS_NO_MEMORY 0xc0000017U
#define WINE_VULKAN_DRIVER_VERSION 123
struct vulkan_driver_funcs { int identity; };
static const struct vulkan_driver_funcs ios_vulkan_driver_funcs = {42};
static int g_use_metal_surface, advertise = 1, fail_enumerate;
static VkResult enumerate(const char *layer, uint32_t *count, VkExtensionProperties *props) {
    (void)layer;
    if (fail_enumerate) return -3;
    if (props) strcpy(props[0].extensionName, advertise ? "VK_EXT_metal_surface" : "VK_EXT_headless_surface");
    *count = 1;
    return VK_SUCCESS;
}
Proc vkGetInstanceProcAddr(void *instance, const char *name) {
    assert(instance == NULL);
    return !strcmp(name,"vkEnumerateInstanceExtensionProperties") ? (Proc)enumerate : NULL;
}
Proc vkGetDeviceProcAddr(void *device, const char *name) {
    (void)device; (void)name; return NULL;
}
'''
surface_harness += initializer + r'''
int main(void) {
    const struct vulkan_driver_funcs *driver = NULL;
    void *handle = madeira_vk_dlopen("/__anyps5_missing__/libMoltenVK.dylib", 1);
    assert(handle == madeira_vk_static_sentinel());
    assert(winios_pVulkanInit(123, handle, &driver) == 0);
    assert(driver == &ios_vulkan_driver_funcs && g_use_metal_surface);
    assert(winios_pVulkanInit(122, handle, &driver) == STATUS_INVALID_PARAMETER);
    assert(winios_pVulkanInit(123, handle, NULL) == STATUS_INVALID_PARAMETER);
    advertise = 0;
    assert(winios_pVulkanInit(123, handle, &driver) == STATUS_NOT_IMPLEMENTED);
    assert(driver == NULL && !g_use_metal_surface);
    advertise = 1; fail_enumerate = 1;
    assert(winios_pVulkanInit(123, handle, &driver) == STATUS_NOT_IMPLEMENTED);
    assert(winios_pVulkanInit(123, NULL, &driver) == STATUS_NOT_IMPLEMENTED);
    puts("PASS: static ICD sentinel reaches Metal initializer; missing extension and failed enumeration refuse initialization");
}
'''

virtual = (madeira / "build/ntdll-unix/virtual_ios.c").read_text()
binder = function(virtual, "static NTSTATUS ios_bind_unixlib_table(")
assert 'madeira_winevulkan_matches(match) || madeira_winevulkan_matches(modname)' in virtual
assert 'madeira_winevulkan_table(0), NULL, funcs' in virtual
binder_harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
typedef int BOOL;
typedef int32_t NTSTATUS;
#define STATUS_SUCCESS 0
#define STATUS_NOT_SUPPORTED ((int32_t)0xc00000bb)
static int called;
static int init(void *args) { ++called; return *(int *)args; }
const void *winevulkan_unix_call_funcs[] = {(const void *)init};
#include "binder.h"
'''
binder_harness += binder + r'''
int main(void) {
    assert(madeira_winevulkan_matches("C:\\windows\\system32\\WINEVULKAN.DLL"));
    assert(madeira_winevulkan_matches("/usr/lib/wine/winevulkan.so"));
    assert(madeira_winevulkan_matches("winevulkan"));
    assert(!madeira_winevulkan_matches(NULL));
    assert(!madeira_winevulkan_matches("winevulkan_helper.dll"));
    assert(!madeira_winevulkan_matches("vulkan-1.dll"));
    assert(madeira_winevulkan_table(1) == NULL);
    const void *table = NULL;
    assert(ios_bind_unixlib_table(NULL,"winevulkan",0,madeira_winevulkan_table(0),NULL,&table) == 0);
    int value = 42;
    assert(((int (*)(void *))((const void *const *)table)[0])(&value) == 42 && called == 1);
    table = NULL;
    assert(ios_bind_unixlib_table(NULL,"winevulkan",1,madeira_winevulkan_table(0),NULL,&table) == STATUS_NOT_SUPPORTED);
    assert(table == NULL && called == 1);
    puts("PASS: ARM64EC winevulkan binds and dispatches its real table shape; wow64 is refused");
}
'''

compiler = os.environ.get("CC", "cc")
present_source = (madeira / "build/winevulkan-unix/vulkan_ios.c").read_text()
present_harness = r'''
#include <stdint.h>
#include <stdatomic.h>
#include <assert.h>
#include <stdio.h>
typedef int VkResult;
typedef void *VkQueue;
typedef struct { uint32_t swapchainCount; VkResult *pResults; } VkPresentInfoKHR;
#define VK_SUCCESS 0
#define VK_SUBOPTIMAL_KHR 1000001003
static _Atomic(uint64_t) aps5_presented;
static int next_result;
static VkResult fake_present(VkQueue queue, const VkPresentInfoKHR *info) {
    (void)queue; (void)info; return next_result;
}
static const struct { VkResult (*p_vkQueuePresentKHR)(VkQueue, const VkPresentInfoKHR *); }
    driver = {fake_present};
#define aps5_vulkan_driver (&driver)
'''
present_harness += function(present_source, "static VkResult aps5_vkQueuePresentKHR(")
present_harness += r'''
int main(void) {
    VkPresentInfoKHR info = {2, NULL};
    assert(aps5_vkQueuePresentKHR(NULL, &info) == 0 && aps5_presented == 2);
    VkResult results[2] = {0, 0};
    info.pResults = results; next_result = -1;
    assert(aps5_vkQueuePresentKHR(NULL, &info) == -1 && aps5_presented == 2);
    next_result = VK_SUBOPTIMAL_KHR; results[1] = -4;
    assert(aps5_vkQueuePresentKHR(NULL, &info) == VK_SUBOPTIMAL_KHR && aps5_presented == 3);
    next_result = 0; results[0] = -4;
    assert(aps5_vkQueuePresentKHR(NULL, &info) == 0 && aps5_presented == 3);
    puts("PASS: presentation counter advances only on accepted presents, never uninitialized failure results");
}
'''
with tempfile.TemporaryDirectory(prefix="anyps5-vulkan-integration-") as temp:
    for name, source, extra in [
        ("surface", surface_harness, [str(loader), "-DMADEIRA_VK_STATIC_LINK=1"]),
        ("binder", binder_harness, []),
        ("present", present_harness, []),
    ]:
        c = Path(temp) / (name + ".c")
        exe = Path(temp) / name
        c.write_text(source)
        subprocess.run([compiler, "-std=c11", "-D_DARWIN_C_SOURCE", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(loader.parent), "-I" + str(madeira / "build/winevulkan-unix"),
                        str(c), *extra, "-ldl", "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
