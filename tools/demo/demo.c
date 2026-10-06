// An original, freestanding x86-64 guest. Only public sce* imports are used.
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
static ShaderHeader shader_header;
static u32 commands[64] __attribute__((aligned(256)));
static u32* source;
static volatile u32* output;
static volatile u32* label;

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
static int fail(const char* stage, int value) { event(stage, "fail", (u32)value); return 1; }
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
static u32 checksum(const volatile u32* pixels) {
    u32 hash = 2166136261u;
    for (unsigned i = 0; i < PIXELS; ++i) { hash ^= pixels[i]; hash *= 16777619u; }
    return hash;
}
static void descriptor(u32* to, const void* address) {
    u64 pointer = (u64)address;
    to[0] = (u32)pointer; to[1] = (u32)(pointer >> 32) & 0xffff;
    to[2] = FRAME_BYTES; to[3] = 0x01016fac;
}
static unsigned make_commands(unsigned frame) {
    unsigned n = 0; const u64 code = (u64)shader_code, done = (u64)label;
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
    const u64 started = sceKernelGetProcessTime();
    for (unsigned frame = 0; frame < FRAMES; ++frame) {
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
        unsigned n = make_commands(frame); Packet packet = {commands, n, 0, {0, 0, 0}};
        error = sceAgcDriverSubmitDcb(&packet);
        if (error) return fail("dispatch_submit", error);
        unsigned polls = 0;
        while (*label != frame + 1 && polls++ < 30000) sceKernelUsleep(1000);
        if (*label != frame + 1) return fail("gpu_timeout", frame);
        for (unsigned i = 0; i < PIXELS; ++i)
            if (output[i] != (source[i] ^ 1u)) return fail("gpu_readback", i);
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
        frames_done = frame + 1;
        if (frame % 60 == 0) { event("frame", "pass", frame); event("gpu_checksum", "pass", checksum(output)); }
    }
    event("input_changes", "pass", input_changes);
    event("frames_presented", "pass", frames_done);
    event("elapsed_us", "pass", sceKernelGetProcessTime() - started);
    error = sceVideoOutClose(video);
    if (error) return fail("video_close", error);
    event("complete", user_stopped ? "stopped" : "pass", frames_done);
    return 0;
}
