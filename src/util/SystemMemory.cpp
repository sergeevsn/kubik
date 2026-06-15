#include "kubik/SystemMemory.hpp"

#include <fstream>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif

namespace kubik {

std::uint64_t availablePhysicalBytes() {
#if defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return status.ullAvailPhys;
    }
    return 0;
#elif defined(__linux__)
    {
        std::ifstream meminfo("/proc/meminfo");
        std::string line;
        while (std::getline(meminfo, line)) {
            if (line.compare(0, 13, "MemAvailable:") == 0) {
                const std::uint64_t avail_kb = std::stoull(line.substr(13));
                if (avail_kb > 0) {
                    return avail_kb * 1024u;
                }
                break;
            }
        }
    }
    struct sysinfo info{};
    if (sysinfo(&info) == 0) {
        return static_cast<std::uint64_t>(info.freeram) * info.mem_unit;
    }
    return 0;
#elif defined(__APPLE__)
    vm_statistics64_data_t vm_stats{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vm_stats), &count) == KERN_SUCCESS) {
        return static_cast<std::uint64_t>(vm_stats.free_count) * vm_page_size;
    }
    return 0;
#else
    return 0;
#endif
}

}  // namespace kubik
