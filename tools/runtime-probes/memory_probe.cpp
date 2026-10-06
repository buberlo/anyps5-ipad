#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <initializer_list>

using Allocate = PVOID (WINAPI*)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER*, ULONG);
using Map = PVOID (WINAPI*)(HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER*, ULONG);
using Unmap = BOOL (WINAPI*)(HANDLE, PVOID, ULONG);

static unsigned failures;
static bool result(const char* name, bool pass, DWORD error) {
    std::printf("{\"schema\":1,\"probe\":\"memory\",\"case\":\"%s\",\"status\":\"%s\",\"win32_error\":%lu}\n",
        name, pass ? "pass" : "fail", static_cast<unsigned long>(pass ? 0 : error));
    std::fflush(stdout);
    failures += !pass;
    return pass;
}

static bool result(const char* name, bool pass) {
    // Function arguments have no guaranteed evaluation order: a default
    // GetLastError() argument could run before the API being checked.
    const DWORD error = pass ? ERROR_SUCCESS : GetLastError();
    return result(name, pass, error);
}

static FARPROC symbol(const char* name) {
    for (const char* dll : {"kernelbase.dll", "kernel32.dll"}) {
        if (HMODULE mod = GetModuleHandleA(dll))
            if (auto proc = GetProcAddress(mod, name)) return proc;
    }
    return nullptr;
}

