/* Read-only diagnostics of this process's actual native Mach VM map.
 * No reservation, protection, task entitlement or JIT state is changed.
 * Run on a background queue: a live map is non-atomic, and an apparent gap
 * does not establish that a future fixed guest allocation is safe.
 *
 * int aps5_runtime_snapshot(path, phase, context_json)
 * Returns 0 for a complete walk, 1 for a partial walk, or a positive errno for
 * an I/O/busy error. context_json is retained as an escaped JSON STRING named
 * caller_context_json. This keeps the report valid even for malformed input;
 * callers normally provide JSONSerialization output of known build/JIT flags.
 * Bounds: 8192 leaves,32768 queries,depth64,1s query budget,4MiB/snapshot,
 * 32MiB log. An owned log is truncated before the next snapshot if necessary.
 * Non-log files and symlinks are refused. All failures remain explicit.
 */
#include <mach/mach.h>
#include <mach/vm_region.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* The iPhoneOS SDK deliberately rejects <mach/mach_vm.h>, although libSystem
 * exports this Mach call. This is its 64-bit MIG signature, also used on macOS.
 */
extern kern_return_t mach_vm_region_recurse(vm_map_t, mach_vm_address_t *, mach_vm_size_t *,
                                           natural_t *, vm_region_recurse_info_t, mach_msg_type_number_t *);

enum { MAX_LEAVES = 8192, MAX_QUERIES = 32768, MAX_DEPTH = 64,
       SNAPSHOT_BYTES = 4 * 1024 * 1024, LOG_BYTES = 32 * 1024 * 1024, MAX_CONTEXT = 8192 };
static pthread_mutex_t snapshot_lock = PTHREAD_MUTEX_INITIALIZER;
static const char record_prefix[] = "{\"schema\":1,\"probe\":\"native_runtime_va\"";

static uint64_t monotonic_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static void quoted(FILE *stream, const char *value, size_t maximum) {
    if (!value) { fputs("null", stream); return; }
    fputc('"', stream);
    for (size_t i = 0; i < maximum && value[i]; ++i) {
        const unsigned char ch = (unsigned char)value[i];
        if (ch == '"' || ch == '\\') { fputc('\\', stream); fputc(ch, stream); }
        else if (ch < 32 || ch >= 127) fprintf(stream, "\\u%04x", ch);
        else fputc(ch, stream);
    }
    fputc('"', stream);
}

static void record(FILE *stream, uint64_t snapshot, const char *kind) {
    fprintf(stream, "%s,\"snapshot_id\":\"%d-%" PRIu64 "\",\"kind\":\"%s\"", record_prefix, getpid(), snapshot, kind);
}

static void sysctl_text(FILE *stream, const char *field, const char *key) {
    char value[128] = {0};
    size_t bytes = sizeof(value);
    int status = sysctlbyname(key, value, &bytes, NULL, 0);
    value[sizeof(value) - 1] = 0;
    fprintf(stream, ",\"%s\":", field);
    quoted(stream, status == 0 ? value : NULL, sizeof(value) - 1);
}

static void task_metrics(FILE *stream, uint64_t snapshot, const char *when) {
    task_vm_info_data_t info = {0};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    kern_return_t status = task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count);
    record(stream, snapshot, "task_memory");
    fprintf(stream, ",\"when\":\"%s\",\"kern_return\":%d,\"returned_words\":%u", when, status, count);
    if (status == KERN_SUCCESS) {
        const size_t bytes = (size_t)count * sizeof(natural_t);
#define METRIC(name) do { \
        fprintf(stream, ",\"" #name "\":"); \
        if (bytes >= offsetof(task_vm_info_data_t, name) + sizeof(info.name)) \
            fprintf(stream, "%" PRIu64, (uint64_t)info.name); \
        else fputs("null", stream); \
    } while (0)
        METRIC(virtual_size); METRIC(region_count); METRIC(page_size);
        METRIC(resident_size); METRIC(resident_size_peak); METRIC(phys_footprint);
        METRIC(internal); METRIC(external); METRIC(reusable); METRIC(compressed);
        METRIC(purgeable_volatile_resident); METRIC(ledger_phys_footprint_peak); METRIC(limit_bytes_remaining);
#undef METRIC
    }
    fputs("}\n", stream);
}

