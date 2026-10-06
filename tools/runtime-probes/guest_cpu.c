// Original freestanding SysV ELF: public HLE calls and callbacks, guest TLS,
// pthread key destructors, concurrent atomics and a three-page guest stack.
// CPU SIMD correctness is covered separately by cpu-probe.exe.
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef long long i64;
typedef unsigned long usize;
typedef void* (*ThreadEntry)(void*);
typedef void (*KeyDestructor)(void*);
#include "imports.h"

extern u64* guest_tls_initialized(void);
extern u64* guest_tls_zero(void);
static const u64 TLS_INITIAL = 0x1020304050607080ULL;
enum { WORKERS = 2, ITERATIONS = 512 };
typedef struct { u64 id; u32 done, failure; } Worker;
static Worker workers[WORKERS];
static int key;
static u32 ready, started, destructors, destructor_failure;
static u64 counter;

static usize length(const char* text) { usize n = 0; while (text[n]) ++n; return n; }
static void text(const char* value) { sceKernelWrite(1, value, length(value)); }
static void number(u64 value) {
    char digits[24]; usize n = 0;
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) sceKernelWrite(1, &digits[--n], 1);
}
static void event(const char* stage, int pass, u64 value) {
    text("{\"schema\":1,\"probe\":\"guest_cpu\",\"stage\":\""); text(stage);
    text(pass ? "\",\"status\":\"pass\",\"value\":" : "\",\"status\":\"fail\",\"value\":");
    number(value); text("}\n");
}
static int fail(const char* stage, u64 value) { event(stage, 0, value); return 1; }

__attribute__((noinline)) static u64 nine_arguments(u64 a, u64 b, u64 c, u64 d, u64 e,
                                                   u64 f, u64 g, u64 h, u64 i) {
    return a + 3*b + 5*c + 7*d + 11*e + 13*f + 17*g + 19*h + 23*i;
}

static void key_destructor(void* value) {
    Worker* worker = (Worker*)value;
    if (*guest_tls_initialized() != worker->id || *guest_tls_zero() != ITERATIONS)
        __atomic_store_n(&destructor_failure, 1, __ATOMIC_RELEASE);
    __atomic_fetch_add(&destructors, 1, __ATOMIC_ACQ_REL);
}

static void* work(void* argument) {
    Worker* worker = (Worker*)argument;
    volatile u8 stack[9216];
    for (usize i = 0; i < sizeof(stack); ++i) stack[i] = (u8)(i + worker->id);
    u64* initialized = guest_tls_initialized();
    u64* zero = guest_tls_zero();
    if (*initialized != TLS_INITIAL || *zero != 0 || scePthreadGetspecific(key) != 0)
        worker->failure = 1;
    *initialized = worker->id;
    if (scePthreadSetspecific(key, worker) != 0) worker->failure = 2;
    __atomic_fetch_add(&ready, 1, __ATOMIC_ACQ_REL);
    const u64 deadline = sceKernelGetProcessTime() + 10000000ULL;
    while (!__atomic_load_n(&started, __ATOMIC_ACQUIRE)) {
        if (sceKernelGetProcessTime() > deadline) { worker->failure = 3; break; }
        sceKernelUsleep(1000);
    }
    for (u64 i = 0; i < ITERATIONS; ++i) {
        if (*initialized != worker->id || *zero != i || scePthreadGetspecific(key) != worker)
            worker->failure = 4;
        *zero = i + 1;
        __atomic_fetch_add(&counter, 1, __ATOMIC_ACQ_REL);
        if ((i & 15) == 0) sceKernelUsleep(1000);
    }
    for (usize i = 0; i < sizeof(stack); ++i)
        if (stack[i] != (u8)(i + worker->id)) worker->failure = 5;
    __atomic_store_n(&worker->done, 1, __ATOMIC_RELEASE);
    return worker;
}

int guest_cpu_entry(void) {
    event("entry", 1, 0);
    volatile u64 seed = 1;
    const u64 abi = nine_arguments(seed, 2, 3, 4, 5, 6, 7, 8, 9);
    if (abi != 761) return fail("sysv_register_and_stack_arguments", abi);
    event("sysv_register_and_stack_arguments", 1, abi);
    if (*guest_tls_initialized() != TLS_INITIAL || *guest_tls_zero() != 0)
        return fail("main_elf_tls_template", *guest_tls_initialized());
    *guest_tls_initialized() = 0xabcdef;
    *guest_tls_zero() = 1234;
    event("main_elf_tls_template", 1, 1);
    int error = scePthreadKeyCreate(&key, key_destructor);
    if (error) return fail("key_create", (u32)error);
    error = scePthreadSetspecific(key, &counter);
    if (error) return fail("main_key_value", (u32)error);
    void* threads[WORKERS];
    for (unsigned i = 0; i < WORKERS; ++i) {
        workers[i].id = i + 7;
        error = scePthreadCreate(&threads[i], 0, work, &workers[i], "guest-cpu-probe");
        if (error) return fail("thread_create", (u32)error);
    }
    u64 deadline = sceKernelGetProcessTime() + 10000000ULL;
    while (__atomic_load_n(&ready, __ATOMIC_ACQUIRE) != WORKERS) {
        if (sceKernelGetProcessTime() > deadline) return fail("thread_ready_timeout", ready);
        sceKernelUsleep(1000);
    }
    __atomic_store_n(&started, 1, __ATOMIC_RELEASE);
    deadline = sceKernelGetProcessTime() + 20000000ULL;
    for (unsigned i = 0; i < WORKERS; ++i) {
        while (!__atomic_load_n(&workers[i].done, __ATOMIC_ACQUIRE)) {
            if (sceKernelGetProcessTime() > deadline) return fail("worker_timeout", i);
            sceKernelUsleep(1000);
        }
        // The runner also imposes a hard process timeout: Join itself has no timeout API.
        void* result = 0;
        error = scePthreadJoin(threads[i], &result);
        if (error || result != &workers[i] || workers[i].failure)
            return fail("thread_join_callback_tls_stack", error ? (u32)error : workers[i].failure + 100*i);
    }
    event("thread_join_callback_tls_stack", 1, WORKERS);
    if (__atomic_load_n(&counter, __ATOMIC_ACQUIRE) != WORKERS * ITERATIONS)
        return fail("atomic_64_counter", counter);
    event("atomic_64_counter", 1, counter);
    if (destructors != WORKERS || destructor_failure)
        return fail("pthread_key_destructors", destructors);
    event("pthread_key_destructors", 1, destructors);
    if (*guest_tls_initialized() != 0xabcdef || *guest_tls_zero() != 1234 || scePthreadGetspecific(key) != &counter)
        return fail("main_tls_isolation", 0);
    event("main_tls_isolation", 1, 1);
    error = scePthreadKeyDelete(key);
    if (error) return fail("key_delete", (u32)error);
    event("complete", 1, 1);
    return 0;
}
