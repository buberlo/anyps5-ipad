// Run against the actual patched allocator, not a second copy of its logic.
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <limits>

static constexpr std::uintptr_t test_base = UINT64_C(0x200000000);
static constexpr std::size_t chunk = 0x10000;
static void* startup_obstacle;
static unsigned failed;

static bool check(const char* name, bool pass) {
    std::printf("{\"schema\":1,\"probe\":\"allocator\",\"case\":\"%s\",\"status\":\"%s\"}\n", name, pass ? "pass" : "fail");
    std::fflush(stdout);
    failed += !pass;
    return pass;
}

// Defined before the included implementation so its pre-main Arena singleton
// observes a real occupied host region and the controlled test profile.
static struct Prepare {
    Prepare() {
        _putenv("APS5_GUEST_ARENA_BASE=0x200000000");
        _putenv("APS5_GUEST_ARENA_SIZE=0x100000");
        _putenv("APS5_GUEST_ARENA_CHUNK=0x10000");
        _putenv("APS5_GUEST_ARENA_LAZY=1");
        startup_obstacle = VirtualAlloc(reinterpret_cast<void*>(test_base + 2 * chunk), chunk,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!check("prepare_exact_host_obstacle", startup_obstacle == reinterpret_cast<void*>(test_base + 2 * chunk))) std::exit(2);
        *static_cast<volatile unsigned*>(startup_obstacle) = 0x12345678;
    }
} prepare;

#include <prx/libc/src/GuestArena.cpp>

template<class F> static bool throws(F&& operation) {
    try { operation(); } catch (const std::exception&) { return true; }
    return false;
}

static bool is_free(std::uintptr_t address) {
    MEMORY_BASIC_INFORMATION info{};
    return VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) && info.State == MEM_FREE;
}

int main() {
    using namespace GuestArena;
    try {
        check("startup_host_collision_recorded", GuestArenaHostRegionOverlaps_nid_postfix(test_base + 2 * chunk, chunk));
        void* first = GuestArenaAllocate_nid_postfix(2 * chunk, chunk);
        check("allocate_before_hole", first == reinterpret_cast<void*>(test_base));
        void* next = GuestArenaAllocate_nid_postfix(chunk, chunk);
        check("skip_host_collision", next == reinterpret_cast<void*>(test_base + 3 * chunk));
        check("fixed_collision_refused", throws([] {
            GuestArenaMarkUsed_nid_postfix(reinterpret_cast<void*>(test_base + 2 * chunk), chunk);
        }));
        check("host_contents_unchanged", *static_cast<volatile unsigned*>(startup_obstacle) == 0x12345678);
        GuestArenaRelease_nid_postfix(next, chunk);

        // Two fresh chunks must be rolled back when the third cannot be held.
        void* later = VirtualAlloc(reinterpret_cast<void*>(test_base + 6 * chunk), chunk,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!check("prepare_late_host_collision", later == reinterpret_cast<void*>(test_base + 6 * chunk))) return 1;
        check("multichunk_failure_reported", throws([] {
            GuestArenaAllocateAtOrAbove_nid_postfix(test_base + 4 * chunk, 3 * chunk, chunk);
        }));
        check("partial_reservations_rolled_back", is_free(test_base + 4 * chunk) && is_free(test_base + 5 * chunk));
        check("remove_late_obstacle", VirtualFree(later, 0, MEM_RELEASE));
        void* retry = GuestArenaAllocateAtOrAbove_nid_postfix(test_base + 4 * chunk, 3 * chunk, chunk);
        check("failed_allocation_did_not_consume_range", retry == reinterpret_cast<void*>(test_base + 4 * chunk));
        GuestArenaRelease_nid_postfix(retry, 3 * chunk);
        check("release_and_reuse", GuestArenaAllocateAtOrAbove_nid_postfix(test_base + 4 * chunk, 3 * chunk, chunk) == retry);
        check("zero_allocation_refused", throws([] { GuestArenaAllocate_nid_postfix(0, chunk); }));
        check("fixed_outside_refused", throws([] {
            GuestArenaMarkUsed_nid_postfix(reinterpret_cast<void*>(test_base + 0x100000), chunk);
        }));
        check("overflow_hint_refused", throws([] {
            GuestArenaAllocateAtOrAbove_nid_postfix(UINTPTR_MAX - 1, chunk, chunk);
        }));
        check("overflow_alignment_refused", throws([] {
            GuestArenaAllocate_nid_postfix(chunk, UINT64_C(1) << 63);
        }));
        check("release_outside_refused", throws([] {
            GuestArenaRelease_nid_postfix(reinterpret_cast<void*>(UINTPTR_MAX - 1), chunk);
        }));
        check("host_overlap_overflow_refused", throws([] {
            GuestArenaHostRegionOverlaps_nid_postfix(UINTPTR_MAX - 1, chunk);
        }));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "allocator-probe: %s\n", error.what());
        check("unexpected_exception", false);
    }
    if (startup_obstacle) VirtualFree(startup_obstacle, 0, MEM_RELEASE);
    std::printf("{\"schema\":1,\"probe\":\"allocator\",\"summary\":true,\"failed\":%u}\n", failed);
    return failed ? 1 : 0;
}
