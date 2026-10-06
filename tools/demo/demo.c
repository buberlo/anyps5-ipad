// An original, freestanding x86-64 guest using public sce* and libc imports.
// The CPU draws a small paddle game; a real RDNA compute program transforms
// EVERY pixel before VideoOut presents it. A per-frame CPU comparison checks
// GPU readback. No shader-recompiler or VideoOut internal API is imported.
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef long long i64;
typedef unsigned long usize;
#include "imports.h"

#ifndef DEMO_WIDTH
#define DEMO_WIDTH 1280
#endif
#ifndef DEMO_HEIGHT
#define DEMO_HEIGHT 720
#endif
#ifndef DEMO_SECONDS
#define DEMO_SECONDS 600
#endif
#ifndef DEMO_SMOKE_PROFILE
#define DEMO_SMOKE_PROFILE 0
#endif
#ifndef DEMO_ACCEPTANCE_PROFILE
#define DEMO_ACCEPTANCE_PROFILE 1
#endif
#define WIDTH DEMO_WIDTH
#define HEIGHT DEMO_HEIGHT
#define PIXELS (WIDTH * HEIGHT)
_Static_assert(PIXELS % 32 == 0, "compute dispatch must cover every pixel");
#define FRAME_BYTES (PIXELS * 4)
#define FRAME_STRIDE ((FRAME_BYTES + 65535) & ~65535)
#define STORAGE_BYTES (2 * FRAME_STRIDE + 65536)
#ifndef DEMO_FRAME_LIMIT
#define DEMO_FRAME_LIMIT 36000
#endif
#define FRAMES DEMO_FRAME_LIMIT

typedef struct { u32 offset, value; } ShaderReg;
typedef struct {
    u32 magic, version;
    void *user_data, *code, *cx, *sh, *specials, *inputs, *outputs;
    u32 header_size, shader_size, constants, target, num_inputs;
    u16 scratch, num_outputs, special_size;
    u8 type, num_cx, num_sh;
} Shader;
typedef struct { Shader shader; ShaderReg regs[2]; } ShaderHeader;
typedef struct { u32* words; u32 count; u8 flags, reserved[3]; } Packet;
typedef struct {
    u32 reserved0, tiling, aspect, width, height, pitch;
    u64 option, format, clear;
    u32 dcc, pad;
    u64 reserved[3];
} BufferAttribute;
typedef struct { const void* pixels; const void* metadata; const void* reserved[2]; } VideoBuffer;
typedef struct {
    u64 count, process_time, reserved0;
    i64 argument;
    u64 reserved1, time_counter;
    int queue, pending, current;
    u32 reserved2;
    u64 submit_time, reserved3[7];
} FlipStatus;

_Static_assert(sizeof(Shader) == 96, "shader ABI");
_Static_assert(sizeof(BufferAttribute) == 80, "VideoOut ABI");
_Static_assert(sizeof(Packet) == 16, "packet ABI");
_Static_assert(sizeof(FlipStatus) == 128, "flip status ABI");

// gfx10: local byte offset = v0 << 2; workgroup byte offset = s8 << 7.
// Load input, XOR the low color channel by 1, and store output; the non-copy operation
// prevents the HLE copy-kernel fast path from satisfying this test.
static const u32 shader_code[] __attribute__((aligned(256))) = {
    0x34020082,                 // v_lshlrev_b32 v1, 2, v0
    0x8f098708,                 // s_lshl_b32 s9, s8, 7
    0xe0301000, 0x09000201,     // buffer_load_dword v2, v1, s[0:3], s9 offen
    0xbf8c3f70,                 // s_waitcnt vmcnt(0)
    0x3a040481,                 // v_xor_b32 v2, 1, v2
    0xe0701000, 0x09010201,     // buffer_store_dword v2, v1, s[4:7], s9 offen
    0xbf810000                  // s_endpgm
};
// An exercised data relocation, in addition to the ELF's imported PLT entries.
static const u32* volatile shader_pointer = shader_code;
static ShaderHeader shader_header;
static u32 commands[64] __attribute__((aligned(256)));
static u32* source;
static volatile u32* output;
static volatile u32* label;
static u32 frame_times[FRAMES];

