/* Slim stand-in for libdxmt_combined.a.
 *
 * The Madeira app calls these symbols from Swift. Their real definitions
 * live in DXMT (winemetal_unix.c) and madeira-d3d12, which the slim
 * Vulkan path does not build and which need Madeira's llvm-ios toolchain.
 * The functions return empty results. They are not a D3D or Metal driver.
 */
#include <stdint.h>

uint64_t madeira_get_present_count(void) { return 0; }

void madeira_gpu_meter_enable(int on) { (void)on; }

double madeira_gpu_meter_busy_seconds(void) { return 0.0; }

uint64_t madeira_gpu_meter_cmdbufs(void) { return 0; }

void madeira_capture_request(int frames) { (void)frames; }

void madeira_set_fence_mode(int mode) { (void)mode; }

void madeira_set_vsync_locked(int locked) { (void)locked; }

int madeira_get_vsync_locked(void) { return 0; }

int madeira_d3d12_canary_run(const char *fixture_dir, const char *dylib_path,
                             void (*sink)(const char *))
{
    (void)fixture_dir;
    (void)dylib_path;
    (void)sink;
    return 0;
}

int madeira_d3d12_canary_run_log(const char *fixture_dir, const char *dylib_path,
                                 void (*sink)(const char *), const char *log_path,
                                 const char *build_id)
{
    (void)fixture_dir;
    (void)dylib_path;
    (void)sink;
    (void)log_path;
    (void)build_id;
    return 0;
}
