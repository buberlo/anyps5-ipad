/* Synthetic samples only. Actual mixer and old/new render callback source is
 * included by the checker, with Core Audio declaration mocks, not a device. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static _Atomic unsigned checks, allocations, frees, writes;
static _Thread_local int in_render;
static int write_mode;
static unsigned write_attempts, fail_allocation;
#define CHECK(x) do { atomic_fetch_add(&checks, 1); assert(x); } while (0)
static void *checked_calloc(size_t n, size_t size)
{
    CHECK(!in_render);
    atomic_fetch_add(&allocations, 1);
    if (fail_allocation && !--fail_allocation) return NULL;
    return calloc(n, size);
}
static void checked_free(void *p)
{
    CHECK(!in_render);
    atomic_fetch_add(&frees, 1);
    free(p);
}
static ssize_t checked_write(int fd, const void *data, size_t count)
{
    CHECK(!in_render);
    atomic_fetch_add(&writes, 1);
    write_attempts++;
    if (write_mode == 1) {
        if (write_attempts == 1) { errno = EINTR; return -1; }
        if (count > 7) count = 7;
    }
    if (write_mode == 2) {
        if (write_attempts == 1) return write(fd, data, count < 5 ? count : 5);
        errno = ENOSPC; return -1;
    }
    if (write_mode == 3) return 0;
    if (write_mode == 4) { errno = EIO; return -1; }
    return write(fd, data, count);
}
#define calloc checked_calloc
#define free checked_free
#define APS5_POSTMIX_WRITE checked_write
#include "aps5_audio_postmix_capture.h"
#undef calloc
#undef free

typedef uint32_t UINT32;
typedef uint32_t UInt32;
typedef unsigned char BYTE;
typedef void *HANDLE;
typedef void *AudioUnit;
typedef int32_t OSStatus;
typedef uint32_t AudioUnitRenderActionFlags;
typedef struct { double mSampleTime; uint64_t mHostTime; uint32_t mFlags; } AudioTimeStamp;
typedef struct { uint32_t mNumberChannels, mDataByteSize; void *mData; } AudioBuffer;
typedef struct { uint32_t mNumberBuffers; AudioBuffer mBuffers[1]; } AudioBufferList;
enum { noErr = 0, kAudioTimeStampSampleTimeValid = 1, kAudioTimeStampHostTimeValid = 2 };
#define IOS_MAX_STREAMS 16
#include "production_audio_callback.inc"

struct guarded_capture {
    unsigned char front[19];
    float samples[64];
    unsigned char back[19];
    struct aps5_postmix_record records[8];
    struct aps5_postmix_capture capture;
};
static void fixture_init(struct guarded_capture *g, uint32_t frames)
{
    memset(g, 0, sizeof(*g));
    memset(g->front, 0x65, sizeof(g->front));
    memset(g->back, 0x76, sizeof(g->back));
    for (size_t i = 0; i < 64; i++) g->samples[i] = 1234.5f;
    g->capture.enabled = 1;
    atomic_init(&g->capture.state, APS5_POSTMIX_ARMED);
    atomic_init(&g->capture.writer, 0);
    atomic_init(&g->capture.contended, 0);
    g->capture.samples = g->samples;
    g->capture.records = g->records;
    g->capture.capacity = frames;
    g->capture.record_capacity = 8;
    g->capture.directory_fd = -1;
    g->capture.timebase_numer = 125;
    g->capture.timebase_denom = 3;
}
static void guards(const struct guarded_capture *g)
{
    for (size_t i = 0; i < 19; i++) CHECK(g->front[i] == 0x65 && g->back[i] == 0x76);
    for (size_t i = (size_t)g->capture.frames * 2; i < 64; i++) CHECK(g->samples[i] == 1234.5f);
}
static void seed_streams(struct ios_stream stream[2], const float *a, const float *b, unsigned frames)
{
    memset(stream, 0, sizeof(*stream) * 2);
    memset(&g_dev, 0, sizeof(g_dev));
    g_dev.rate = 48000; g_dev.channels = 2;
    for (unsigned i = 0; i < IOS_MAX_STREAMS; i++) atomic_store(&g_mix[i], NULL);
    for (unsigned i = 0; i < 2; i++) {
        stream[i].started = stream[i].mixable = stream[i].is_float = 1;
        stream[i].sample_bits = 32;
        stream[i].sample_rate = 48000;
        stream[i].channels = 2;
        stream[i].frame_bytes = 8;
        stream[i].buffer_frames = frames;
        stream[i].ring = (BYTE *)(i ? b : a);
        stream[i].mix_gain[0][0] = 1;
        stream[i].mix_gain[1][1] = 1;
        atomic_store(&stream[i].write_pos, frames);
        atomic_store(&g_mix[i], &stream[i]);
    }
}
static void parity(void)
{
    float a[32], b[32], old[35], current[35];
    struct ios_stream stream[2];
    struct guarded_capture g;
    AudioUnitRenderActionFlags flags = 8;
    AudioTimeStamp ts = {1024.25, 987654321, 3};
    for (unsigned i = 0; i < 32; i++) {
        a[i] = (i % 8 == 0) ? 1.25f : (i % 8 == 1) ? -1.5f : (float)(i % 5) / 8.0f;
        b[i] = 0.25f;
    }
    for (unsigned bytes = 0; bytes <= 35 * sizeof(float); bytes++) {
        memset(old, 0xab, sizeof(old)); memset(current, 0xab, sizeof(current));
        seed_streams(stream, a, b, 16);
        memset(&g_audio_postmix_capture, 0, sizeof(g_audio_postmix_capture));
        AudioBufferList io = {1, {{2, bytes, old}}};
        in_render = 1;
        CHECK(ios_audio_render_cb_before(NULL, &flags, &ts, 0, 20, &io) == noErr);
        in_render = 0;
        uint64_t old_clamps = atomic_load(&g_dev.clamped), old_epoch = atomic_load(&g_dev.cb_epoch);
        uint64_t old_pos[2] = {atomic_load(&stream[0].play_pos), atomic_load(&stream[1].play_pos)};
        seed_streams(stream, a, b, 16);
        fixture_init(&g, 16);
        g_audio_postmix_capture = g.capture;
        io.mBuffers[0].mData = current;
        unsigned alloc_before = atomic_load(&allocations), free_before = atomic_load(&frees), writes_before = atomic_load(&writes);
        in_render = 1;
        CHECK(ios_audio_render_cb(NULL, &flags, &ts, 0, 20, &io) == noErr);
        in_render = 0;
        CHECK(!memcmp(old, current, sizeof(old)));
        CHECK(old_clamps == atomic_load(&g_dev.clamped) && old_epoch == atomic_load(&g_dev.cb_epoch));
        CHECK(old_pos[0] == atomic_load(&stream[0].play_pos) && old_pos[1] == atomic_load(&stream[1].play_pos));
        CHECK(alloc_before == atomic_load(&allocations) && free_before == atomic_load(&frees) && writes_before == atomic_load(&writes));
        unsigned expected = bytes / 8 < 16 ? bytes / 8 : 16;
        CHECK(g_audio_postmix_capture.frames == expected);
        CHECK(!memcmp(g.samples, current, expected * 8));
        if (expected) {
            const struct aps5_postmix_record *record = &g.records[0];
            CHECK(record->stamp.epoch == 1 && record->stamp.requested_frames == 20);
            CHECK(record->stamp.sample_time == 1024.25 && record->stamp.host_ticks == 987654321);
            CHECK(record->stamp.host_time_valid && record->stamp.sample_time_valid && record->stamp.action_flags == 8);
        }
        g.capture.frames = g_audio_postmix_capture.frames;
        guards(&g);
    }
    /* Explicit independent post-sum clamp oracle on the actual new callback. */
    seed_streams(stream, a, b, 16); fixture_init(&g, 16); g_audio_postmix_capture = g.capture;
    AudioBufferList io = {1, {{2, sizeof(current), current}}};
    ios_audio_render_cb(NULL, NULL, NULL, 0, 16, &io);
    for (unsigned i = 0; i < 32; i++) {
        float expected = a[i] + b[i];
        if (expected > 1) expected = 1;
        if (expected < -1) expected = -1;
        CHECK(!memcmp(current + i, &expected, 4));
    }
    CHECK(!g.records[0].stamp.host_time_valid && !g.records[0].stamp.sample_time_valid);
    io.mBuffers[0].mData = NULL; CHECK(ios_audio_render_cb(NULL, NULL, NULL, 0, 16, &io) == noErr);
    io.mNumberBuffers = 0; CHECK(ios_audio_render_cb(NULL, NULL, NULL, 0, 16, &io) == noErr);
    CHECK(ios_audio_render_cb(NULL, NULL, NULL, 0, 16, NULL) == noErr);
}
static void controls(void)
{
    struct guarded_capture g;
    float source[32];
    struct aps5_postmix_stamp ts = {9, 456, 100.5, 3, 0, 16, 1, 1};
    uint32_t count;
    for (unsigned i = 0; i < 32; i++) source[i] = (float)i / 32.0f;
    CHECK(aps5_postmix_parse_skip(NULL, &count) && !count);
    CHECK(aps5_postmix_parse_skip("28800000", &count) && count == APS5_POSTMIX_MAX_SKIP);
    for (unsigned i = 0; i < 7; i++) {
        const char *bad[] = {"", "-1", "+1", "1x", "28800001", "4294967295", "99999999999999999999"};
        CHECK(!aps5_postmix_parse_skip(bad[i], &count));
    }
    fixture_init(&g, 5); g.capture.skip_remaining = 3;
    aps5_postmix_record(&g.capture, source, 2, 48000, 2, &ts);
    CHECK(!g.capture.frames && !g.capture.record_count && g.capture.skip_remaining == 1);
    aps5_postmix_record(&g.capture, source + 4, 10, 48000, 2, &ts);
    CHECK(g.capture.frames == 5 && g.capture.record_count == 1 && g.records[0].skipped_frames == 1);
    CHECK(!memcmp(g.samples, source + 6, 40));
    CHECK(atomic_load(&g.capture.state) == APS5_POSTMIX_DONE);
    aps5_postmix_record(&g.capture, source, 16, 48000, 2, &ts); CHECK(g.capture.frames == 5); guards(&g);
    for (unsigned kind = 0; kind < 4; kind++) {
        fixture_init(&g, 8);
        aps5_postmix_record(&g.capture, source, 2, 48000, 2, &ts);
        aps5_postmix_record(&g.capture, kind == 0 ? NULL : source, 2,
                            kind == 1 ? 44100 : 48000, kind == 2 ? 1 : kind == 3 ? 6 : 2, &ts);
        CHECK(g.capture.frames == 2 && g.capture.reason == APS5_POSTMIX_BAD_CALLBACK);
        CHECK(atomic_load(&g.capture.state) == APS5_POSTMIX_DONE); guards(&g);
    }
    fixture_init(&g, 8); g.capture.record_capacity = 1;
    aps5_postmix_record(&g.capture, source, 1, 48000, 2, &ts);
    aps5_postmix_record(&g.capture, source + 2, 1, 48000, 2, &ts);
    CHECK(g.capture.frames == 1 && g.capture.reason == APS5_POSTMIX_RECORD_LIMIT); guards(&g);
    fixture_init(&g, 8); atomic_store(&g.capture.writer, 1);
    CHECK(!aps5_postmix_cancel(&g.capture));
    aps5_postmix_record(&g.capture, source, 1, 48000, 2, &ts);
    CHECK(!g.capture.frames && atomic_load(&g.capture.contended));
    atomic_store(&g.capture.writer, 0);
    aps5_postmix_record(&g.capture, source, 1, 48000, 2, &ts);
    CHECK(g.capture.reason == APS5_POSTMIX_CONTENTION && atomic_load(&g.capture.state) == APS5_POSTMIX_DONE);
    fixture_init(&g, 8); CHECK(aps5_postmix_cancel(&g.capture));
    CHECK(g.capture.reason == APS5_POSTMIX_TIMEOUT && !g.capture.frames); guards(&g);
    /* A contender paused before its flag store is still counted. Production
     * finalization must reject "complete" before that contender resumes. */
    fixture_init(&g, 8); atomic_store(&g.capture.writer, 2);
    g.capture.reason = APS5_POSTMIX_COMPLETE;
    aps5_postmix_publish_done(&g.capture);
    CHECK(g.capture.reason == APS5_POSTMIX_CONTENTION);
    CHECK(atomic_load(&g.capture.state) == APS5_POSTMIX_DONE);
    atomic_store(&g.capture.contended, 1);
    atomic_fetch_sub(&g.capture.writer, 2); /* both admitted participants leave */
    CHECK(atomic_load(&g.capture.writer) == APS5_POSTMIX_CLOSED);
    aps5_postmix_record(&g.capture, source, 1, 48000, 2, &ts);
    CHECK(!g.capture.frames && !g.capture.record_count); guards(&g);
    /* Exercise CLOSED admission itself while another participant is still
     * counted, independently of the earlier DONE state gate. */
    fixture_init(&g, 8); atomic_store(&g.capture.writer, APS5_POSTMIX_CLOSED | 1u);
    aps5_postmix_record(&g.capture, source, 1, 48000, 2, &ts);
    CHECK(atomic_load(&g.capture.writer) == (APS5_POSTMIX_CLOSED | 1u));
    CHECK(!g.capture.frames && !g.capture.record_count && !atomic_load(&g.capture.contended));
    guards(&g);
}
static unsigned char *read_file(int dir, const char *name, size_t *length)
{
    int fd = openat(dir, name, O_RDONLY); struct stat st;
    CHECK(fd >= 0 && !fstat(fd, &st)); *length = (size_t)st.st_size;
    unsigned char *bytes = malloc(*length + 1); CHECK(bytes);
    size_t done = 0;
    while (done < *length) { ssize_t got = read(fd, bytes + done, *length - done); CHECK(got > 0); done += (size_t)got; }
    bytes[*length] = 0; CHECK(!close(fd)); return bytes;
}
static void exports(const char *directory)
{
    struct guarded_capture g; float samples[16];
    struct aps5_postmix_stamp ts = {12, 998, NAN, 3, 1, 8, 1, 1};
    int dir = open(directory, O_RDONLY | O_DIRECTORY); CHECK(dir >= 0);
    for (unsigned i = 0; i < 16; i++) samples[i] = (float)i / 16.0f;
    fixture_init(&g, 8); g.capture.directory_fd = dir;
    aps5_postmix_record(&g.capture, samples, 8, 48000, 2, &ts);
    for (unsigned mode = 0; mode <= 4; mode++) {
        snprintf(g.capture.stem, sizeof(g.capture.stem), "export-%u", mode);
        write_mode = (int)mode; write_attempts = 0;
        int ok = aps5_postmix_export(&g.capture); CHECK(ok == (mode <= 1));
        char name[112]; snprintf(name, sizeof(name), "export-%u.wav", mode);
        if (ok) {
            size_t length; unsigned char *bytes = read_file(dir, name, &length);
            CHECK(length == 58 + sizeof(samples) && !memcmp(bytes + 58, samples, sizeof(samples)));
            CHECK(!memcmp(bytes, "RIFF", 4) && bytes[20] == 3 && bytes[22] == 2);
            CHECK(bytes[46] == 8 && !memcmp(bytes + 50, "data", 4));
            free(bytes);
            CHECK(!aps5_postmix_export(&g.capture)); /* O_EXCL: no overwrite. */
            snprintf(name, sizeof(name), "export-%u.json", mode);
            bytes = read_file(dir, name, &length);
            CHECK(strstr((char *)bytes, "\"sample_time\":null") && strstr((char *)bytes, "\"captured_frames\":8"));
            free(bytes);
        } else {
            CHECK(faccessat(dir, name, F_OK, 0) && errno == ENOENT);
            snprintf(name, sizeof(name), "export-%u.json", mode);
            CHECK(faccessat(dir, name, F_OK, 0) && errno == ENOENT);
        }
    }
    write_mode = 0; strcpy(g.capture.stem, "metadata-collision");
    int fd = openat(dir, "metadata-collision.json", O_WRONLY | O_CREAT | O_EXCL, 0600);
    CHECK(fd >= 0 && write(fd, "existing-private", 16) == 16 && !close(fd));
    CHECK(!aps5_postmix_export(&g.capture));
    CHECK(faccessat(dir, "metadata-collision.wav", F_OK, 0) && errno == ENOENT);
    size_t length; unsigned char *bytes = read_file(dir, "metadata-collision.json", &length);
    CHECK(length == 16 && !memcmp(bytes, "existing-private", 16)); free(bytes);
    g.capture.directory_fd = -1; strcpy(g.capture.stem, "closed-dir"); CHECK(!aps5_postmix_export(&g.capture));
    CHECK(!close(dir));
}
static void gate_and_worker(const char *directory)
{
    struct aps5_postmix_capture capture = {0};
    const char *flag = getenv("MADEIRA_AUDIO_CAPTURE_POSTMIX");
    unsigned alloc_before = atomic_load(&allocations);
    int enabled = aps5_postmix_begin(&capture, flag, NULL, directory, 48000, 2, 125, 3);
    CHECK(enabled == (flag && !strcmp(flag, "1")));
    if (!enabled) {
        CHECK(!capture.enabled && !capture.samples && !capture.records);
        CHECK(alloc_before == atomic_load(&allocations)); return;
    }
    float frames[2048];
    for (unsigned i = 0; i < 2048; i++) frames[i] = (float)(i % 127) / 128.0f;
    struct aps5_postmix_stamp ts = {0, 12345, 0, 3, 0, 1024, 1, 1};
    for (unsigned i = 0; i < 469; i++) {
        ts.epoch++; ts.host_ticks += 1000; ts.sample_time += 1024;
        in_render = 1; aps5_postmix_record(&capture, frames, 1024, 48000, 2, &ts); in_render = 0;
    }
    for (unsigned i = 0; i < 400 && atomic_load_explicit(&capture.state, memory_order_acquire) == APS5_POSTMIX_DONE; i++) {
        struct timespec pause = {0, 25000000}; nanosleep(&pause, NULL);
    }
    CHECK(atomic_load_explicit(&capture.state, memory_order_acquire) == APS5_POSTMIX_EXPORTED);
    CHECK(capture.frames == APS5_POSTMIX_FRAMES && capture.record_count == 469);
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    parity(); controls(); exports(argv[1]); gate_and_worker(argv[1]);
    struct aps5_postmix_capture rejected = {0};
    CHECK(aps5_postmix_begin(&rejected, "1", NULL, argv[1], 44100, 2, 1, 1) == -1);
    CHECK(aps5_postmix_begin(&rejected, "1", NULL, argv[1], 48000, 1, 1, 1) == -1);
    CHECK(aps5_postmix_begin(&rejected, "1", "-1", argv[1], 48000, 2, 1, 1) == -1);
    CHECK(aps5_postmix_begin(&rejected, "1", NULL, NULL, 48000, 2, 1, 1) == -1);
    for (unsigned failure = 1; failure <= 2; failure++) {
        memset(&rejected, 0, sizeof(rejected)); fail_allocation = failure;
        unsigned free_before = atomic_load(&frees);
        CHECK(aps5_postmix_begin(&rejected, "1", NULL, argv[1], 48000, 2, 1, 1) == -1);
        CHECK(!rejected.enabled && !rejected.samples && !rejected.records);
        CHECK(atomic_load(&rejected.state) == APS5_POSTMIX_OFF);
        CHECK(atomic_load(&frees) == free_before + 2);
        CHECK(fcntl(rejected.directory_fd, F_GETFD) == -1 && errno == EBADF);
    }
    printf("{\"status\":\"passed\",\"checks\":%u,\"actual_callback_byte_parity\":true,"
           "\"synthetic_coreaudio_declarations\":true,\"no_device\":true}\n", atomic_load(&checks));
    return 0;
}