int main(int argc, char** argv) {
    std::uintptr_t base = UINT64_C(0x200000000);
    std::size_t size = 0x10000000;
    if (argc > 3) return 64;
    for (int i = 1; i < argc; ++i) {
        char* end = nullptr;
        const auto value = std::strtoull(argv[i], &end, 0);
        if (!argv[i][0] || argv[i][0] == '-' || *end || !value) return 64;
        if (i == 1) base = static_cast<std::uintptr_t>(value);
        else size = static_cast<std::size_t>(value);
    }
    if (base % 0x10000 || size % 0x10000 || size < 0x10000 || size > UINTPTR_MAX - base) return 64;
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    std::printf("{\"schema\":1,\"probe\":\"memory\",\"base\":\"0x%llx\",\"size\":%llu,\"windows_page_size\":%lu,\"allocation_granularity\":%lu}\n",
        static_cast<unsigned long long>(base), static_cast<unsigned long long>(size),
        static_cast<unsigned long>(system.dwPageSize), static_cast<unsigned long>(system.dwAllocationGranularity));
    // Wine's VirtualQuery is a guest census. The app must additionally log the
    // native Mach map; this probe never infers a 512 GiB usable arena from it.
    unsigned regions = 0;
    for (auto cursor = base; cursor < base + size;) {
        MEMORY_BASIC_INFORMATION info{};
        if (!result("query_range", VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info)) != 0)) return 1;
        const auto begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        if (!info.RegionSize || begin > cursor || info.RegionSize > UINTPTR_MAX - begin || begin + info.RegionSize <= cursor)
            return result("query_progress", false, ERROR_INVALID_DATA) ? 0 : 1;
        std::printf("{\"schema\":1,\"probe\":\"memory\",\"region_base\":\"0x%llx\",\"region_size\":%llu,\"state\":%lu,\"protect\":%lu}\n",
            static_cast<unsigned long long>(begin), static_cast<unsigned long long>(info.RegionSize),
            static_cast<unsigned long>(info.State), static_cast<unsigned long>(info.Protect));
        cursor = begin + info.RegionSize;
        if (++regions > 4096) return result("query_region_limit", false, ERROR_MORE_DATA) ? 0 : 1;
    }
    auto allocate = reinterpret_cast<Allocate>(symbol("VirtualAlloc2"));
    auto map = reinterpret_cast<Map>(symbol("MapViewOfFile3"));
    auto unmap = reinterpret_cast<Unmap>(symbol("UnmapViewOfFile2"));
    if (!result("required_apis", allocate && map && unmap, ERROR_PROC_NOT_FOUND)) return 1;
    void* address = reinterpret_cast<void*>(base);
    constexpr SIZE_T guest_page = 0x4000;
    if (!result("reserve_exact_placeholder", allocate(GetCurrentProcess(), address, size,
            MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, nullptr, 0) == address)) return 1;
    // Refusing a second fixed reservation is part of the contract. It must
    // neither relocate it nor overwrite the first reservation.
    void* collision = allocate(GetCurrentProcess(), address, size,
        MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, nullptr, 0);
    result("fixed_collision_refused", collision == nullptr);
    if (collision) return 1;
    if (!result("split_16k_placeholder", VirtualFree(address, guest_page, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER))) return 1;
    if (!result("replace_private_write_watch", allocate(GetCurrentProcess(), address, guest_page,
            MEM_RESERVE | MEM_COMMIT | MEM_REPLACE_PLACEHOLDER | MEM_WRITE_WATCH, PAGE_READWRITE, nullptr, 0) == address)) return 1;
    auto words = static_cast<volatile std::uint32_t*>(address);
    result("zero_commit", words[0] == 0 && words[guest_page / sizeof(*words) - 1] == 0);
    words[0] = 0x12345678;
    void* dirty[16]{};
    ULONG_PTR count = 16;
    DWORD granularity = 0;
    const UINT watch_error = GetWriteWatch(WRITE_WATCH_FLAG_RESET, address, guest_page, dirty, &count, &granularity);
    bool found = false;
    for (ULONG_PTR i = 0; i < count && i < 16; ++i) found |= dirty[i] == address;
    result("write_watch_dirty", watch_error == 0 && found, watch_error);
    count = 16;
    const UINT clean_error = GetWriteWatch(0, address, guest_page, dirty, &count, &granularity);
    result("write_watch_reset", clean_error == 0 && count == 0, clean_error);
    DWORD previous = 0;
    result("protect_readonly", VirtualProtect(address, guest_page, PAGE_READONLY, &previous));
    MEMORY_BASIC_INFORMATION protected_info{};
    result("query_readonly", VirtualQuery(address, &protected_info, sizeof(protected_info)) && protected_info.Protect == PAGE_READONLY);
    if (!result("private_preserve_placeholder", VirtualFree(address, guest_page, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER))) return 1;
    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE, 0, guest_page, nullptr);
    if (!result("create_shared_section", section != nullptr)) return 1;
    if (!result("replace_shared_16k", map(section, GetCurrentProcess(), address, 0, guest_page,
            MEM_REPLACE_PLACEHOLDER, PAGE_EXECUTE_READWRITE, nullptr, 0) == address)) return 1;
    void* alias = map(section, GetCurrentProcess(), nullptr, 0, guest_page, 0, PAGE_READWRITE, nullptr, 0);
    if (!result("shared_alias", alias != nullptr)) return 1;
    auto alias_words = static_cast<volatile std::uint32_t*>(alias);
    words[0] = 0xa1b2c3d4;
    result("alias_reads_primary", alias_words[0] == 0xa1b2c3d4);
    result("shared_protect_readonly", VirtualProtect(address, guest_page, PAGE_READONLY, &previous));
    alias_words[guest_page / sizeof(*words) - 1] = 0xfeed1234;
    result("primary_reads_alias", words[guest_page / sizeof(*words) - 1] == 0xfeed1234);
    result("alias_unmap", unmap(GetCurrentProcess(), alias, 0));
    result("shared_preserve_placeholder", unmap(GetCurrentProcess(), address, MEM_PRESERVE_PLACEHOLDER));
    CloseHandle(section);
    if (!result("coalesce_placeholders", VirtualFree(address, size, MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS))) return 1;
    if (!result("release_placeholder", VirtualFree(address, 0, MEM_RELEASE))) return 1;
    void* reused = allocate(GetCurrentProcess(), address, size,
        MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, nullptr, 0);
    result("reserve_same_range_again", reused == address);
    if (reused) result("final_release", VirtualFree(reused, 0, MEM_RELEASE));
    std::printf("{\"schema\":1,\"probe\":\"memory\",\"summary\":true,\"failed\":%u}\n", failures);
    return failures ? 1 : 0;
}
