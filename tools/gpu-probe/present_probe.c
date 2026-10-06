/* Exercises the actual Windows HWND -> winevulkan -> Metal surface bridge.
 * Successful present submissions still require separate visible-device evidence. */
#define WIN32_LEAN_AND_MEAN
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int closed;
static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_CLOSE) { closed = 1; return 0; }
    return DefWindowProcA(window, message, wparam, lparam);
}

static void json_string(FILE *file, const char *text)
{
    fputc('"', file);
    for (; *text; ++text) {
        unsigned char c = (unsigned char)*text;
        if (c == '"' || c == '\\') fprintf(file, "\\%c", c);
        else if (c < 32) fprintf(file, "\\u%04x", c);
        else fputc(c, file);
    }
    fputc('"', file);
}

static int has_extension(const VkExtensionProperties *extensions, uint32_t count, const char *name)
{
    for (uint32_t i = 0; i < count; ++i)
        if (!strcmp(extensions[i].extensionName, name)) return 1;
    return 0;
}

int main(int argc, char **argv)
{
    unsigned requested = 120, delay_ms = 16, presented = 0;
    const char *report_path = NULL, *stage = "arguments";
    int exit_code = 1;
    VkResult result = VK_SUCCESS;
    DWORD win32_error = 0;
    char device_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = "";
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE, *ready = NULL;
    VkFence fence = VK_NULL_HANDLE;
    VkImage *images = NULL;
    uint32_t image_count = 0, family = UINT32_MAX;
    VkExtent2D extent = {640, 360};
    HWND window = NULL;
    HINSTANCE application = GetModuleHandleA(NULL);
    LARGE_INTEGER start, end, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    for (int i = 1; i < argc; ++i) {
        if ((!strcmp(argv[i], "--frames") || !strcmp(argv[i], "--delay-ms")) && i + 1 < argc) {
            const int is_frames = !strcmp(argv[i], "--frames");
            char *tail;
            unsigned long value = strtoul(argv[++i], &tail, 10);
            if (*tail || !*argv[i] || value > 360000 || (is_frames && !value)) goto done;
            if (is_frames) requested = (unsigned)value; else delay_ms = (unsigned)value;
        } else if (!strcmp(argv[i], "--report") && i + 1 < argc) report_path = argv[++i];
        else goto done;
    }
#define CHECK(call, label) do { stage = label; result = (call); if (result != VK_SUCCESS) goto done; } while (0)
#define REQUIRE(condition, label) do { if (!(condition)) { stage = label; result = VK_ERROR_INITIALIZATION_FAILED; goto done; } } while (0)
    WNDCLASSA klass = {0};
    klass.lpfnWndProc = window_proc;
    klass.hInstance = application;
    klass.lpszClassName = "APS5VulkanPresentProbe";
    klass.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClassA(&klass)) { stage = "register_window"; win32_error = GetLastError(); goto done; }
    window = CreateWindowExA(0, klass.lpszClassName, "AnyPS5 Vulkan present probe", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                             CW_USEDEFAULT, CW_USEDEFAULT, 640, 360, NULL, NULL, application, NULL);
    if (!window) { stage = "create_window"; win32_error = GetLastError(); goto done; }
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    uint32_t extension_count = 0;
    CHECK(vkEnumerateInstanceExtensionProperties(NULL, &extension_count, NULL), "instance_extensions");
    VkExtensionProperties *extensions = calloc(extension_count, sizeof(*extensions));
    REQUIRE(extensions, "instance_extension_allocation");
    result = vkEnumerateInstanceExtensionProperties(NULL, &extension_count, extensions);
    const int surface_support = has_extension(extensions, extension_count, VK_KHR_SURFACE_EXTENSION_NAME);
    const int win32_support = has_extension(extensions, extension_count, VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    const int portability = has_extension(extensions, extension_count, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    free(extensions);
    if (result != VK_SUCCESS) { stage = "instance_extensions"; goto done; }
    REQUIRE(surface_support && win32_support, "win32_surface_extension_missing");
    const char *instance_extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
                                         VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME};
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "APS5 present probe",
                             .applicationVersion = 1, .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
        .flags = portability ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0,
        .enabledExtensionCount = portability ? 3u : 2u, .ppEnabledExtensionNames = instance_extensions};
    CHECK(vkCreateInstance(&ici, NULL, &instance), "create_instance");
    VkWin32SurfaceCreateInfoKHR sci = {.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
                                      .hinstance = application, .hwnd = window};
    CHECK(vkCreateWin32SurfaceKHR(instance, &sci, NULL, &surface), "create_win32_surface");
    uint32_t physical_count = 0;
    CHECK(vkEnumeratePhysicalDevices(instance, &physical_count, NULL), "enumerate_devices");
    REQUIRE(physical_count, "no_physical_device");
    VkPhysicalDevice *devices = calloc(physical_count, sizeof(*devices));
    REQUIRE(devices, "physical_device_allocation");
    result = vkEnumeratePhysicalDevices(instance, &physical_count, devices);
    if (result != VK_SUCCESS) { free(devices); stage = "enumerate_devices"; goto done; }
    for (uint32_t d = 0; d < physical_count && !physical; ++d) {
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &count, NULL);
        VkQueueFamilyProperties *families = calloc(count, sizeof(*families));
        if (!families) { free(devices); stage = "queue_family_allocation"; goto done; }
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &count, families);
        for (uint32_t q = 0; q < count; ++q) {
            VkBool32 supported = VK_FALSE;
            result = vkGetPhysicalDeviceSurfaceSupportKHR(devices[d], q, surface, &supported);
            if (result == VK_SUCCESS && supported && (families[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                physical = devices[d]; family = q; break;
            }
        }
        free(families);
    }
    free(devices);
    REQUIRE(physical, "no_graphics_present_queue");
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    memcpy(device_name, properties.deviceName, sizeof(device_name));
    device_name[sizeof(device_name) - 1] = 0;
    CHECK(vkEnumerateDeviceExtensionProperties(physical, NULL, &extension_count, NULL), "device_extensions");
    extensions = calloc(extension_count, sizeof(*extensions));
    REQUIRE(extensions, "device_extension_allocation");
    result = vkEnumerateDeviceExtensionProperties(physical, NULL, &extension_count, extensions);
    const int swapchain_support = has_extension(extensions, extension_count, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    const int subset = has_extension(extensions, extension_count, "VK_KHR_portability_subset");
    free(extensions);
    if (result != VK_SUCCESS) { stage = "device_extensions"; goto done; }
    REQUIRE(swapchain_support, "swapchain_extension_missing");
    const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, "VK_KHR_portability_subset"};
    float priority = 1;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family,
                                  .queueCount = 1, .pQueuePriorities = &priority};
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qci, .enabledExtensionCount = subset ? 2u : 1u, .ppEnabledExtensionNames = device_extensions};
    CHECK(vkCreateDevice(physical, &dci, NULL, &device), "create_device");
    vkGetDeviceQueue(device, family, 0, &queue);
    VkSurfaceCapabilitiesKHR caps;
    CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps), "surface_capabilities");
    REQUIRE(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT, "surface_transfer_destination_missing");
    uint32_t format_count = 0;
    CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, NULL), "surface_formats");
    REQUIRE(format_count, "no_surface_formats");
    VkSurfaceFormatKHR *formats = calloc(format_count, sizeof(*formats));
    REQUIRE(formats, "surface_format_allocation");
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, formats);
    VkSurfaceFormatKHR format = formats[0];
    for (uint32_t i = 0; i < format_count; ++i)
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) { format = formats[i]; break; }
    free(formats);
    if (result != VK_SUCCESS) { stage = "surface_formats"; goto done; }
    if (format.format == VK_FORMAT_UNDEFINED) format.format = VK_FORMAT_B8G8R8A8_UNORM;
    if (caps.currentExtent.width != UINT32_MAX) extent = caps.currentExtent;
    else {
        if (extent.width < caps.minImageExtent.width) extent.width = caps.minImageExtent.width;
        if (extent.width > caps.maxImageExtent.width) extent.width = caps.maxImageExtent.width;
        if (extent.height < caps.minImageExtent.height) extent.height = caps.minImageExtent.height;
        if (extent.height > caps.maxImageExtent.height) extent.height = caps.maxImageExtent.height;
    }
    REQUIRE(extent.width && extent.height, "zero_surface_extent");
    image_count = caps.minImageCount + 1;
    if (caps.maxImageCount && image_count > caps.maxImageCount) image_count = caps.maxImageCount;
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (unsigned bit = 1; bit <= VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR; bit <<= 1)
        if (caps.supportedCompositeAlpha & bit) { alpha = (VkCompositeAlphaFlagBitsKHR)bit; break; }
    VkSwapchainCreateInfoKHR swci = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = surface,
        .minImageCount = image_count, .imageFormat = format.format, .imageColorSpace = format.colorSpace,
        .imageExtent = extent, .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, .preTransform = caps.currentTransform,
        .compositeAlpha = alpha, .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE};
    CHECK(vkCreateSwapchainKHR(device, &swci, NULL, &swapchain), "create_swapchain");
    CHECK(vkGetSwapchainImagesKHR(device, swapchain, &image_count, NULL), "swapchain_images");
    images = calloc(image_count, sizeof(*images));
    ready = calloc(image_count, sizeof(*ready));
    REQUIRE(images && ready, "swapchain_image_allocation");
    CHECK(vkGetSwapchainImagesKHR(device, swapchain, &image_count, images), "swapchain_images");
    VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = family};
    CHECK(vkCreateCommandPool(device, &pci, NULL, &pool), "command_pool");
    VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    CHECK(vkAllocateCommandBuffers(device, &cai, &command), "command_buffer");
    VkSemaphoreCreateInfo semci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    CHECK(vkCreateSemaphore(device, &semci, NULL, &acquired), "acquire_semaphore");
    for (uint32_t i = 0; i < image_count; ++i)
        CHECK(vkCreateSemaphore(device, &semci, NULL, &ready[i]), "present_semaphore");
    VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    CHECK(vkCreateFence(device, &fci, NULL, &fence), "frame_fence");
    for (unsigned frame = 0; frame < requested; ++frame) {
        MSG message;
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) closed = 1;
            TranslateMessage(&message); DispatchMessageA(&message);
        }
        if (closed) { stage = "window_closed"; exit_code = 2; goto done; }
        CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(30000000000)), "wait_frame");
        uint32_t index = 0;
        stage = "acquire_image";
        result = vkAcquireNextImageKHR(device, swapchain, UINT64_C(30000000000), acquired, VK_NULL_HANDLE, &index);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) goto done;
        CHECK(vkResetFences(device, 1, &fence), "reset_fence");
        CHECK(vkResetCommandBuffer(command, 0), "reset_command");
        VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                          .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        CHECK(vkBeginCommandBuffer(command, &begin), "begin_command");
        VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = images[index], .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, NULL, 0, NULL, 1, &barrier);
        float phase = (float)(frame % 120) / 119.0f;
        VkClearColorValue color = {.float32 = {phase, 0.15f, 1.0f - phase, 1.0f}};
        vkCmdClearColorImage(command, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &barrier.subresourceRange);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0, 0, NULL, 0, NULL, 1, &barrier);
        CHECK(vkEndCommandBuffer(command), "end_command");
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1,
            .pWaitSemaphores = &acquired, .pWaitDstStageMask = &wait_stage, .commandBufferCount = 1,
            .pCommandBuffers = &command, .signalSemaphoreCount = 1, .pSignalSemaphores = &ready[index]};
        CHECK(vkQueueSubmit(queue, 1, &submit, fence), "queue_submit");
        VkResult per_swapchain = VK_NOT_READY;
        VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1,
            .pWaitSemaphores = &ready[index], .swapchainCount = 1, .pSwapchains = &swapchain,
            .pImageIndices = &index, .pResults = &per_swapchain};
        stage = "queue_present";
        result = vkQueuePresentKHR(queue, &present);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) goto done;
        if (per_swapchain != VK_SUCCESS && per_swapchain != VK_SUBOPTIMAL_KHR) { result = per_swapchain; goto done; }
        ++presented;
        if (delay_ms) Sleep(delay_ms);
    }
    CHECK(vkDeviceWaitIdle(device), "wait_device_idle");
    stage = "complete"; exit_code = 0;
