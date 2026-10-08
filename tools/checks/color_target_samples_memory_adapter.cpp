// Test-only macOS memory boundary adapter. The production guest tracker assumes
// Windows VirtualQuery or Linux /proc maps. This verifies native mapped ranges;
// it does not qualify Wine's guest page tracker, write watch or deferred GPU work.
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace AgcDriver::GuestMemory {
void CheckRange(const void* pointer, std::size_t bytes, std::size_t alignment, bool writable) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    if (!alignment || !address || address % alignment ||
        bytes > std::numeric_limits<std::uintptr_t>::max() - address)
        throw std::runtime_error("test adapter: invalid guest range");
    const auto end = address + bytes;
    auto cursor = address;
    while (cursor < end) {
        mach_vm_address_t begin = cursor;
        mach_vm_size_t size = 0;
        vm_region_basic_info_data_64_t info{};
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object = MACH_PORT_NULL;
        const auto result = mach_vm_region(mach_task_self(), &begin, &size, VM_REGION_BASIC_INFO_64,
                                           reinterpret_cast<vm_region_info_t>(&info), &count, &object);
        if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
        if (result != KERN_SUCCESS || begin > cursor || !size || begin + size <= cursor ||
            (info.protection & VM_PROT_READ) == 0 || (writable && (info.protection & VM_PROT_WRITE) == 0))
            throw std::runtime_error("test adapter: inaccessible guest range");
        cursor = std::min<std::uint64_t>(begin + size, end);
    }
}
void Read(std::uint64_t address, std::span<std::byte> out, std::size_t alignment) {
    CheckRange(reinterpret_cast<const void*>(address), out.size(), alignment);
    std::memcpy(out.data(), reinterpret_cast<const void*>(address), out.size());
}
void Write(std::uint64_t address, std::span<const std::byte> in, std::size_t alignment) {
    CheckRange(reinterpret_cast<const void*>(address), in.size(), alignment, true);
    std::memcpy(reinterpret_cast<void*>(address), in.data(), in.size());
}
}
