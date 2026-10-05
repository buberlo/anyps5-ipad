/* SPDX-License-Identifier: MIT */
/* Compile-check and behavior check for Madeira's MoltenVK dlopen fallback.
 * Links upstreams/Madeira/build/win32u-unix/moltenvk_static_loader.c, which
 * the patch series adds. Run after scripts/apply-patches.sh.
 */
#include <stdio.h>
#include <string.h>

#include "../../upstreams/Madeira/build/win32u-unix/moltenvk_static_loader.h"

__attribute__((visibility("default"))) void *vkGetInstanceProcAddr(void *instance, const char *name)
{
    (void)instance;
    (void)name;
    return (void *)1;
}

__attribute__((visibility("default"))) void *vkGetDeviceProcAddr(void *device, const char *name)
{
    (void)device;
    (void)name;
    return (void *)2;
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

    void *icd = madeira_vk_dlopen("libMoltenVK.dylib", 1);
    expect("static fallback sentinel", icd == madeira_vk_static_sentinel());
    expect("instance proc from sentinel", madeira_vk_dlsym(icd, "vkGetInstanceProcAddr") == (void *)vkGetInstanceProcAddr);
    expect("device proc from sentinel", madeira_vk_dlsym(icd, "vkGetDeviceProcAddr") == (void *)vkGetDeviceProcAddr);
    expect("other symbol stays unresolved", madeira_vk_dlsym(icd, "vkCreateInstance") == NULL);
    expect("close sentinel", madeira_vk_dlclose(icd) == 0);
    return g_fail ? 1 : 0;
}
