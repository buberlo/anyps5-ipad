// Original freestanding ELF testing the public libc exception/heap ABI.
using usize = __SIZE_TYPE__;
#ifdef HOST_REFERENCE
#include <cstdio>
#include <cstdlib>
static void text(const char *s) { std::fputs(s, stdout); }
#else
extern "C" long long sceKernelWrite(int, const void *, usize);
extern "C" void *malloc(usize);
extern "C" void free(void *);
static void text(const char *s) {
    usize n = 0; while (s[n]) ++n;
    sceKernelWrite(1, s, n);
}
#endif

static unsigned cleanups, rethrows;
struct Error { unsigned value; };
struct Guard { ~Guard() { ++cleanups; } };

__attribute__((noinline)) static void leaf(unsigned value) {
    Guard guard;
    throw Error{value};
}
__attribute__((noinline)) static void middle(unsigned value) {
    Guard guard;
    try { leaf(value); }
    catch (const Error &error) {
        if (error.value == value) ++rethrows;
        throw;
    }
}
// Keep genuine data relocation and defeat interprocedural constant folding.
static void (*volatile entry)(unsigned) = middle;

static int event(const char *name, bool pass) {
    text("{\"schema\":1,\"probe\":\"guest_exceptions\",\"stage\":\"");
    text(name);
    text(pass ? "\",\"status\":\"pass\"}\n" : "\",\"status\":\"fail\"}\n");
    return pass ? 0 : 1;
}

extern "C" int guest_exceptions_entry() {
    event("entry", true);
    auto *bytes = static_cast<volatile unsigned char *>(malloc(65573));
    if (!bytes) return event("heap_allocate", false);
    for (usize i = 0; i < 65573; ++i) bytes[i] = (unsigned char)(i * 17 + 9);
    bool heap_ok = true;
    for (usize i = 0; i < 65573; ++i) heap_ok &= bytes[i] == (unsigned char)(i * 17 + 9);
    free(const_cast<unsigned char *>(bytes));
    if (event("heap_write_read_free", heap_ok)) return 1;
    unsigned caught = 0;
    for (unsigned i = 1; i <= 16; ++i) {
        try { entry(i); }
        catch (const Error &error) { if (error.value == i) ++caught; }
        catch (...) { return event("typed_catch", false); }
    }
    if (event("typed_catch_and_rethrow", caught == 16 && rethrows == 16)) return 1;
    if (event("unwind_destructors", cleanups == 32)) return 1;
    return event("complete", true);
}

#ifdef HOST_REFERENCE
int main() { return guest_exceptions_entry(); }
#endif