static usize length(const char* text) { usize n = 0; while (text[n]) ++n; return n; }
static void write_text(const char* text) { sceKernelWrite(1, text, length(text)); }
static void number(u64 value) {
    char digits[24]; usize n = 0;
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) sceKernelWrite(1, &digits[--n], 1);
}
static void event(const char* stage, const char* status, u64 value) {
    write_text("{\"schema\":1,\"probe\":\"demo\",\"stage\":\""); write_text(stage);
    write_text("\",\"status\":\""); write_text(status);
    write_text("\",\"value\":"); number(value); write_text("}\n");
}
static void complete(const char* status, u64 frames) {
    write_text("{\"schema\":1,\"probe\":\"demo\",\"stage\":\"complete\",\"scope\":\"execution_only\",\"status\":\"");
    write_text(status); write_text("\",\"value\":"); number(frames); write_text("}\n");
}
static int fail(const char* stage, int value) {
    event(stage, "fail", (u32)value);
    exit(1);
    __builtin_unreachable();
}
static u32 percentile95(unsigned count) {
    // In-place quickselect runs only after the timed frame loop has ended.
    int low = 0, high = (int)count - 1;
    const int target = (int)((count * 95u + 99u) / 100u) - 1;
    while (low < high) {
        const u32 pivot = frame_times[low + (high - low) / 2];
        int left = low, right = high;
        while (left <= right) {
            while (frame_times[left] < pivot) ++left;
            while (frame_times[right] > pivot) --right;
            if (left <= right) {
                const u32 tmp = frame_times[left]; frame_times[left] = frame_times[right]; frame_times[right] = tmp;
                ++left; --right;
            }
        }
        if (target <= right) high = right;
        else if (target >= left) low = left;
        else break;
    }
    return frame_times[target];
}
static void rectangle(int x, int y, int width, int height, u32 color) {
    for (int j = y; j < y + height; ++j)
        for (int i = x; i < x + width; ++i)
            if ((unsigned)i < WIDTH && (unsigned)j < HEIGHT) source[j * WIDTH + i] = color;
}
// Design-space coordinates keep the same playable scene at 320x192 and 720p.
static void game_rectangle(int x, int y, int width, int height, u32 color) {
    rectangle(x * WIDTH / 320, y * HEIGHT / 192,
              (width * WIDTH + 319) / 320, (height * HEIGHT + 191) / 192, color);
}
static u32 checksum_xor(const volatile u32* pixels, u32 mask) {
    u32 hash = 2166136261u;
    for (unsigned i = 0; i < PIXELS; ++i) { hash ^= pixels[i] ^ mask; hash *= 16777619u; }
    return hash;
}
static u32 checksum(const volatile u32* pixels) { return checksum_xor(pixels, 0); }
static void snapshot(const char* path, const void* pixels) {
    const int fd = sceKernelOpen(path, 0x601, 0600); // WRONLY | CREAT | TRUNC
    if (fd < 0) { event("diagnostic_snapshot", "unavailable", (u32)fd); return; }
    const i64 written = sceKernelWrite(fd, pixels, FRAME_BYTES);
    sceKernelClose(fd);
    event("diagnostic_snapshot", written == FRAME_BYTES ? "saved" : "incomplete", (u64)written);
}
static void descriptor(u32* to, const void* address) {
    u64 pointer = (u64)address;
    to[0] = (u32)pointer; to[1] = (u32)(pointer >> 32) & 0xffff;
    to[2] = FRAME_BYTES; to[3] = 0x01016fac;
}
static unsigned make_commands(unsigned frame) {
    unsigned n = 0; const u64 code = (u64)shader_pointer, done = (u64)label;
#define WORD(value) commands[n++] = (value)
    WORD(0xc0027600); WORD(0x20c); WORD((u32)(code >> 8)); WORD((u32)(code >> 40));
    WORD(0xc0017600); WORD(0x213); WORD((8u << 1) | (1u << 7));
    WORD(0xc0037600); WORD(0x207); WORD(32); WORD(1); WORD(1);
    WORD(0xc0087600); WORD(0x240);
    descriptor(&commands[n], source); n += 4;
    descriptor(&commands[n], (const void*)output); n += 4;
    WORD(0xc0031500); WORD(PIXELS / 32); WORD(1); WORD(1); WORD(0x8041);
    WORD(0xc0064900); WORD(0x514); WORD(1u << 29);
    WORD((u32)done); WORD((u32)(done >> 32)); WORD(frame + 1); WORD(0); WORD(0);
#undef WORD
    return n;
}

