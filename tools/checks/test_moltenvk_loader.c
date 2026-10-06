/* SPDX-License-Identifier: MIT */
/* Compile-check and behavior check for Madeira's MoltenVK dlopen fallback.
 * Links upstreams/Madeira/build/win32u-unix/moltenvk_static_loader.c, which
 * the patch series adds. Run after scripts/apply-patches.sh.
 */
#include <stdio.h>
#include <string.h>

#include "../../upstreams/Madeira/build/win32u-unix/moltenvk_static_loader.h"

typedef void (*test_vk_proc)(void);
static void instance_result(void) { }
static void device_result(void) { }

__attribute__((visibility("default"))) test_vk_proc vkGetInstanceProcAddr(void *instance, const char *name)
{
    (void)instance;
    (void)name;
    return instance_result;
}

__attribute__((visibility("default"))) test_vk_proc vkGetDeviceProcAddr(void *device, const char *name)
{
    (void)device;
    (void)name;
    return device_result;
}

static int g_fail;

static void expect(const char *label, int cond)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", label);
        g_fail++;
    } else {
        printf("ok %s\n", label);
    }
}

int main(void)
{
    expect("moltenvk path", madeira_vk_path_is_icd("libMoltenVK.dylib"));
    expect("vulkan framework path", madeira_vk_path_is_icd("/Madeira.app/Frameworks/vulkan.framework/vulkan"));
    expect("libvulkan soname", madeira_vk_path_is_icd("libvulkan.so.1"));
    expect("freetype is not an icd", !madeira_vk_path_is_icd("libfreetype.a"));
    expect("null path", !madeira_vk_path_is_icd(NULL));

    void *missing = madeira_vk_dlopen("libfreetype-not-installed.so", 1);
    expect("unrelated dlopen misses", missing == NULL);

    expect("null load path is a miss", madeira_vk_dlopen(NULL, 1) == NULL);
    void *icd = madeira_vk_dlopen("/__anyps5_missing__/libMoltenVK.dylib", 1);
    expect("static fallback sentinel", icd == madeira_vk_static_sentinel());
    expect("instance proc from sentinel", madeira_vk_dlsym(icd, "vkGetInstanceProcAddr") == (void *)vkGetInstanceProcAddr);
    expect("device proc from sentinel", madeira_vk_dlsym(icd, "vkGetDeviceProcAddr") == (void *)vkGetDeviceProcAddr);
    expect("other symbol stays unresolved", madeira_vk_dlsym(icd, "vkCreateInstance") == NULL);
    expect("null symbol is a miss", madeira_vk_dlsym(icd, NULL) == NULL);
    test_vk_proc (*instance_proc)(void *, const char *) =
        (test_vk_proc (*)(void *, const char *))madeira_vk_dlsym(icd, "vkGetInstanceProcAddr");
    expect("static resolver is callable", instance_proc && instance_proc(NULL, "test") == instance_result);
    expect("close sentinel", madeira_vk_dlclose(icd) == 0);
    return g_fail ? 1 : 0;
}