int aps5_runtime_snapshot(const char *path, const char *phase, const char *context_json) {
    if (!path || !*path || !phase) return EINVAL;
    const int locked = pthread_mutex_trylock(&snapshot_lock);
    if (locked) return locked;
    int result = 0;
    int fd = open(path, O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    FILE *stream = NULL;
    if (fd < 0) { result = errno; goto done; }
    struct stat file;
    if (fstat(fd, &file)) { result = errno; goto done; }
    if (!S_ISREG(file.st_mode)) { result = EINVAL; goto done; }
    if (flock(fd, LOCK_EX | LOCK_NB)) { result = errno; goto done; }
    if (file.st_size) {
        char prefix[sizeof(record_prefix) - 1];
        if (pread(fd, prefix, sizeof(prefix), 0) != (ssize_t)sizeof(prefix) || memcmp(prefix, record_prefix, sizeof(prefix))) {
            result = EINVAL; goto done;
        }
    }
    const int rotated = file.st_size > LOG_BYTES - SNAPSHOT_BYTES;
    if (rotated && ftruncate(fd, 0)) { result = errno; goto done; }
    stream = fdopen(fd, "a");
    if (!stream) { result = errno; goto done; }
    fd = -1; /* stream now owns the locked descriptor */
    const off_t start_offset = rotated ? 0 : file.st_size;
    const uint64_t started = monotonic_ns();
    const uint64_t snapshot = started;
    struct timespec wall = {0};
    clock_gettime(CLOCK_REALTIME, &wall);
    record(stream, snapshot, "context");
    fprintf(stream, ",\"pid\":%d,\"timestamp_unix_seconds\":%lld,\"timestamp_nanoseconds\":%ld,\"phase\":",
            getpid(), (long long)wall.tv_sec, wall.tv_nsec);
    quoted(stream, phase, 128);
    const int oversized_context = context_json && strnlen(context_json, MAX_CONTEXT + 1) > MAX_CONTEXT;
    fputs(",\"caller_context_json\":", stream);
    quoted(stream, oversized_context ? NULL : context_json, MAX_CONTEXT);
    fprintf(stream, ",\"context_oversized\":%s,\"log_rotated\":%s,\"native_page_size\":%" PRIu64 ",\"sysconf_page_size\":%ld",
            oversized_context ? "true" : "false", rotated ? "true" : "false", (uint64_t)vm_page_size, sysconf(_SC_PAGESIZE));
    sysctl_text(stream, "hardware_model", "hw.machine");
    sysctl_text(stream, "os_product_version", "kern.osproductversion");
    sysctl_text(stream, "os_build", "kern.osversion");
    uint64_t memory = 0; size_t memory_bytes = sizeof(memory);
    fputs(",\"physical_memory_bytes\":", stream);
    if (sysctlbyname("hw.memsize", &memory, &memory_bytes, NULL, 0) == 0) fprintf(stream, "%" PRIu64, memory);
    else fputs("null", stream);
    fputs(",\"map_consistency\":\"live_non_atomic\",\"scope\":\"native_self_map_only; no gap or map limit proves guest allocation safety\"}\n", stream);
    task_metrics(stream, snapshot, "before");

    mach_vm_address_t address = 0;
    natural_t depth = 0;
    unsigned leaves = 0, queries = 0;
    uint64_t mapped_bytes = 0;
    const char *termination = "query_limit";
    kern_return_t last_status = KERN_SUCCESS;
    int complete = 0;
    while (queries < MAX_QUERIES) {
        if (leaves >= MAX_LEAVES) { termination = "region_limit"; break; }
        const uint64_t now = monotonic_ns();
        if (!started || !now || now - started > UINT64_C(1000000000)) { termination = "time_limit_or_clock_error"; break; }
        if (ftello(stream) - start_offset > SNAPSHOT_BYTES - 4096) { termination = "byte_limit"; break; }
        vm_region_submap_info_data_64_t info = {0};
        mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;
        mach_vm_size_t size = 0;
        const mach_vm_address_t requested = address;
        last_status = mach_vm_region_recurse(mach_task_self(), &address, &size, &depth,
                                             (vm_region_recurse_info_t)&info, &count);
        ++queries;
        if (last_status == KERN_INVALID_ADDRESS) { termination = "end_of_map"; complete = 1; break; }
        if (last_status != KERN_SUCCESS) { termination = "query_failed"; break; }
        if (address < requested || !size || size > UINT64_MAX - address) { termination = "invalid_region_progress"; break; }
        if (info.is_submap) {
            if (depth >= MAX_DEPTH) { termination = "depth_limit"; break; }
            ++depth;
            continue;
        }
        record(stream, snapshot, "region");
        fprintf(stream, ",\"start\":\"0x%" PRIx64 "\",\"end\":\"0x%" PRIx64 "\",\"size_bytes\":%" PRIu64
                ",\"depth\":%u,\"protection\":%d,\"max_protection\":%d,\"inheritance\":%d,\"user_tag\":%u"
                ",\"share_mode\":%u,\"pages_resident\":%u,\"pages_swapped_out\":%u,\"pages_dirtied\":%u}\n",
                (uint64_t)address, (uint64_t)(address + size), (uint64_t)size, depth, info.protection,
                info.max_protection, info.inheritance, info.user_tag, (unsigned)info.share_mode,
                info.pages_resident, info.pages_swapped_out, info.pages_dirtied);
        ++leaves;
        if (size <= UINT64_MAX - mapped_bytes) mapped_bytes += size;
        address += size;
        if (ferror(stream)) { termination = "write_failed"; break; }
    }
    task_metrics(stream, snapshot, "after");
    record(stream, snapshot, "summary");
    fprintf(stream, ",\"status\":\"%s\",\"termination\":\"%s\",\"kern_return\":%d,\"regions\":%u,\"queries\":%u"
            ",\"mapped_leaf_bytes\":%" PRIu64 ",\"elapsed_ns\":%" PRIu64 ",\"guest_arena_safety_verified\":false}\n",
            complete ? "complete" : "partial", termination, last_status, leaves, queries, mapped_bytes, monotonic_ns() - started);
    result = complete ? 0 : 1;
    if (ferror(stream)) result = EIO;
done:
    if (stream) { if (fclose(stream)) result = EIO; }
    if (fd >= 0) close(fd);
    pthread_mutex_unlock(&snapshot_lock);
    return result;
}

#ifdef APS5_RUNTIME_DIAGNOSTICS_CLI
int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) return 64;
    return aps5_runtime_snapshot(argv[1], "host_readonly_validation", argc == 3 ? argv[2] : "{\"runtime\":\"native_host_test\"}");
}
#endif