int demo_entry(void) {
    event("start", "pass", FRAMES);
    i64 physical = 0; void* mapped = 0;
    int error = sceKernelAllocateDirectMemory(0, 0x7fffffffffLL, STORAGE_BYTES, 65536, 0, &physical);
    if (error) return fail("direct_allocate", error);
    error = sceKernelMapDirectMemory(&mapped, STORAGE_BYTES, 0x33, 0, physical, 65536);
    if (error) return fail("direct_map", error);
    source = (u32*)mapped; output = (volatile u32*)((u8*)mapped + FRAME_STRIDE);
    label = (volatile u32*)((u8*)mapped + 2 * FRAME_STRIDE); *label = 0;
    shader_header.shader.magic = 0x34333231;
    shader_header.shader.version = 0x18;
    shader_header.shader.header_size = sizeof(shader_header);
    shader_header.shader.shader_size = sizeof(shader_code);
    shader_header.shader.type = 0;
    shader_header.shader.num_sh = 2;
    shader_header.regs[0].offset = 0x20c; shader_header.regs[1].offset = 0x20d;
    // sceAgcCreateShader consumes relative offsets from each pointer FIELD.
    shader_header.shader.sh = (void*)((u64)shader_header.regs - (u64)&shader_header.shader.sh);
    void* created = 0;
    error = sceAgcCreateShader(&created, &shader_header, shader_code);
    if (error || created != &shader_header) return fail("shader_create", error);
    int video = sceVideoOutOpen(255, 0, 0, 0);
    if (video < 0) return fail("video_open", video);
    BufferAttribute attribute = {0};
    sceVideoOutSetBufferAttribute2(&attribute, 0x8000000000000000ULL, 1, WIDTH, HEIGHT, 0, 0, 0);
    VideoBuffer buffer = {(const void*)output, 0, {0, 0}};
    error = sceVideoOutRegisterBuffers2(video, 0, 0, &buffer, 1, &attribute, 0, 0);
    if (error) return fail("video_buffers", error);
    if ((error = scePadInit())) return fail("pad_init", error);
    int pad = scePadOpen(255, 0, 0, 0);
    if (pad < 0) return fail("pad_open", pad);
    int paddle = 132, ball_x = 160, ball_y = 70, dx = 2, dy = 2;
    unsigned score = 0, input_changes = 0, served = 0, frames_done = 0;
    u32 previous_buttons = 0; u8 previous_stick = 128;
    int user_stopped = 0;
    u64 minimum_frame = ~0ULL, maximum_frame = 0;
    // Aggregate phases independently from display FPS. Keep the full-pixel
    // reference comparison and the same wait/pacing behavior in this probe.
    u64 draw_us = 0, dispatch_us = 0, readback_us = 0, flip_us = 0, pacing_us = 0;
    u64 gpu_poll_calls = 0, flip_poll_calls = 0;
    const u64 started = sceKernelGetProcessTime();
    for (unsigned frame = 0; frame < FRAMES; ++frame) {
        const u64 frame_started = sceKernelGetProcessTime();
        if (sceKernelGetProcessTime() - started >= (u64)DEMO_SECONDS * 1000000) break;
        u8 pad_data[256] __attribute__((aligned(16))) = {0};
        error = scePadReadState(pad, pad_data);
        if (error) return fail("pad_read", error);
        u32 buttons = *(u32*)pad_data;
        if (buttons != previous_buttons || pad_data[4] != previous_stick) { ++input_changes; event("input", "pass", buttons); }
        if ((buttons & 0x4000) && !(previous_buttons & 0x4000)) {
            ball_x = paddle + 25; ball_y = 160; dy = -2; ++served;
            event("serve_action", "pass", served);
        }
        previous_buttons = buttons; previous_stick = pad_data[4];
        if ((buttons & 0x80) || pad_data[4] < 80) paddle -= 4;
        if ((buttons & 0x20) || pad_data[4] > 176) paddle += 4;
        if (buttons & 0x2000) { user_stopped = 1; event("user_exit", "stopped", frame); break; }
        if (paddle < 8) paddle = 8;
        if (paddle > 256) paddle = 256;
        ball_x += dx; ball_y += dy;
        if (ball_x <= 9 || ball_x >= 305) dx = -dx;
        if (ball_y <= 28) dy = 2;
        if (ball_y >= 161 && ball_y <= 168 && ball_x + 6 >= paddle && ball_x <= paddle + 56) { dy = -2; ++score; }
        if (ball_y > 180) { ball_x = 160; ball_y = 60; score = 0; }
        for (unsigned y = 0; y < HEIGHT; ++y)
            for (unsigned x = 0; x < WIDTH; ++x) source[y * WIDTH + x] = 0xff171020u + ((y / 12) & 1) * 0x00020204;
        game_rectangle(4, 24, 312, 2, 0xff887766); game_rectangle(4, 24, 2, 164, 0xff887766);
        game_rectangle(314, 24, 2, 164, 0xff887766);
        game_rectangle(paddle, 169, 56, 7, 0xffe6ba38); game_rectangle(ball_x, ball_y, 6, 6, 0xff68def5);
        for (unsigned i = 0; i < score % 25; ++i) game_rectangle(8 + i * 12, 8, 8, 7, 0xff73e598);
        game_rectangle(8, 183, 304 * (frame % 600) / 600, 2, 0xff7464ff);
        const u64 drawn = sceKernelGetProcessTime();
        draw_us += drawn - frame_started;
        unsigned n = make_commands(frame); Packet packet = {commands, n, 0, {0, 0, 0}};
        error = sceAgcDriverSubmitDcb(&packet);
        if (error) return fail("dispatch_submit", error);
        unsigned polls = 0;
        while (*label != frame + 1 && polls++ < 30000) sceKernelUsleep(1000);
        if (*label != frame + 1) return fail("gpu_timeout", frame);
        gpu_poll_calls += polls;
        const u64 dispatched = sceKernelGetProcessTime();
        dispatch_us += dispatched - drawn;
        for (unsigned i = 0; i < PIXELS; ++i)
            if (output[i] != (source[i] ^ 1u)) return fail("gpu_readback", i);
        u32 before_flip = 0;
        if (frame % 60 == 0) {
            before_flip = checksum(output);
            event("gpu_checksum_before_flip", "sample", before_flip);
            event("source_xor_checksum", "sample", checksum_xor(source, 1));
        }
        const u64 compared = sceKernelGetProcessTime();
        readback_us += compared - dispatched;
        error = sceVideoOutSubmitFlip(video, 0, 1, frame + 1);
        if (error) return fail("flip_submit", error);
        FlipStatus status = {0}; polls = 0;
        do {
            error = sceVideoOutGetFlipStatus(video, &status);
            if (error) return fail("flip_status", error);
            if (status.argument == (i64)frame + 1 && status.pending == 0) break;
            sceKernelUsleep(1000);
        } while (++polls < 30000);
        if (polls >= 30000) return fail("flip_timeout", frame);
        flip_poll_calls += polls;
        const u64 flipped = sceKernelGetProcessTime();
        flip_us += flipped - compared;
        frames_done = frame + 1;
        if (frame % 60 == 0) {
            const u32 actual = checksum(output);
            event("frame", "pass", frame);
            event("gpu_checksum", "pass", actual);
            event("paddle_x", "sample", paddle);
            event("ball_x", "sample", ball_x);
            event("ball_y", "sample", ball_y);
            event("score", "sample", score);
            if (actual != before_flip) return fail("presentation_changed_guest_buffer", actual);
            // The qualified Windows neutral-input smoke and an independent
            // integer scene model agree on these checkpoints. Checking only
            // output == source ^ 1 cannot catch a divergent CPU scene.
            if (DEMO_SMOKE_PROFILE && WIDTH == 320 && HEIGHT == 192 && !input_changes) {
                const u32 expected = frame == 0 ? 1745292961u :
                                     frame == 60 ? 3237446373u : actual;
                if (actual != expected) {
                    snapshot("/app0/demo-source.rgba", source);
                    snapshot("/app0/demo-output.rgba", (const void*)output);
                    return fail("neutral_scene_reference", actual);
                }
                event("neutral_scene_reference", "pass", frame);
            }
        }
        // Target60Hz using the guest monotonic clock. Catch up after a slow
        // frame without introducing another full-frame sleep.
        const u64 next_frame = started + ((u64)frames_done * 1000000 + 59) / 60;
        u64 now;
        while ((now = sceKernelGetProcessTime()) < next_frame) {
            const u64 remaining = next_frame - now;
            sceKernelUsleep((unsigned)(remaining > 1000 ? 1000 : remaining));
        }
        const u64 frame_elapsed = sceKernelGetProcessTime() - frame_started;
        // Includes sparse post-flip checkpoint logging as well as pacing.
        pacing_us += sceKernelGetProcessTime() - flipped;
        frame_times[frame] = (u32)(frame_elapsed > 0xffffffffULL ? 0xffffffffULL : frame_elapsed);
        if (frame_elapsed < minimum_frame) minimum_frame = frame_elapsed;
        if (frame_elapsed > maximum_frame) maximum_frame = frame_elapsed;
        if (frame % 60 == 0) event("frame_elapsed_us", "sample", frame_elapsed);
    }
    const u64 elapsed = sceKernelGetProcessTime() - started;
    event("input_changes", "pass", input_changes);
    event("input_observed", input_changes ? "observed" : "not_observed", input_changes);
    event("serve_actions", served ? "observed" : "not_observed", served);
    event("readback_frames", frames_done ? "pass" : "fail", frames_done);
    event("frames_presented", "pass", frames_done);
    event("elapsed_us", "pass", elapsed);
    event("draw_input_total_us", "measured", draw_us);
    event("dispatch_wait_total_us", "measured", dispatch_us);
    event("readback_checkpoint_total_us", "measured", readback_us);
    event("flip_wait_total_us", "measured", flip_us);
    event("post_flip_pacing_total_us", "measured", pacing_us);
    event("gpu_poll_sleep_calls", "measured", gpu_poll_calls);
    event("flip_poll_sleep_calls", "measured", flip_poll_calls);
    const u64 fps_milli = elapsed ? (u64)frames_done * 1000000000ULL / elapsed : 0;
    event("avg_fps_milli", "measured", fps_milli);
    if (frames_done) {
        event("frame_time_min_us", "measured", minimum_frame);
        event("frame_time_max_us", "measured", maximum_frame);
        event("frame_time_p95_us", "measured", percentile95(frames_done));
    }
    write_text("{\"schema\":1,\"probe\":\"demo\",\"stage\":\"timing_scope\",\"status\":\"info\",\"detail\":\"guest monotonic time includes draw, GPU readback, FlipStatus acknowledgment and 60Hz pacing; not display timestamps\"}\n");
    event("onscreen_device_foreground", "pending_external_check", 0);
    if (DEMO_ACCEPTANCE_PROFILE)
        event("acceptance_duration", !user_stopped && elapsed >= 600000000ULL ? "pass" : "not_met", elapsed);
    if (DEMO_ACCEPTANCE_PROFILE)
        event("acceptance_average_fps", !user_stopped && fps_milli >= 30000 ? "pass" : "not_met", fps_milli);
    error = sceVideoOutClose(video);
    if (error) return fail("video_close", error);
    if (!user_stopped && (!frames_done || (DEMO_SMOKE_PROFILE && frames_done != FRAMES)))
        return fail("required_frames", frames_done);
    complete(user_stopped ? "stopped" : "pass", frames_done);
    // The normal libc exit path stops and joins the VideoOut/AGC workers
    // before Windows starts DLL teardown. Returning directly to the PE
    // entry stub's ExitProcess would terminate workers before their cleanup.
    exit(0);
    __builtin_unreachable();
}
