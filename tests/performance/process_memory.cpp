#include "process_memory.h"
#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(_WIN32)
// clang-format off: psapi.h requires Windows declarations first.
#include <windows.h>
#include <psapi.h>
// clang-format on
#elif defined(__linux__)
#include <fstream>
#include <unistd.h>
#endif

std::optional<std::uint64_t> currentResidentBytes() {
#if defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info),
                  &count) == KERN_SUCCESS)
        return info.resident_size;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS info{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info)))
        return info.WorkingSetSize;
#elif defined(__linux__)
    std::ifstream input("/proc/self/statm");
    std::uint64_t total = 0, resident = 0;
    const auto page = sysconf(_SC_PAGESIZE);
    if (page > 0 && input >> total >> resident)
        return resident * std::uint64_t(page);
#endif
    return std::nullopt;
}

std::optional<std::uint64_t> currentPhysicalFootprintBytes() {
#if defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) ==
        KERN_SUCCESS)
        return info.phys_footprint;
#endif
    return std::nullopt;
}
