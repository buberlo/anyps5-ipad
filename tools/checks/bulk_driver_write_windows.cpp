#include <windows.h>
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <vector>

// Execute the existing production write-tracking fixture against the same
// actual GuestMemory/GuestArena/GuestAllocations objects as these edge cases.
#define main OriginalWriteTrackingMain
#include "prx/libSceAgcDriver/tests/WriteTracking.cpp"
#undef main

namespace {
std::atomic<unsigned> protectionCalls{0}, writeFaults{0};
std::array<unsigned, 2> failProtectionAt{};
unsigned scopeChecks = 0;

LONG CALLBACK recoverSharedWrite(EXCEPTION_POINTERS* exception) {
    const auto& record = *exception->ExceptionRecord;
    if (record.ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record.NumberParameters < 2 || record.ExceptionInformation[0] != 1) return EXCEPTION_CONTINUE_SEARCH;
    if (!GuestArena::GuestArenaHandleWrite_nid_postfix(record.ExceptionInformation[1])) return EXCEPTION_CONTINUE_SEARCH;
    ++writeFaults;
    return EXCEPTION_CONTINUE_EXECUTION;
}

struct SharedScopes {
    static constexpr std::size_t page = 0x4000, length = 4 * page;
    void* first = GuestArena::GuestArenaAllocate_nid_postfix(2 * length, length);
    void* second = static_cast<std::byte*>(first) + length;
    HANDLE original = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE, 0, length, nullptr);
    SharedScopes() {
        Require(original != nullptr, "cannot create scope-test shared section");
        GuestArena::GuestArenaMap_nid_postfix(first, length, original, 0, PAGE_READWRITE);
        GuestArena::GuestArenaMap_nid_postfix(second, length, original, 0, PAGE_READWRITE);
        std::memset(first, 0x5a, length);
        collect(first); collect(second);
    }
    ~SharedScopes() {
        GuestArena::GuestArenaReset_nid_postfix(first, 2 * length);
        GuestArena::GuestArenaRelease_nid_postfix(first, 2 * length);
        CloseHandle(original);
    }
    std::size_t collect(void* address) {
        std::array<void*, 32> pages{};
        auto count = pages.size();
        Require(GuestArena::GuestArenaCollectWrites_nid_postfix(reinterpret_cast<std::uintptr_t>(address), length, pages.data(), &count, true), "scope-test write collection failed");
        return count;
    }
    bool armed(std::size_t at = 0) {
        MEMORY_BASIC_INFORMATION memory{};
        Require(VirtualQuery(static_cast<std::byte*>(first) + at, &memory, sizeof(memory)) == sizeof(memory), "cannot query scope-test protection");
        return memory.Protect == PAGE_READONLY;
    }
};

using NativeCopy = void* (__cdecl*)(void*, const void*, std::size_t);
NativeCopy nativeCopy() {
    const auto crt = LoadLibraryW(L"ucrtbase.dll");
    Require(crt != nullptr, "cannot load Windows CRT");
    const auto copy = reinterpret_cast<NativeCopy>(GetProcAddress(crt, "memcpy"));
    Require(copy != nullptr, "cannot resolve CRT copy");
    return copy;
}

