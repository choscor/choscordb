#include "app/diagnostics_memory.h"
#include <limits>
#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <psapi.h>
#include <windows.h>
#elif defined(__linux__)
#include <fstream>
#include <unistd.h>
#endif

namespace choscordb {
std::optional<quint64> residentBytes() {
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
    quint64 total = 0, resident = 0;
    const auto page = sysconf(_SC_PAGESIZE);
    if (page > 0 && input >> total >> resident)
        return resident * quint64(page);
#endif
    return std::nullopt;
}
std::optional<quint64> footprintBytes() {
#if defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) ==
        KERN_SUCCESS)
        return info.phys_footprint;
#endif
    return std::nullopt;
}
std::optional<quint64> peakBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS info{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info)))
        return info.PeakWorkingSetSize;
#elif defined(__linux__)
    std::ifstream input("/proc/self/status");
    std::string label;
    while (input >> label) {
        if (label == "VmHWM:") {
            quint64 kib = 0;
            if (input >> kib)
                return kib * 1024;
        }
        input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
#endif
    return std::nullopt;
}
} // namespace choscordb
