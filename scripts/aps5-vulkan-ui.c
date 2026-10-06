/* Madeira UI adapter for the Vulkan-only runtime. No DXMT is linked.
 * Present counts are real winevulkan results. Unsupported D3D diagnostics
 * explicitly fail; GPU busy time remains unavailable, never a synthetic FPS. */
#include <stdint.h>
#include <stdio.h>
extern uint64_t aps5_vulkan_present_count(void);
uint64_t madeira_get_present_count(void) { return aps5_vulkan_present_count(); }
void madeira_gpu_meter_enable(int on) { (void)on; }
double madeira_gpu_meter_busy_seconds(void) { return -1.0; }
uint64_t madeira_gpu_meter_cmdbufs(void) { return 0; }
void madeira_capture_request(int frames) {
    (void)frames;
    fputs("[anyps5] D3D frame capture unavailable in Vulkan runtime\n", stderr);
}
void madeira_set_fence_mode(int mode) { (void)mode; }
void madeira_set_vsync_locked(int locked) {
    (void)locked;
    fputs("[anyps5] Vulkan present mode is controlled by the guest swapchain\n", stderr);
}
int madeira_get_vsync_locked(void) { return 1; }
int madeira_d3d12_canary_run(const char *fixture, const char *dylib, void (*sink)(const char *)) {
    (void)fixture; (void)dylib;
    const char *message = "D3D12 canary unavailable: this build uses Vulkan/MoltenVK";
    if (sink) sink(message);
    fprintf(stderr, "[anyps5] %s\n", message);
    return 1;
}
int madeira_d3d12_canary_run_log(const char *fixture, const char *dylib,
                               void (*sink)(const char *), const char *log_path,
                               const char *build_id) {
    (void)build_id;
    if (log_path) {
        FILE *log = fopen(log_path, "w");
        if (log) { fputs("UNSUPPORTED: D3D12 canary in Vulkan-only runtime\n", log); fclose(log); }
    }
    return madeira_d3d12_canary_run(fixture, dylib, sink);
}