done:
    if (device) {
        VkResult idle_result = vkDeviceWaitIdle(device);
        if (idle_result != VK_SUCCESS && exit_code == 0) { result = idle_result; stage = "cleanup_wait_idle"; exit_code = 1; }
        if (fence) vkDestroyFence(device, fence, NULL);
        if (acquired) vkDestroySemaphore(device, acquired, NULL);
        if (ready) for (uint32_t i = 0; i < image_count; ++i) if (ready[i]) vkDestroySemaphore(device, ready[i], NULL);
        if (pool) vkDestroyCommandPool(device, pool, NULL);
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, NULL);
        vkDestroyDevice(device, NULL);
    }
    free(images); free(ready);
    if (surface) vkDestroySurfaceKHR(instance, surface, NULL);
    if (instance) vkDestroyInstance(instance, NULL);
    if (window) DestroyWindow(window);
    QueryPerformanceCounter(&end);
    FILE *report = stdout;
    if (report_path) {
        report = fopen(report_path, "wb");
        if (!report) { report = stdout; stage = "write_report"; exit_code = 1; }
    }
    fprintf(report, "{\"schema\":1,\"probe\":\"win32_vulkan_present\",\"status\":\"%s\",\"stage\":",
            exit_code == 0 ? "pass" : exit_code == 2 ? "stopped" : "fail");
    json_string(report, stage);
    fprintf(report, ",\"vk_result\":%d,\"win32_error\":%lu,\"device\":", result, (unsigned long)win32_error);
    json_string(report, device_name);
    fprintf(report, ",\"requested_frames\":%u,\"accepted_presents\":%u,\"width\":%u,\"height\":%u,"
                    "\"elapsed_ms\":%.3f,\"visible_device_verified\":false}\n", requested, presented, extent.width, extent.height,
            1000.0 * (double)(end.QuadPart - start.QuadPart) / (double)frequency.QuadPart);
    if (report != stdout && fclose(report)) exit_code = 1;
    return exit_code;
}