void fullRangeFaultGate() {
    SharedScopes mapping;
    constexpr std::size_t offset = SharedScopes::page - 7, bytes = 2 * SharedScopes::page + 31;
    const auto base = reinterpret_cast<std::uintptr_t>(mapping.first);
    {
        GuestAllocations::Mutation mutation;
        mutation.Add(mapping.first, SharedScopes::length, true, true);
    }
    struct RegistryRelease {
        void* pointer;
        ~RegistryRelease() { GuestAllocations::Mutation mutation; mutation.Remove(pointer); }
    } release{mapping.first};
    std::vector<unsigned char> source(bytes, 0x91);
    const auto copy = nativeCopy();
    const auto* option = std::getenv("APS5_BULK_DRIVER_WRITES");
    const bool enabled = option != nullptr && std::strcmp(option, "1") == 0;
    AgcDriver::GuestMemory::CollectWritesUncached(base, SharedScopes::length);
    const auto before = writeFaults.load();
    AgcDriver::GuestMemory::StoreOwnBytes(base + offset, bytes, [&] { copy(static_cast<std::byte*>(mapping.first) + offset, source.data(), bytes); });
    const auto faults = writeFaults.load() - before;
    Require(enabled ? faults == 0 : faults != 0, "the exact bulk opt-in did not select the expected fault-free/legacy copy");
    for (std::size_t at = 0; at < SharedScopes::length; ++at) {
        const auto expected = at >= offset && at < offset + bytes ? 0x91 : 0x5a;
        Require(static_cast<unsigned char*>(mapping.first)[at] == expected && static_cast<unsigned char*>(mapping.second)[at] == expected, "fault-gate bytes or untouched guards differ");
    }
    std::printf("Full-range native CRT copy: bulk=%u shared_write_faults=%u bytes_and_guards=PASS\n", enabled, faults);
    if (enabled) {
        std::atomic<bool> requested{false}, applied{false};
        std::exception_ptr mutationError;
        std::thread worker;
        struct Join {
            std::thread& worker;
            ~Join() { if (worker.joinable()) worker.join(); }
        } join{worker};
        AgcDriver::GuestMemory::StoreOwnBytes(base + offset, bytes, [&] {
            worker = std::thread([&] {
                try {
                    GuestAllocations::Mutation mutation;
                    requested.store(true);
                    mutation.Protect(mapping.first, SharedScopes::length, true, false, [&] {
                        // Each arena view is a separate MapViewOfFile3 reservation.
                        for (std::size_t at = 0; at < SharedScopes::length; at += SharedScopes::page) {
                            DWORD previous;
                            Require(VirtualProtect(static_cast<std::byte*>(mapping.first) + at, SharedScopes::page, PAGE_READONLY, &previous), "cannot apply the waited mapping protection");
                        }
                        GuestArena::GuestArenaSetProtection_nid_postfix(base, SharedScopes::length, PAGE_READONLY);
                        applied.store(true);
                    });
                } catch (...) { mutationError = std::current_exception(); }
            });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            while (!requested.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
            Require(requested.load(), "the mapping mutation did not start");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            Require(!applied.load(), "a mapping mutation passed the destination lease during a bulk copy");
            copy(static_cast<std::byte*>(mapping.first) + offset, source.data(), bytes);
        });
        worker.join();
        if (mutationError) std::rethrow_exception(mutationError);
        Require(applied.load(), "the mapping lease was not released after the bulk store");
        {
            GuestAllocations::Mutation mutation;
            mutation.Protect(mapping.first, SharedScopes::length, true, true, [&] {
                for (std::size_t at = 0; at < SharedScopes::length; at += SharedScopes::page) {
                    DWORD previous;
                    Require(VirtualProtect(static_cast<std::byte*>(mapping.first) + at, SharedScopes::page, PAGE_READWRITE, &previous), "cannot restore the waited mapping protection");
                }
                GuestArena::GuestArenaSetProtection_nid_postfix(base, SharedScopes::length, PAGE_READWRITE);
            });
        }
        std::printf("Registered mapping mutation: waited during copy, resumed after tracker/scope/lease cleanup=PASS\n");
    }
    // Incomplete registered coverage must retain the old copy path.
    {
        GuestAllocations::Mutation mutation;
        mutation.Remove(mapping.first);
        mutation.Add(mapping.first, SharedScopes::page, true, true);
    }
    mapping.collect(mapping.first); mapping.collect(mapping.second);
    const auto beforeFallback = writeFaults.load();
    AgcDriver::GuestMemory::StoreOwnBytes(base + offset, bytes, [&] { copy(static_cast<std::byte*>(mapping.first) + offset, source.data(), bytes); });
    Require(writeFaults.load() > beforeFallback, "partial registry coverage opened an unsafe bulk scope");
    std::printf("Incomplete mapping lease: legacy shared-write faults retained=PASS\n");
}

void scopeEdges() {
    SharedScopes mapping;
    std::vector<unsigned char> source(2 * SharedScopes::page, 0xa5);
    const auto copy = nativeCopy();
    const auto base = reinterpret_cast<std::uintptr_t>(mapping.first);
    constexpr auto size = SharedScopes::length;
    const auto faultingWrite = [&] {
        const auto before = writeFaults.load();
        static_cast<volatile unsigned char*>(mapping.first)[7] ^= 1;
        Require(writeFaults.load() > before, "scope cleanup left subsequent CPU writes untracked");
    };

    {
        const GuestArena::HostWrite outer(mapping.first, size);
        Require(outer.Open(), "outer scope refused a writable shared range");
        mapping.collect(mapping.first);
        const auto before = writeFaults.load();
        {
            const GuestArena::HostWrite inner(mapping.first, size);
            Require(inner.Open(), "nested scope refused a writable range");
            mapping.collect(mapping.second);
            Require(copy(mapping.first, source.data(), source.size()) == mapping.first, "native CRT copy result differs");
        }
        mapping.collect(mapping.first);
        copy(mapping.first, source.data(), source.size());
        Require(writeFaults.load() == before, "a resetting collect rearmed an active nested host scope");
    }
    Require(mapping.collect(mapping.second) == 16, "a completed nested scope did not invalidate all aliases");
    mapping.collect(mapping.first);
    faultingWrite();
    ++scopeChecks;

    mapping.collect(mapping.first); mapping.collect(mapping.second);
    try {
        const GuestArena::HostWrite scope(mapping.first, size);
        Require(scope.Open(), "throwing scope did not open");
        copy(mapping.first, source.data(), 8);
        throw std::runtime_error("expected scope exception");
    } catch (const std::runtime_error&) {}
    mapping.collect(mapping.first);
    faultingWrite();
    ++scopeChecks;

    mapping.collect(mapping.first); mapping.collect(mapping.second);
    const auto protectedPage = base + SharedScopes::page;
    DWORD previous;
    Require(VirtualProtect(reinterpret_cast<void*>(protectedPage), SharedScopes::page, PAGE_READONLY, &previous), "cannot make logical read-only view");
    GuestArena::GuestArenaSetProtection_nid_postfix(protectedPage, SharedScopes::page, PAGE_READONLY);
    const auto calls = protectionCalls.load();
    Require(!GuestArena::GuestArenaBeginHostWrite_nid_postfix(mapping.first, size), "read-only view was opened for a host write");
    Require(protectionCalls.load() == calls, "read-only refusal changed another view before preflight finished");
    GuestArena::GuestArenaSetProtection_nid_postfix(protectedPage, SharedScopes::page, PAGE_READWRITE);
    Require(VirtualProtect(reinterpret_cast<void*>(protectedPage), SharedScopes::page, PAGE_READWRITE, &previous), "cannot restore logical writable view");
    mapping.collect(mapping.first);
    faultingWrite();
    ++scopeChecks;

    for (bool failRollback : {false, true}) {
        mapping.collect(mapping.first); mapping.collect(mapping.second);
        const auto firstCall = protectionCalls.load() + 1;
        failProtectionAt = {firstCall + 1, failRollback ? firstCall + 2 : 0};
        bool failed = false;
        try { GuestArena::GuestArenaBeginHostWrite_nid_postfix(mapping.first, size); }
        catch (const std::system_error&) { failed = true; }
        failProtectionAt = {};
        Require(failed, "injected VirtualProtect failure was swallowed");
        if (!failRollback) for (std::size_t at = 0; at < size; at += SharedScopes::page) Require(mapping.armed(at), "failed opening did not restore an earlier protection");
        Require(mapping.collect(mapping.first) != 0, "a failed/partial opening was falsely remembered as clean");
        Require(mapping.collect(mapping.first) == 0, "failure cleanup left dirty generations permanently pinned");
        faultingWrite();
        ++scopeChecks;
    }

    // A raw reset/remap intentionally bypasses registry leases. Ending the old
    // scope must notify the old aliases without underflowing the new view.
    mapping.collect(mapping.first); mapping.collect(mapping.second);
    Require(GuestArena::GuestArenaBeginHostWrite_nid_postfix(mapping.first, size), "cannot begin remap scope");
    copy(mapping.first, source.data(), 8);
    mapping.collect(mapping.second);
    copy(static_cast<std::byte*>(mapping.first) + 16, source.data(), 8);
    GuestArena::GuestArenaReset_nid_postfix(mapping.first, size);
    const auto replacement = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE, 0, size, nullptr);
    Require(replacement != nullptr, "cannot create replacement section");
    GuestArena::GuestArenaMap_nid_postfix(mapping.first, size, replacement, 0, PAGE_READWRITE);
    CloseHandle(replacement);
    GuestArena::GuestArenaEndHostWrite_nid_postfix(mapping.first, size);
    Require(mapping.collect(mapping.second) == 16, "ending a removed view lost its final generation in the remaining aliases");
    mapping.collect(mapping.first);
    Require(mapping.armed(), "ending the old scope pinned the replacement view");
    faultingWrite();
    ++scopeChecks;

    Require(!GuestArena::GuestArenaBeginHostWrite_nid_postfix(reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max() - 3), 8), "overflowing host-write range was accepted");
    ++scopeChecks;
}
}

extern "C" BOOL WINAPI BulkWriteTestVirtualProtect(void* pointer, SIZE_T bytes, DWORD protection, PDWORD previous) {
    const auto call = ++protectionCalls;
    if (std::find(failProtectionAt.begin(), failProtectionAt.end(), call) != failProtectionAt.end()) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return VirtualProtect(pointer, bytes, protection, previous);
}

int main() {
    const auto handler = AddVectoredExceptionHandler(1, recoverSharedWrite);
    if (handler == nullptr) return 1;
    try {
        if (OriginalWriteTrackingMain() != 0) throw std::runtime_error("actual AGC write-tracking fixture failed");
        fullRangeFaultGate();
        scopeEdges();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "bulk driver write check failed: %s\n", error.what());
        RemoveVectoredExceptionHandler(handler);
        return 1;
    }
    RemoveVectoredExceptionHandler(handler);
    std::printf("Real Windows shared HostWrite scope checks=%u handled_write_faults=%u errors=0\n", scopeChecks, writeFaults.load());
    return 0;
}
