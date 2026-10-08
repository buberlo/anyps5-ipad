/* Original synthetic source bytes. Includes the actual before/after native
 * ReleaseBuffer and render callbacks, with explicit Core Audio declarations. */
#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static _Atomic unsigned checks, allocations;
static _Thread_local int in_release, in_render;
#define CHECK(x) do { checks++; assert(x); } while (0)
static void *fixture_calloc(size_t n, size_t size)
{
    CHECK(!in_release && !in_render); allocations++; return calloc(n, size);
}
#define calloc fixture_calloc
#include "aps5_audio_release_trace.h"
#include "aps5_audio_postmix_capture.h"
#undef calloc

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
typedef int NTSTATUS;
#define STATUS_SUCCESS 0
#define S_OK 0
#define AUDCLNT_E_NOT_INITIALIZED 1
#define IOS_MAX_STREAMS 16
struct release_render_buffer_params {
    uint64_t stream; uint32_t written_frames, flags; int32_t result;
};
static time_t fixture_time(time_t *p) { if (p) *p = 0; return 0; }
#define time fixture_time
#include "production_audio_release.inc"
#undef time

static struct aps5_release_format format(void)
{
    const struct aps5_release_format f = {48000, 2, 8, 32, 1, 3, 4800, 4800, 0};
    return f;
}
static struct aps5_release_trace *manual(uint32_t skip)
{
    struct aps5_release_trace *t = calloc(1, sizeof(*t)); CHECK(t);
    CHECK(!pthread_mutex_init(&t->lock, NULL));
    t->requested_skip = skip; t->start_ns = 100; t->deadline_ns = UINT64_MAX;
    t->directory_fd = -1; snprintf(t->stem, sizeof(t->stem), "synthetic");
    return t;
}
static void dispose(struct aps5_release_trace *t)
{
    CHECK(!pthread_mutex_destroy(&t->lock)); free(t);
}
static unsigned char pattern(uint64_t frame, unsigned byte)
{
    return (unsigned char)(frame * 17 + byte * 23 + (frame / 1024) * 5);
}
static uint64_t oracle(uint64_t start, uint32_t frames, uint32_t fb)
{
    uint64_t h = UINT64_C(14695981039346656037);
    for (uint32_t f = 0; f < frames; f++)
        for (uint32_t i = 0; i < fb; i++) h = (h ^ pattern(start + f, i)) * UINT64_C(1099511628211);
    return h;
}
static void push(struct aps5_release_trace *t, uintptr_t key, uint64_t wr,
                 uint32_t frames, const struct aps5_release_format *f)
{
    unsigned char guarded[19 + 4800 * 48 + 19];
    unsigned char *b = guarded + 19;
    struct aps5_release_stamp st = {1000 + wr, 7 + wr / 1024, wr, wr > 1024 ? wr - 1024 : 0, frames, frames, 0};
    memset(guarded, 0xa7, sizeof(guarded));
    CHECK(frames <= 4800);
    for (uint32_t n = 0; n < frames; n++)
        for (uint32_t byte = 0; byte < f->frame_bytes; byte++) b[n * f->frame_bytes + byte] = pattern(wr + n, byte);
    in_release = 1;
    aps5_release_record(t, key, b, (size_t)frames * f->frame_bytes, f, &st);
    in_release = 0;
    for (unsigned i = 0; i < 19; i++) CHECK(guarded[i] == 0xa7 && guarded[sizeof(guarded) - 1 - i] == 0xa7);
    for (uint32_t n = 0; n < frames; n++)
        for (uint32_t byte = 0; byte < f->frame_bytes; byte++) CHECK(b[n * f->frame_bytes + byte] == pattern(wr + n, byte));
}
static void canonical(void)
{
    const unsigned lengths[] = {1, 17, 256, 4096, 4800, 777, 1024, 2};
    struct aps5_release_trace *t = manual(512);
    struct aps5_release_format f = format(); uint64_t wr = 0; unsigned k = 0;
    CHECK(!aps5_release_register(t, 31, &f, 100));
    while (wr < 512 + APS5_RELEASE_FRAMES) {
        uint32_t n = lengths[k++ % 8];
        if (n > 512 + APS5_RELEASE_FRAMES - wr) n = (uint32_t)(512 + APS5_RELEASE_FRAMES - wr);
        push(t, 31, wr, n, &f); wr += n;
    }
    struct aps5_release_stream *s = &t->streams[0];
    CHECK(s->state == APS5_RELEASE_DONE && s->reason == APS5_RELEASE_COMPLETE);
    CHECK(s->captured_frames == 480000 && s->block_count == 470);
    CHECK(s->blocks[0].frames == 512 && s->blocks[0].source_block_phase == 512);
    CHECK(s->blocks[469].frames == 256 && s->blocks[469].source_block_phase == 0);
    uint64_t total = 0, pos = 512;
    for (uint32_t i = 0; i < s->block_count; i++) {
        const struct aps5_release_block *b = &s->blocks[i];
        CHECK(b->source_frame_start == pos && b->raw_write_start == pos);
        CHECK(b->window_frame_start == total && b->bytes == b->frames * 8);
        CHECK(b->source_block_phase == pos % 1024 && b->fnv64 == oracle(pos, b->frames, 8));
        CHECK(b->first_submission <= b->last_submission && b->first_ns <= b->last_ns);
        pos += b->frames; total += b->frames;
    }
    CHECK(total == 480000 && s->blocks[1].fnv64 != s->blocks[2].fnv64);
    const uint32_t saved = s->submission_count;
    push(t, 31, wr, 19, &f); CHECK(s->submission_count == saved);
    dispose(t);
}
static void lifecycle_and_limits(void)
{
    struct aps5_release_trace *t = manual(0); struct aps5_release_format f = format();
    CHECK(!aps5_release_register(t, 11, &f, 100)); push(t, 11, 0, 17, &f);
    aps5_release_close(t, 11, APS5_RELEASE_STOPPED);
    CHECK(t->streams[0].reason == APS5_RELEASE_STOPPED && t->streams[0].blocks[0].frames == 17);
    CHECK(!aps5_release_register(t, 11, &f, 100)); push(t, 11, 0, 3, &f);
    CHECK(t->streams[0].generation == 1 && t->streams[1].generation == 2);
    CHECK(t->streams[0].captured_frames == 17 && t->streams[1].captured_frames == 3);
    aps5_release_close(t, 11, APS5_RELEASE_RESET); CHECK(t->streams[1].reason == APS5_RELEASE_RESET);
    for (uintptr_t key = 12; t->stream_count < 16; key++) CHECK(!aps5_release_register(t, key, &f, 100));
    CHECK(aps5_release_register(t, 999, &f, 100) == -1 && t->stream_count == 16 && t->rejected_creations == 1);
    aps5_release_close(t, 12, APS5_RELEASE_TEARDOWN); CHECK(t->streams[2].reason == APS5_RELEASE_TEARDOWN);
    dispose(t);
    t = manual(0); CHECK(!aps5_release_register(t, 1, &f, 100));
    push(t, 1, 0, 0, &f); CHECK(t->streams[0].zero_submissions == 1 && !t->streams[0].captured_frames);
    push(t, 1, 1, 8, &f); CHECK(t->streams[0].reason == APS5_RELEASE_DISCONTINUITY);
    dispose(t);
    t = manual(0); CHECK(!aps5_release_register(t, 1, &f, 100));
    t->streams[0].submission_count = APS5_RELEASE_SUBMISSIONS;
    push(t, 1, 0, 8, &f); CHECK(t->streams[0].reason == APS5_RELEASE_SUBMISSION_LIMIT);
    dispose(t);
    t = manual(0); f.rate = 44100; CHECK(!aps5_release_register(t, 1, &f, 100));
    CHECK(t->streams[0].reason == APS5_RELEASE_UNSUPPORTED); dispose(t); f = format();
    t = manual(0); CHECK(!aps5_release_register(t, 1, &f, 100));
    struct aps5_release_stamp st = {1001, 1, 0, 0, 2, 2, 0}; unsigned char b[16] = {0};
    aps5_release_record(t, 1, b, 15, &f, &st); CHECK(t->streams[0].reason == APS5_RELEASE_DISCONTINUITY);
    dispose(t);
    t = manual(0); CHECK(!aps5_release_register(t, 1, &f, 100));
    t->deadline_ns = 1001; aps5_release_record(t, 1, b, 16, &f, &st);
    CHECK(t->streams[0].reason == APS5_RELEASE_TIMEOUT); dispose(t);
    t = manual(0); CHECK(!aps5_release_register(t, 1, &f, 100));
    f.channels = 1; aps5_release_record(t, 1, b, 16, &f, &st);
    CHECK(t->streams[0].reason == APS5_RELEASE_UNSUPPORTED); dispose(t);
}
struct thread_args { struct aps5_release_trace *trace; uintptr_t identity; };
static void *parallel_source(void *argument)
{
    const struct thread_args *a = argument;
    struct aps5_release_format f = format();
    for (uint32_t i = 0; i < 40; i++) push(a->trace, a->identity, (uint64_t)i * 127, 127, &f);
    aps5_release_close(a->trace, a->identity, APS5_RELEASE_TEARDOWN);
    return NULL;
}
static void formats_parallel_and_export_errors(const char *directory)
{
    struct aps5_release_trace *t = manual(0);
    const struct aps5_release_format formats[] = {
        {48000, 1, 2, 16, 0, 1, 4800, 4800, 0},
        {48000, 2, 4, 16, 0, 3, 4800, 4800, 1},
        {48000, 8, 32, 32, 0, 255, 4800, 4800, 2},
        {48000, 12, 48, 32, 1, 4095, 4800, 4800, 3}
    };
    for (unsigned i = 0; i < 4; i++) {
        CHECK(!aps5_release_register(t, 100 + i, &formats[i], 100));
        push(t, 100 + i, 0, 1024, &formats[i]);
        CHECK(t->streams[i].blocks[0].fnv64 == oracle(0, 1024, formats[i].frame_bytes));
        aps5_release_close(t, 100 + i, APS5_RELEASE_TEARDOWN);
    }
    dispose(t);
    t = manual(0); struct aps5_release_format f = format();
    CHECK(!aps5_release_register(t, 100, &f, 100)); CHECK(!aps5_release_register(t, 101, &f, 100));
    struct thread_args args[] = {{t, 100}, {t, 101}}; pthread_t threads[2];
    CHECK(!pthread_create(&threads[0], NULL, parallel_source, &args[0]));
    CHECK(!pthread_create(&threads[1], NULL, parallel_source, &args[1]));
    CHECK(!pthread_join(threads[0], NULL)); CHECK(!pthread_join(threads[1], NULL));
    for (unsigned i = 0; i < 2; i++) {
        CHECK(t->streams[i].captured_frames == 5080 && t->streams[i].reason == APS5_RELEASE_TEARDOWN);
        for (unsigned j = 0; j < t->streams[i].block_count; j++) {
            const struct aps5_release_block *b = &t->streams[i].blocks[j];
            CHECK(b->fnv64 == oracle(b->source_frame_start, b->frames, 8));
        }
    }
    CHECK(aps5_release_export(t, &t->streams[0]) == -1); /* invalid private directory */
    t->directory_fd = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW); CHECK(t->directory_fd >= 0);
    CHECK(!aps5_release_export(t, &t->streams[0]));
    CHECK(aps5_release_export(t, &t->streams[0]) == -1); /* exclusive creation, original preserved */
    close(t->directory_fd); dispose(t);
    t = manual(0); CHECK(!aps5_release_register(t, 1, &f, 100));
    t->streams[0].block_count = APS5_RELEASE_BLOCKS;
    push(t, 1, 0, 1024, &f); CHECK(t->streams[0].reason == APS5_RELEASE_BLOCK_LIMIT);
    dispose(t);
}
static void release_and_callback_parity(int enabled, struct aps5_release_trace *trace)
{
    float scratch[4800 * 2], ring_before[4800 * 2], ring_after[4800 * 2];
    struct ios_stream old = {0}, current;
    old.valid = old.started = old.mixable = old.is_float = 1;
    old.sample_bits = 32; old.sample_rate = 48000; old.channels = 2;
    old.frame_bytes = 8; old.buffer_frames = old.scratch_frames = 4800;
    old.render_scratch = (BYTE *)scratch; old.ring = (BYTE *)ring_before;
    old.mix_gain[0][0] = old.mix_gain[1][1] = 1;
    memcpy(&current, &old, sizeof(old)); current.ring = (BYTE *)ring_after;
    memset(ring_before, 0, sizeof(ring_before)); memset(ring_after, 0, sizeof(ring_after));
    memset(&g_dev, 0, sizeof(g_dev)); g_dev.rate = 48000; g_dev.channels = 2;
    for (int i = 0; i < IOS_MAX_STREAMS; i++) atomic_store(&g_mix[i], NULL);
    atomic_store(&g_mix[0], &current);
    atomic_store(&g_audio_release_trace, trace);
    if (enabled) {
        struct aps5_release_format f = ios_release_trace_format(&current);
        CHECK(!aps5_release_register(trace, (uintptr_t)&current, &f, aps5_release_now_ns()));
    }
    for (unsigned pass = 0; pass < 24; pass++) {
        unsigned n = (pass % 5 == 0) ? 0 : (pass % 5 == 1) ? 17 : (pass % 5 == 2) ? 4096 : 1024;
        for (unsigned i = 0; i < 4800 * 2; i++) scratch[i] = (float)((i + pass) % 127) / 256.0f;
        old.pending_frames = current.pending_frames = n > 1024 ? n - 13 : n;
        struct release_render_buffer_params a = {(uintptr_t)&old, n, pass % 3 == 0 ? 2u : 0, -1};
        struct release_render_buffer_params b = {(uintptr_t)&current, n, a.flags, -1};
        in_release = 1; ios_release_render_buffer_before(&a); ios_release_render_buffer(&b); in_release = 0;
        CHECK(a.result == b.result && !old.pending_frames && !current.pending_frames);
        CHECK(atomic_load(&old.write_pos) == atomic_load(&current.write_pos));
        CHECK(!memcmp(ring_before, ring_after, sizeof(ring_before)));
        if (a.flags & 2) for (unsigned i = 0; i < current.scratch_frames * 2 && i < n * 2; i++) {
            if (i < (n > 1024 ? n - 13 : n) * 2) CHECK(scratch[i] == 0);
        }
    }
    if (enabled) {
        CHECK(trace->streams[0].captured_frames > 0 && trace->streams[0].zero_submissions > 0);
        ios_release_trace_close(&current, APS5_RELEASE_TEARDOWN);
        CHECK(trace->streams[0].reason == APS5_RELEASE_TEARDOWN);
    }
    atomic_store(&g_audio_release_trace, NULL);
    for (int j = 0; j < 2; j++) {
        struct ios_stream *s = j ? &current : &old;
        atomic_store(&s->write_pos, 4800); atomic_store(&s->play_pos, 0);
        atomic_store(&g_mix[0], s);
        float out[2048]; AudioBufferList io = {1, {{2, sizeof(out), out}}};
        AudioTimeStamp stamp = {0, 1, 3}; AudioUnitRenderActionFlags flags = 0;
        in_render = 1;
        if (!j) ios_audio_render_cb_before(NULL, &flags, &stamp, 0, 1024, &io);
        else ios_audio_render_cb(NULL, &flags, &stamp, 0, 1024, &io);
        in_render = 0;
        for (unsigned i = 0; i < 2048; i++) CHECK(out[i] == ring_before[i]);
        CHECK(atomic_load(&s->play_pos) == 1024);
    }
    for (int i = 0; i < IOS_MAX_STREAMS; i++) atomic_store(&g_mix[i], NULL);
}
static void worker_export(struct aps5_release_trace *t)
{
    struct aps5_release_format f = format(); uint64_t wr = 0;
    CHECK(!aps5_release_register(t, 77, &f, aps5_release_now_ns()));
    /* Use deterministic bounded synthetic stamps inside the real deadline. */
    const uint64_t start = aps5_release_now_ns();
    unsigned char b[1024 * 8];
    while (wr < APS5_RELEASE_FRAMES) {
        unsigned n = APS5_RELEASE_FRAMES - wr < 1024 ? (unsigned)(APS5_RELEASE_FRAMES - wr) : 1024;
        for (unsigned frame = 0; frame < n; frame++) for (unsigned byte = 0; byte < 8; byte++)
            b[frame * 8 + byte] = pattern(wr + frame, byte);
        struct aps5_release_stamp st = {start + wr, 2 + wr / 1024, wr, wr, n, n, 0};
        aps5_release_record(t, 77, b, (size_t)n * 8, &f, &st); wr += n;
    }
    for (unsigned wait = 0; wait < 1000; wait++) {
        pthread_mutex_lock(&t->lock);
        int done = t->streams[1].state == APS5_RELEASE_EXPORTED && t->streams[0].state == APS5_RELEASE_EXPORTED;
        pthread_mutex_unlock(&t->lock);
        if (done) return;
        usleep(1000);
    }
    CHECK(!"worker export timed out");
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    uint32_t skip;
    CHECK(aps5_release_parse_skip(NULL, &skip) && !skip);
    CHECK(aps5_release_parse_skip("28800000", &skip) && skip == 28800000);
    for (const char **p = (const char *[]) {"", "-1", "+1", " 1", "1 ", "28800001", "9999999999999999999999999", "x", NULL}; *p; p++)
        CHECK(!aps5_release_parse_skip(*p, &skip));
    const char *flag = getenv("MADEIRA_AUDIO_TRACE_RELEASE");
    int enabled = flag && !strcmp(flag, "1");
    unsigned before = allocations;
    struct aps5_release_trace *t = aps5_release_begin(flag, NULL, argv[1]);
    CHECK(!!t == enabled && allocations == before + enabled);
    before = allocations;
    CHECK(!aps5_release_begin("1", "28800001", argv[1]) && allocations == before);
    CHECK(!aps5_release_begin("1", "-1", argv[1]) && allocations == before);
    release_and_callback_parity(enabled, t);
    if (enabled) {
        canonical(); lifecycle_and_limits(); formats_parallel_and_export_errors(argv[1]); worker_export(t);
        CHECK(sizeof(*t) < 8u * 1024 * 1024);
    }
    printf("{\"status\":\"passed\",\"checks\":%u,\"traceEnabled\":%s,\"poolBytes\":%zu}\n",
           checks, enabled ? "true" : "false", sizeof(struct aps5_release_trace));
    return 0;
}
