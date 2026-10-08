/* Host-only production-header logging contract. Synthetic counters are format
 * fixtures, never GPU measurements or device performance evidence. */
#include <vulkan/vulkan_core.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static _Atomic(unsigned) report_writes;
static size_t longest_row;
static int fault_mode;
static uint64_t fake_ns = 1000000000;
static ssize_t report_write(int fd, const void *bytes, size_t size)
{
    unsigned call = atomic_fetch_add(&report_writes, 1);
    if (size > longest_row) longest_row = size;
    if (fault_mode == 1 && call < 2) { errno = EINTR; return -1; }
    if (fault_mode == 2) return write(fd, bytes, 35);
    if (fault_mode == 3) { errno = EIO; return -1; }
    return write(fd, bytes, size);
}
static int fake_clock(clockid_t kind, struct timespec *ts)
{
    (void)kind;
    ts->tv_sec = fake_ns / 1000000000;
    ts->tv_nsec = fake_ns % 1000000000;
    return 0;
}
static VkResult madeira_vkCreateDevice_base(VkPhysicalDevice, const VkDeviceCreateInfo *, const VkAllocationCallbacks *, VkDevice *);
#define write report_write
#define clock_gettime fake_clock
#include "aps5_display_timing.h"
#undef clock_gettime
#undef write

static VkResult madeira_vkCreateDevice_base(VkPhysicalDevice p, const VkDeviceCreateInfo *i, const VkAllocationCallbacks *a, VkDevice *d)
{
    (void)p; (void)i; (void)a; *d = (VkDevice)(uintptr_t)1; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice p, const char *n, uint32_t *c, VkExtensionProperties *e)
{
    (void)p; (void)n; *c = 1;
    if (e) strcpy(e[0].extensionName, "VK_GOOGLE_display_timing");
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL fake_past(VkDevice d, VkSwapchainKHR s, uint32_t *n, struct aps5_past_timing *t)
{
    (void)d; (void)s; assert(*n >= 1); *n = 1;
    *t = (struct aps5_past_timing){1, 0, fake_ns - 1, fake_ns - 1, 0};
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL fake_refresh(VkDevice d, VkSwapchainKHR s, struct aps5_refresh_cycle *r)
{
    (void)d; (void)s; r->refreshDuration = 16666667; return VK_SUCCESS;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice d, const char *n)
{
    (void)d;
    if (!strcmp(n, "vkGetPastPresentationTimingGOOGLE")) return (PFN_vkVoidFunction)fake_past;
    if (!strcmp(n, "vkGetRefreshCycleDurationGOOGLE")) return (PFN_vkVoidFunction)fake_refresh;
    return NULL;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice d, const VkSwapchainCreateInfoKHR *i, const VkAllocationCallbacks *a, VkSwapchainKHR *s)
{
    (void)d; (void)i; (void)a; *s = (VkSwapchainKHR)(uintptr_t)42; return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice d, VkSwapchainKHR s, const VkAllocationCallbacks *a)
{
    (void)d; (void)s; (void)a;
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue q, const VkPresentInfoKHR *i)
{
    (void)q; (void)i; return VK_SUCCESS;
}

static struct aps5_timed_swapchain maximum_fixture(void)
{
    struct aps5_timed_swapchain s = {0};
    s.width = s.height = UINT_MAX; s.identity = UINT64_MAX;
    s.stats.count = s.stats.last_ns = s.stats.refresh_ns = s.stats.missed_vblanks = UINT64_MAX;
    s.stats.used = UINT_MAX;
    for (unsigned i = 0; i < APS5_HISTOGRAM_BINS; ++i) s.stats.histogram[i] = UINT64_MAX;
    atomic_store(&aps5_display_count, UINT64_MAX); aps5_display_epoch = UINT64_MAX; aps5_display_active = 1;
    for (unsigned i = 0; i < 4; ++i) {
        atomic_store(&aps5_phases[i].calls, UINT64_MAX);
        atomic_store(&aps5_phases[i].ns, UINT64_MAX);
        atomic_store(&aps5_phases[i].max_ns, UINT64_MAX);
    }
    return s;
}
struct noise_context { unsigned identity; const char *path; };
static void *noise(void *argument)
{
    struct noise_context *context = argument;
    int other = open(context->path, O_WRONLY | O_APPEND); assert(other >= 0);
    for (unsigned i = 0; i < 1000; ++i) {
        char bytes[96];
        int length = snprintf(bytes, sizeof(bytes), "[raw-noise] writer=%u row=%u\n", context->identity, i);
        assert(length > 0 && (size_t)length < sizeof(bytes));
        assert(write(STDERR_FILENO, bytes, (size_t)length) == length);
        assert(dprintf(other, "[dprintf-noise] writer=%u row=%u\n", context->identity, i) > 0);
    }
    assert(close(other) == 0); return NULL;
}
static void gate_fixture(void)
{
    VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; VkDevice device;
    assert(aps5_timing_create_device(VK_NULL_HANDLE, &di, NULL, &device) == VK_SUCCESS);
    aps5_timing_set_active(1);
    VkSwapchainCreateInfoKHR si = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .imageExtent = {1280, 720}};
    VkSwapchainKHR swapchain;
    assert(aps5_timing_create_swapchain(device, &si, NULL, &swapchain) == VK_SUCCESS);
    uint32_t index = 0;
    VkPresentInfoKHR pi = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .swapchainCount = 1, .pSwapchains = &swapchain, .pImageIndices = &index};
    for (unsigned i = 0; i < 12; ++i) {
        fake_ns += 1100000000;
        assert(aps5_timing_present(VK_NULL_HANDLE, &pi) == VK_SUCCESS);
    }
    aps5_timing_destroy_swapchain(device, swapchain, NULL);
    aps5_timing_destroy_device(device);
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    int fd = open(argv[2], O_WRONLY | O_CREAT | O_EXCL | O_APPEND, 0600); assert(fd >= 0);
    assert(dup2(fd, STDERR_FILENO) == STDERR_FILENO); assert(close(fd) == 0);
    struct stat st; assert(fstat(STDERR_FILENO, &st) == 0 && S_ISREG(st.st_mode));
    assert(fcntl(STDERR_FILENO, F_GETFL) & O_APPEND);
    if (!strcmp(argv[1], "gate")) gate_fixture();
    else if (!strcmp(argv[1], "max") || !strcmp(argv[1], "concurrent")) {
        struct aps5_timed_swapchain s = maximum_fixture();
        pthread_t writers[4]; struct noise_context contexts[4];
        unsigned rows = !strcmp(argv[1], "concurrent") ? 1000 : 1;
        if (rows > 1) for (unsigned i = 0; i < 4; ++i) {
            contexts[i] = (struct noise_context){i, argv[2]};
            assert(pthread_create(&writers[i], NULL, noise, &contexts[i]) == 0);
        }
        for (unsigned i = 0; i < rows; ++i) aps5_report_telemetry(&s, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX);
        if (rows > 1) for (unsigned i = 0; i < 4; ++i) assert(pthread_join(writers[i], NULL) == 0);
        assert(atomic_load(&report_writes) == 2 * rows);
    } else {
        struct aps5_report_line line = {.valid = 1};
        aps5_report_append(&line, "[anyps5-display] {\"schema\":1,\"partial_fixture\":true}\n");
        if (!strcmp(argv[1], "eintr")) { fault_mode = 1; assert(aps5_report_emit(&line)); assert(atomic_load(&report_writes) == 3); }
        else if (!strcmp(argv[1], "short")) { fault_mode = 2; assert(!aps5_report_emit(&line)); assert(atomic_load(&report_writes) == 1); }
        else if (!strcmp(argv[1], "error")) { fault_mode = 3; assert(!aps5_report_emit(&line)); assert(atomic_load(&report_writes) == 1); }
        else if (!strcmp(argv[1], "overflow")) {
            aps5_report_append(&line, "%*s", APS5_REPORT_LINE_CAPACITY, "");
            assert(!line.valid && !aps5_report_emit(&line) && !atomic_load(&report_writes));
        } else assert(0);
    }
    printf("{\"fixture\":\"%s\",\"write_calls\":%u,\"longest_row_bytes\":%zu,\"capacity_bytes\":%u}\n", argv[1], atomic_load(&report_writes), longest_row, APS5_REPORT_LINE_CAPACITY);
}
